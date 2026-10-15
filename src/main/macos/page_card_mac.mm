#include "page_card.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <unordered_map>

// A mouse-transparent view hosting the card's frame: a canvas-colored ring (so the page's square
// corners are repainted round) and, above it, one soft shadow cast into the margin.
@interface IslandCardFrameView : NSView
@property(nonatomic, weak) NSView* target;
@property(nonatomic) island::PageCardStyle style;
@property(nonatomic, strong) CAShapeLayer* ring;
@property(nonatomic, strong) CAShapeLayer* cardShadow;
@property(nonatomic, strong) CAShapeLayer* cardShadowMask;
@property(nonatomic, strong) id observer;
- (void)sync;
@end

namespace {

NSColor* ToNSColor(island::ArgbColor color) {
    return [NSColor colorWithSRGBRed:((color.argb >> 16) & 0xFF) / 255.0
                               green:((color.argb >> 8) & 0xFF) / 255.0
                                blue:(color.argb & 0xFF) / 255.0
                               alpha:1.0];
}

}  // namespace

// Hosts the corner patches of rows and pills: one shape layer, mouse-transparent.
@interface IslandPatchView : NSView
@property(nonatomic, strong) CAShapeLayer* patches;
@end

@implementation IslandPatchView
- (NSView*)hitTest:(NSPoint)point {
    return nil;
}
- (BOOL)isOpaque {
    return NO;
}
@end

@implementation IslandCardFrameView

- (NSView*)hitTest:(NSPoint)point {
    return nil;
}

- (BOOL)isOpaque {
    return NO;
}

- (void)sync {
    NSView* target = self.target;
    NSView* parent = self.superview;
    if (target == nil || parent == nil) {
        return;
    }
    const island::PageCardStyle style = self.style;
    const CGFloat inset = style.inset_dip;
    // The frame covers the card plus its margin, never beyond the window.
    NSRect outer = NSIntersectionRect(NSInsetRect(target.frame, -inset, -inset), parent.bounds);
    self.frame = outer;
    self.wantsLayer = YES;
    if (self.ring == nil) {
        self.ring = [CAShapeLayer layer];
        self.cardShadow = [CAShapeLayer layer];
        self.cardShadowMask = [CAShapeLayer layer];
        [self.layer addSublayer:self.ring];
        [self.layer addSublayer:self.cardShadow];
    }
    const CGRect bounds = CGRectMake(0, 0, outer.size.width, outer.size.height);
    const CGRect card =
        CGRectMake(target.frame.origin.x - outer.origin.x, target.frame.origin.y - outer.origin.y,
                   target.frame.size.width, target.frame.size.height);
    const CGFloat radius = MIN(style.radius_dip, MIN(card.size.width, card.size.height) / 2);

    CGMutablePathRef ring_path = CGPathCreateMutable();
    CGPathAddRect(ring_path, nullptr, bounds);
    CGPathRef hole = CGPathCreateWithRoundedRect(card, radius, radius, nullptr);
    CGPathAddPath(ring_path, nullptr, hole);

    for (CAShapeLayer* layer in @[ self.ring, self.cardShadow ]) {
        layer.frame = bounds;
    }
    self.ring.path = ring_path;
    self.ring.fillRule = kCAFillRuleEvenOdd;
    self.ring.fillColor = ToNSColor(style.canvas).CGColor;

    self.cardShadowMask.frame = bounds;
    self.cardShadowMask.path = ring_path;
    self.cardShadowMask.fillRule = kCAFillRuleEvenOdd;
    self.cardShadow.mask = self.cardShadowMask;
    self.cardShadow.path = hole;
    self.cardShadow.fillColor = NSColor.blackColor.CGColor;
    self.cardShadow.shadowColor = NSColor.blackColor.CGColor;
    self.cardShadow.shadowPath = hole;
    self.cardShadow.shadowRadius = MAX(2.0, inset);
    self.cardShadow.shadowOffset = CGSizeMake(0, -1);
    self.cardShadow.shadowOpacity =
        (style.shadow && inset > 0) ? (style.dark ? 0.6f : 0.22f) : 0.0f;
    self.cardShadow.hidden = !(style.shadow && inset > 0);

    CGPathRelease(hole);
    CGPathRelease(ring_path);
    self.hidden = style.radius_dip <= 0 && !(style.shadow && inset > 0);
}

@end

