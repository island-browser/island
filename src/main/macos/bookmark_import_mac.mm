#include "bookmark_import.h"

#import <CoreFoundation/CoreFoundation.h>

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace island {
namespace {

std::string StdStringFromCF(CFStringRef string) {
    if (string == nullptr) {
        return {};
    }
    const CFRange range = CFRangeMake(0, CFStringGetLength(string));
    CFIndex byte_count = 0;
    CFStringGetBytes(string, range, kCFStringEncodingUTF8, 0, false, nullptr, 0, &byte_count);
    std::string out(static_cast<std::size_t>(byte_count), '\0');
    if (byte_count > 0) {
        CFStringGetBytes(string, range, kCFStringEncodingUTF8, 0, false,
                         reinterpret_cast<UInt8*>(out.data()), byte_count, nullptr);
    }
    return out;
}

// Recursively walks Safari's bookmarks plist: nodes are dictionaries whose
// WebBookmarkType is "WebBookmarkTypeLeaf" (URLString + URIDictionary.title)
// or "WebBookmarkTypeList" (children under Children). The root dictionary's
// top-level Children array covers BookmarksBar, BookmarksMenu, and any other
// custom folders.
std::vector<BookmarkItem> CollectFromPlist(CFTypeRef node, std::size_t& budget) {
    std::vector<BookmarkItem> items;
    if (node == nullptr || budget == 0) {
        return items;
    }
    const CFTypeID type_id = CFGetTypeID(node);
    if (type_id == CFArrayGetTypeID()) {
        CFArrayRef array = static_cast<CFArrayRef>(node);
        const CFIndex count = CFArrayGetCount(array);
        for (CFIndex i = 0; i < count && budget > 0 && items.size() < kMaxImportedBookmarks; ++i) {
            std::vector<BookmarkItem> child =
                CollectFromPlist(CFArrayGetValueAtIndex(array, i), budget);
            items.insert(items.end(), std::make_move_iterator(child.begin()),
                         std::make_move_iterator(child.end()));
        }
        return items;
    }
    if (type_id != CFDictionaryGetTypeID()) {
        return items;
    }
    CFDictionaryRef dict = static_cast<CFDictionaryRef>(node);
    CFStringRef type =
        static_cast<CFStringRef>(CFDictionaryGetValue(dict, CFSTR("WebBookmarkType")));
    if (type == nullptr) {
        return items;
    }

    if (CFStringCompare(type, CFSTR("WebBookmarkTypeLeaf"), 0) == kCFCompareEqualTo) {
        CFStringRef url = static_cast<CFStringRef>(CFDictionaryGetValue(dict, CFSTR("URLString")));
        if (url == nullptr) {
            return items;
        }
        BookmarkItem item;
        item.url = StdStringFromCF(url);
        CFTypeRef uri_dictionary = CFDictionaryGetValue(dict, CFSTR("URIDictionary"));
        if (uri_dictionary != nullptr && CFGetTypeID(uri_dictionary) == CFDictionaryGetTypeID()) {
            item.title = StdStringFromCF(static_cast<CFStringRef>(CFDictionaryGetValue(
                static_cast<CFDictionaryRef>(uri_dictionary), CFSTR("title"))));
        }
        if (item.title.empty()) {
            item.title = StdStringFromCF(
                static_cast<CFStringRef>(CFDictionaryGetValue(dict, CFSTR("Title"))));
        }
        if (!item.url.empty()) {
            items.push_back(std::move(item));
            --budget;
        }
        return items;
    }

    if (CFStringCompare(type, CFSTR("WebBookmarkTypeList"), 0) == kCFCompareEqualTo) {
        return CollectFromPlist(CFDictionaryGetValue(dict, CFSTR("Children")), budget);
    }
    return items;
}

}  // namespace

std::optional<std::vector<BookmarkItem>> ImportSafariBookmarks(
    const std::filesystem::path& base_home) {
    const std::filesystem::path plist_path = base_home / "Library/Safari/Bookmarks.plist";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(plist_path, ec)) {
        return std::nullopt;
    }

    std::ifstream in(plist_path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    std::string bytes;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return std::nullopt;
    }

    CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.data()),
                                  static_cast<CFIndex>(bytes.size()));
    if (data == nullptr) {
        return std::nullopt;
    }

    CFErrorRef error = nullptr;
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        kCFAllocatorDefault, data, kCFPropertyListImmutable, nullptr, &error);
    CFRelease(data);
    if (plist == nullptr) {
        if (error != nullptr) {
            CFRelease(error);
        }
        return std::nullopt;
    }
    if (CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
        CFRelease(plist);
        return std::nullopt;
    }

    std::size_t budget = kMaxImportedBookmarks;
    std::vector<BookmarkItem> items = CollectFromPlist(
        CFDictionaryGetValue(static_cast<CFDictionaryRef>(plist), CFSTR("Children")), budget);
    CFRelease(plist);
    return items;
}

}  // namespace island
