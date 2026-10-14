#ifndef ISLAND_MAIN_STARTUP_OPTIONS_H_
#define ISLAND_MAIN_STARTUP_OPTIONS_H_

#include <span>
#include <string_view>

namespace island {

// The production start page: a fixed local document styled with the Graphite tokens
// (neutral surface, hairlines, Geist stack with system fallbacks, mono uppercase label,
// keycap hints) that follows the system light/dark scheme. It carries no script, link,
// image, or external reference, so startup stays offline. Readable source of the encoded
// document below:
//   <!doctype html><title>Island</title><meta name="color-scheme" content="light dark">
//   <style>:root{--bg:#fff;--fg:#0a0a0a;--fg2:#666;--line:#e5e5e5}
//     @media(prefers-color-scheme:dark){:root{--bg:#111;--fg:#ededed;--fg2:#a1a1a1;--line:#2a2a2a}}
//     body{display:flex;align-items:center;justify-content:center;background:var(--bg);...}
//     h1{font-size:clamp(32px,4vw,48px);font-weight:600;letter-spacing:-.03em} ...</style>
//   <main><p class="label">Island</p><h1>Island Browser</h1>
//     <p class="lede">Enter an address in the sidebar to begin.</p>
//     <ul><li>New tab<kbd>Cmd/Ctrl T</kbd></li>...</ul></main>
inline constexpr std::string_view kProductionInitialUrl =
    "data:text/html;charset=utf-8,"
    "%3C%21doctype%20html%3E%3Ctitle%3EIsland%3C%2Ftitle%3E%3Cmeta%20name%3D%22color-scheme"
    "%22%20content%3D%22light%20dark%22%3E%3Cstyle%3E%3Aroot%7B--bg%3A%23fff%3B--fg%3A%230a0a"
    "0a%3B--fg2%3A%23666%3B--line%3A%23e5e5e5%7D%40media%28prefers-color-scheme%3Adark%29%7B"
    "%3Aroot%7B--bg%3A%23111%3B--fg%3A%23ededed%3B--fg2%3A%23a1a1a1%3B--line%3A%232a2a2a%7D"
    "%7D%2A%7Bbox-sizing%3Aborder-box%7Dhtml%2Cbody%7Bheight%3A100%25%3Bmargin%3A0%7Dbody%7Bd"
    "isplay%3Aflex%3Balign-items%3Acenter%3Bjustify-content%3Acenter%3Bbackground%3Avar%28--b"
    "g%29%3Bcolor%3Avar%28--fg%29%3Bfont%3A15px%2F1.55%20Geist%2Cui-sans-serif%2C-apple-syste"
    "m%2CBlinkMacSystemFont%2C%22Segoe%20UI%22%2Csans-serif%3B-webkit-font-smoothing%3Aantial"
    "iased%7Dmain%7Bwidth%3Amin%28440px%2Ccalc%28100%25%20-%2048px%29%29%7D.label%7Bmargin%3A"
    "0%200%2012px%3Bcolor%3Avar%28--fg2%29%3Bfont%3A500%2011px%2F1.4%20%22Geist%20Mono%22%2Cu"
    "i-monospace%2CSFMono-Regular%2CMenlo%2Cmonospace%3Bletter-spacing%3A.08em%3Btext-transfo"
    "rm%3Auppercase%7Dh1%7Bmargin%3A0%200%2012px%3Bfont-size%3Aclamp%2832px%2C4vw%2C48px%29"
    "%3Bfont-weight%3A600%3Bline-height%3A1.05%3Bletter-spacing%3A-.03em%7D.lede%7Bmargin%3A0"
    "%200%2024px%3Bcolor%3Avar%28--fg2%29%7Dul%7Bmargin%3A0%3Bpadding%3A0%3Blist-style%3Anone"
    "%3Bborder-top%3A1px%20solid%20var%28--line%29%7Dli%7Bdisplay%3Aflex%3Balign-items%3Acent"
    "er%3Bjustify-content%3Aspace-between%3Bpadding%3A10px%200%3Bborder-bottom%3A1px%20solid"
    "%20var%28--line%29%3Bfont-size%3A14px%7Dkbd%7Bpadding%3A1px%206px%3Bborder%3A1px%20solid"
    "%20var%28--line%29%3Bborder-bottom-width%3A2px%3Bborder-radius%3A6px%3Bcolor%3Avar%28--f"
    "g2%29%3Bfont%3A500%2012px%2F1.4%20%22Geist%20Mono%22%2Cui-monospace%2CSFMono-Regular%2CM"
    "enlo%2Cmonospace%7D%3C%2Fstyle%3E%3Cmain%3E%3Cp%20class%3D%22label%22%3EIsland%3C%2Fp%3E"
    "%3Ch1%3EIsland%20Browser%3C%2Fh1%3E%3Cp%20class%3D%22lede%22%3EEnter%20an%20address%20in"
    "%20the%20sidebar%20to%20begin.%3C%2Fp%3E%3Cul%3E%3Cli%3ENew%20tab%3Ckbd%3ECmd%2FCtrl%20T"
    "%3C%2Fkbd%3E%3C%2Fli%3E%3Cli%3EFocus%20address%3Ckbd%3ECmd%2FCtrl%20L%3C%2Fkbd%3E%3C%2Fl"
    "i%3E%3Cli%3ECommand%20palette%3Ckbd%3ECmd%2FCtrl%20K%3C%2Fkbd%3E%3C%2Fli%3E%3C%2Ful%3E"
    "%3C%2Fmain%3E";

inline constexpr std::string_view kSmokeTestInitialUrl =
    "data:text/html;charset=utf-8,%3C%21doctype%20html%3E%3Ctitle%3EIsland%20Smoke%20"
    "Test%3C%2Ftitle%3E"
    "%3Ch1%20id%3D%22island-phase1-smoke-ok%22%3EISLAND_PHASE1_SMOKE_OK%3C%2Fh1%3E"
    "%3Ca%20href%3D%22%23island-phase1-smoke-history%22%3EAdvance%20smoke%20history%3C%2Fa%3E"
    "%3Cp%20id%3D%22island-phase1-smoke-history%22%3ESmoke%20history%20entry%3C%2Fp%3E";

class StartupOptions final {
  public:
    [[nodiscard]] static StartupOptions Parse(std::span<const std::string_view> arguments);

    [[nodiscard]] constexpr std::string_view initial_url() const {
        return smoke_test_ ? kSmokeTestInitialUrl : kProductionInitialUrl;
    }

    [[nodiscard]] constexpr bool is_smoke_test() const { return smoke_test_; }

  private:
    explicit constexpr StartupOptions(bool smoke_test) : smoke_test_(smoke_test) {}

    bool smoke_test_;
};

}  // namespace island

#endif
