# Island Browser — дизайн-документ Фазы 1

## Контекст

Фаза 1 продолжает реализованную Фазу 0. Исторические документы Фазы 0 остаются в
`docs/superpowers/specs/2026-08-07-island-browser-phase0-design.md` и
`docs/superpowers/plans/2026-08-07-island-browser-phase0.md`; текущим источником правды для
реализованного браузерного каркаса являются этот документ и план Фазы 1.

Island остаётся C++20 desktop-приложением на CEF и нативном `cef_views`, без Electron/Node.js и без
web-based shell. Фаза 1 добавляет минимальный браузерный view, но не добавляет продуктовый chrome.

## Принятое архитектурное решение

- Верхний уровень UI — один `CefWindow` с одним `CefBrowserView`.
- Начальная production-страница и smoke-страница — детерминированные локальные
  `data:text/html;charset=utf-8` документы. Они не содержат `http`, `<script>`, внешних `src`,
  `<link>` или CSS `url(...)`, поэтому старт приложения не должен делать внешние запросы.
- `--island-smoke-test` выбирает smoke-страницу с title `Island Smoke Test`, DOM-маркером
  `ISLAND_PHASE1_SMOKE_OK` и локальной history-ссылкой. Без флага открывается production-страница
  `Island Browser`.
- Общие швы вынесены в `BrowserWindow`, `IslandApp`, runtime-функцию запуска CEF и
  `NavigationState`/`NavigationSnapshot`. Это фиксирует состояние загрузки, URL, заголовок,
  доступность back/forward, HTTP status/network error и ревизию изменения без привязки тестов к CEF
  UI.
- Команды браузера ограничены `Back`, `Forward`, `Reload`. На macOS они доступны через меню Browser
  и клавиши Cmd+[ / Cmd+] / Cmd+R; CEF accelerators также обрабатывают back/forward/reload.
- `OnBeforePopup` всегда возвращает блокировку: новые окна и popups в Фазе 1 не создаются.
- Закрытие идёт через CEF lifecycle: window close вызывает `TryCloseBrowser()`, browser destroy
  очищает ссылки, navigation snapshot переходит в `Closed`, затем вызывается `CefQuitMessageLoop()` и
  после выхода — `CefShutdown()`.

## Платформенный bootstrap и sandbox

- macOS использует `main_mac.mm`, `CefScopedLibraryLoader`, нативное меню Quit/Back/Forward/Reload и
  bundle/helper layout, совместимый с vendored CEF distribution.
- Windows имеет отдельный `windows/main_win.cc`. Матрица сборки покрывает два режима:
  `USE_SANDBOX=OFF` и `USE_SANDBOX=ON`; sandbox-on использует bootstrap/client DLL layout.
- Linux имеет отдельный native entry point и CMake platform integration. Как и Windows/macOS x64,
  Linux targets не считаются подтверждёнными без native CI evidence.
- `CefSettings.remote_debugging_port` остаётся `9222`. Дополнительный CDP/agentic слой не реализован.

## CMake, зависимости и packaging

- `scripts/setup_deps.sh` вендорит CEF 150 в `third_party/cef/` и Geist fonts в `assets/fonts/` из
  `deps/dependencies.lock.json`; оба результата остаются вне git.
- Первый `cmake -B build -S .` требует network access для GoogleTest v1.15.2 через CMake
  `FetchContent`. CEF и fonts к этому моменту уже должны быть установлены setup-скриптом.
- Core-логика Фазы 1 собирается в `island_browser_core`; platform-specific executable targets
  подключают macOS, Windows или Linux bootstrap.
- GoogleTest discovery остаётся через `gtest_discover_tests`.
- Package tooling сохраняет unsigned candidate contract, проверяет runtime layout, metadata,
  checksum, macOS framework/helper bundles, Windows sandbox client DLL consistency and architecture,
  symlink safety and target architecture conflicts. Реальная совместимость с Phase 0 package layout
  подтверждена.

## Проверка и acceptance evidence

Команды clean-checkout verification:

```bash
bash -n scripts/setup_deps.sh
./scripts/setup_deps.sh --dry-run
./scripts/setup_deps.sh
python3 scripts/deps.py verify
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
open build/src/main/island_browser.app
```

Smoke-запуск на macOS:

```bash
open build/src/main/island_browser.app --args --island-smoke-test
```

После ручного выхода из приложения:

```bash
pgrep -fl island_browser || true
```

Принятая evidence для Фазы 1:

- macOS arm64 clean build проходит.
- `ctest --test-dir build --output-on-failure`: 18/18 tests passed.
- `python3 scripts/deps.py verify` проходит.
- Runtime показывает одно окно, одну страницу, корректный title и smoke marker.
- Startup остаётся data-only: внешних startup requests нет.
- Popup creation blocked.
- History back/forward работает на smoke history entry.
- Native close и menu Quit очищают процессы после выхода.
- Package tests: 11 passed.
- Реальная совместимость с Phase 0 package contract подтверждена.
- Cmd+R дал decisive signal; physical Cmd+Q остаётся manual caveat.

## Явно вне скоупа Фазы 1 / Phase 2+

Фаза 1 не реализует и не должна документироваться как реализующая: адресную строку, browser chrome,
таб-стрип, multiple tabs/windows, spaces, persistence, command bar, settings UI, extensions,
split view, hibernation, sync/profile features, stable release signing/notarization или расширенную
CDP/agentic integration beyond port `9222`.

macOS x64 и Windows/Linux targets требуют native CI evidence перед claims о поддержке. Stable public
release остаётся заблокированным до signing/notarization verification.
