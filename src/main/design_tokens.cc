#include "design_tokens.h"

namespace island {

ChromeTokens ChromeTokens::ForTheme(ChromeTheme theme) noexcept {
    if (theme == ChromeTheme::kDark) {
        return {
            .background = {.argb = 0xFF0D1B26U},
            .surface = {.argb = 0xFF142633U},
            .surface_secondary = {.argb = 0xFF1B3040U},
            .text = {.argb = 0xFFEAF3F3U},
            .text_secondary = {.argb = 0xFF9CB0B5U},
            .border = {.argb = 0xFF29414EU},
            .accent = {.argb = 0xFF168C99U},
        };
    }

    return {
        .background = {.argb = 0xFFF3F0E9U},
        .surface = {.argb = 0xFFFFFEFBU},
        .surface_secondary = {.argb = 0xFFECE9E2U},
        .text = {.argb = 0xFF18303AU},
        .text_secondary = {.argb = 0xFF687A7DU},
        .border = {.argb = 0xFFD8D8D0U},
        .accent = {.argb = 0xFF168C99U},
    };
}

}  // namespace island
