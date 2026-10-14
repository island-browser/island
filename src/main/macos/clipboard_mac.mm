#include "clipboard.h"

#import <Cocoa/Cocoa.h>

#include <string>

namespace island {

bool CopyTextToClipboard(std::string_view text) {
    NSString* value = [[NSString alloc] initWithBytes:text.data()
                                               length:text.size()
                                             encoding:NSUTF8StringEncoding];
    if (value == nil) {
        return false;
    }
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    const BOOL written = [pasteboard setString:value forType:NSPasteboardTypeString];
    [value release];
    return written == YES;
}

}  // namespace island