namespace island {
namespace {

std::unordered_map<void*, IslandCardFrameView*>& Frames() {
    static auto* map = new std::unordered_map<void*, IslandCardFrameView*>();
    return *map;
}

// The handle CEF reports for a view-hosted browser is the window's content view; the page lives
// in its WebContentsViewCocoa subviews (hidden ones belong to background tabs).
void CollectWebViews(NSView* content, std::vector<NSView*>& out) {
    for (NSView* sub in content.subviews) {
        if ([NSStringFromClass([sub class]) isEqualToString:@"WebContentsViewCocoa"] &&
            !sub.hidden && sub.frame.size.width > 0 && sub.frame.size.height > 0) {
            out.push_back(sub);
        }
    }
}

void RemoveFrame(IslandCardFrameView* frame) {
    if (frame.observer != nil) {
        [[NSNotificationCenter defaultCenter] removeObserver:frame.observer];
    }
    [frame removeFromSuperview];
}

}  // namespace

void ApplyPageCardStyle(const std::vector<void*>& handles, const PageCardStyle& style) {
    std::vector<NSView*> views;
    for (void* handle : handles) {
        NSView* content = (__bridge NSView*)handle;
        if (content != nil) {
            CollectWebViews(content, views);
        }
    }
    auto& frames = Frames();
    for (auto it = frames.begin(); it != frames.end();) {
        bool listed = false;
        for (NSView* view : views) {
            listed = listed || (__bridge void*)view == it->first;
        }
        if (listed) {
            ++it;
        } else {
            RemoveFrame(it->second);
            it = frames.erase(it);
        }
    }
    for (NSView* view : views) {
        void* key = (__bridge void*)view;
        auto found = frames.find(key);
        IslandCardFrameView* frame = found != frames.end() ? found->second : nil;
        if (frame == nil) {
            frame = [[IslandCardFrameView alloc] initWithFrame:view.frame];
            frame.target = view;
            view.postsFrameChangedNotifications = YES;
            __weak IslandCardFrameView* weak_frame = frame;
            frame.observer = [[NSNotificationCenter defaultCenter]
                addObserverForName:NSViewFrameDidChangeNotification
                            object:view
                             queue:nil
                        usingBlock:^(NSNotification*) {
                          [weak_frame sync];
                        }];
            frames[key] = frame;
        }
        frame.style = style;
        // Always the topmost subview, above the page and the compositor.
        if (frame.superview != view.superview || view.superview.subviews.lastObject != frame) {
            [frame removeFromSuperview];
            [view.superview addSubview:frame positioned:NSWindowAbove relativeTo:nil];
        }
        [frame sync];
    }
}

namespace {

IslandPatchView* g_patch_view = nil;

// The area between a rect's corner and the quarter circle inscribed in it (y up).
void AddWedge(CGMutablePathRef path, CGPoint corner, CGPoint start, CGPoint center, CGFloat radius,
              CGFloat start_angle, CGFloat end_angle, bool clockwise) {
    CGPathMoveToPoint(path, nullptr, corner.x, corner.y);
    CGPathAddLineToPoint(path, nullptr, start.x, start.y);
    CGPathAddArc(path, nullptr, center.x, center.y, radius, start_angle, end_angle, clockwise);
    CGPathCloseSubpath(path);
}

}  // namespace

void ApplyCornerPatches(const std::vector<void*>& handles, const std::vector<CornerPatch>& patches,
                        ArgbColor canvas) {
    NSView* content = handles.empty() ? nil : (__bridge NSView*)handles.front();
    if (content == nil) {
        return;
    }
    if (g_patch_view == nil || g_patch_view.superview != content) {
        [g_patch_view removeFromSuperview];
        g_patch_view = [[IslandPatchView alloc] initWithFrame:content.bounds];
        g_patch_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        g_patch_view.wantsLayer = YES;
        g_patch_view.patches = [CAShapeLayer layer];
        [g_patch_view.layer addSublayer:g_patch_view.patches];
        [content addSubview:g_patch_view positioned:NSWindowAbove relativeTo:nil];
    } else if (content.subviews.lastObject != g_patch_view) {
        [g_patch_view removeFromSuperview];
        [content addSubview:g_patch_view positioned:NSWindowAbove relativeTo:nil];
    }
    g_patch_view.frame = content.bounds;
    g_patch_view.patches.frame = content.bounds;
    const CGFloat height = content.bounds.size.height;
    CGMutablePathRef path = CGPathCreateMutable();
    for (const CornerPatch& patch : patches) {
        const CGFloat r = MIN(patch.radius, MIN(patch.width, patch.height) / 2);
        if (r <= 0) {
            continue;
        }
        const CGFloat x = patch.x;
        const CGFloat y = height - patch.y - patch.height;
        const CGFloat w = patch.width;
        const CGFloat h = patch.height;
        AddWedge(path, CGPointMake(x, y + h), CGPointMake(x + r, y + h),
                 CGPointMake(x + r, y + h - r), r, M_PI_2, M_PI, false);
        AddWedge(path, CGPointMake(x + w, y + h), CGPointMake(x + w - r, y + h),
                 CGPointMake(x + w - r, y + h - r), r, M_PI_2, 0, true);
        AddWedge(path, CGPointMake(x + w, y), CGPointMake(x + w, y + r),
                 CGPointMake(x + w - r, y + r), r, 0, -M_PI_2, true);
        AddWedge(path, CGPointMake(x, y), CGPointMake(x, y + r), CGPointMake(x + r, y + r), r, M_PI,
                 M_PI + M_PI_2, false);
    }
    g_patch_view.patches.path = path;
    g_patch_view.patches.fillColor = ToNSColor(canvas).CGColor;
    CGPathRelease(path);
}

}  // namespace island
