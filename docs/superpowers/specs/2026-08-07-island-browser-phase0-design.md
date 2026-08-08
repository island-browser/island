# Island Browser — дизайн-документ (общий контекст + Фаза 0)

> Исторический документ. Текущий реализованный браузерный каркас описан в
> `docs/superpowers/specs/2026-08-08-island-browser-phase1-design.md` и
> `docs/superpowers/plans/2026-08-08-island-browser-phase1.md`.

## Контекст

Island — кроссплатформенный (Windows/macOS/Linux) десктопный браузер с открытым
исходным кодом, построенный на CEF (Chromium Embedded Framework) и нативном
UI-тулките `cef_views`, без Electron/Node.js. Полная функциональная
спецификация (Фазы 0–9: setup, голый браузинг, свой chrome, мульти-таб,
spaces + персистентность, command bar, split view + hibernation, settings,
расширения, агентная интеграция) была предоставлена пользователем целиком и
хранится как источник правды в истории задачи. Каждая фаза получает
собственный цикл спека → план → реализация; этот документ фиксирует решения
инфраструктурного уровня, принятые в ходе брейнштормa, и детальный дизайн
**Фазы 0 — Setup**.

Разработка ведётся в первую очередь на macOS (Darwin), с прицелом на
кроссплатформенность в CMake с самого начала (условная логика по платформам,
а не macOS-only код).

## Решения по зависимостям (действуют для всех последующих фаз)

> Примечание после реализации (2026-08-08): исторический дизайн ниже описывал
> начальный замысел. Реализованный Phase 0 использует manifest-driven lock-файл
> `deps/dependencies.lock.json`: CEF `150.0.14+g7c1aa68+chromium-150.0.7871.129`
> для шести desktop targets и Geist `1.7.2` по immutable commit archive. Это
> заменяет устаревшие детали про CEF 131 и mutable Geist `main`, не меняя
> исходный смысл решения — бинарные зависимости не коммитятся и ставятся через
> `scripts/setup_deps.sh`.

- **CEF binary distribution**: у CEF нет официального git-репозитория с
  бинарниками — только S3-архивы на `cef-builds.spotifycdn.com`. Официальный
  подход через CMake FetchContent/ExternalProject или git submodule на
  внешний vendor-репозиторий были отклонены (первый требует интернet при
  каждой чистой конфигурации и усложняет офлайн-сборку; второй требует
  стороннего репозитория с Git LFS, который никто ещё не завёл). Итоговое
  решение: **shell-скрипт `scripts/setup_deps.sh`**, который пользователь
  запускает один раз вручную перед первой сборкой. Скрипт скачивает архив
  нужной CEF-версии под текущую платформу с `cef-builds.spotifycdn.com`,
  распаковывает его в `third_party/cef/`. Эта директория в `.gitignore`.
  `CMakeLists.txt` ищет CEF в `third_party/cef`, и если её там нет — падает
  с понятным сообщением "запустите scripts/setup_deps.sh".
- **Шрифты Geist / Geist Mono**: та же логика — не коммитим бинарные файлы
  шрифтов в репозиторий, а тянем их тем же `setup_deps.sh` (архив с
  `github.com/vercel/geist-font`, releases) в `assets/fonts/`, тоже в
  `.gitignore`.
- **SQLite**: понадобится с Фазы 4; в Фазе 0 не подключается. Способ
  подключения (amalgamation vendored в репо, т.к. это два файла `sqlite3.c`/
  `sqlite3.h` и лицензия public domain — без проблем размера) будет решён
  отдельно перед Фазой 4.
- **GoogleTest**: подключается через CMake `FetchContent` (сам GoogleTest —
  маленький, чисто исходный код, тянуть его из интернета при конфигурации
  приемлемо и не противоречит решению про CEF/шрифты, которое было продиктовано
  именно размером бинарных дистрибутивов).

## Стиль кода и форматирование

- `.clang-format` фиксируется в Фазе 0, до появления кода. Базируется на
  Google C++ Style с отступом 4 пробела, `PointerAlignment: Left`,
  `ColumnLimit: 100`. Явная квалификация `std::` везде, `using namespace std;`
  запрещён (проверяется код-ревью, не линтером на этом этапе).
- C++20, современный CMake (`target_*` команды, никаких global
  `include_directories`).

## Структура репозитория (актуальна для Фазы 0, будет расти по мере фаз)

```
my-browser/
  CMakeLists.txt              # корневой, подключает src/ и tests/
  .clang-format
  .gitignore                  # third_party/cef, assets/fonts, build/
  scripts/
    setup_deps.sh             # качает CEF binary distribution + Geist/Geist Mono
  third_party/
    cef/                      # НЕ в git, наполняется setup_deps.sh
  assets/
    fonts/                    # НЕ в git, наполняется setup_deps.sh
  src/
    main/
      CMakeLists.txt
      main.cpp                # точка входа: CefApp, CefMainArgs,
                               # CefInitialize/CefRunMessageLoop/CefShutdown
  tests/
    CMakeLists.txt            # FetchContent(GoogleTest), одна цель island_tests
    smoke_test.cpp            # один тривиальный TEST(Smoke, Placeholder)
```

Дальнейшие фазы будут добавлять поддиректории в `src/` (`src/browser/`,
`src/ui/`, `src/spaces/`, `src/persistence/` и т.д.) — по одному
модулю/подсистеме на директорию, минимальная связанность, как того требует
спецификация.

## Фаза 0 — Setup: цели и критерий готовности

Цель: пустой CEF `cef_views` каркас, который открывается и закрывается без
крашей на macOS, плюс рабочий, но пока пустой тестовый таргет.

Объём работ:
1. `.clang-format`, `.gitignore`.
2. `scripts/setup_deps.sh` (bash) с определением ОС/архитектуры, скачиванием
   правильного архива CEF под macOS (arm64/x64) с `cef-builds.spotifycdn.com`
   и Geist/Geist Mono с GitHub releases, распаковкой в `third_party/cef` и
   `assets/fonts`.
3. Корневой `CMakeLists.txt` + `src/main/CMakeLists.txt`: минимальный
   `CefApp`/`CefClient`, `main()` с `CefExecuteProcess`/`CefInitialize`, окно
   через `cef_views` (`CefWindowDelegate` + пустой `CefBrowserView` или просто
   пустой `views::Window`, без адресной строки/навигации — это Фаза 1),
   `CefRunMessageLoop`, `CefShutdown`. Флаг `remote_debugging_port` уже
   выставляется в `CefSettings` (задел под Фазу 9, ничего сверх этого делать
   не нужно).
4. `tests/CMakeLists.txt` с `FetchContent(googletest)`, один
   `TEST(Smoke, Placeholder) { EXPECT_TRUE(true); }`, таргет собирается и
   проходит через `ctest`.

Критерий готовности Фазы 0:
- `cmake -B build && cmake --build build` собирается без ошибок на macOS
  после запуска `scripts/setup_deps.sh` (зависимости на месте).
- Бинарник запускается, показывает пустое `cef_views`-окно, закрывается через
  штатный close без крашей и без утечек процессов CEF (helper-процессы
  корректно завершаются).
- `ctest` в `build/` проходит (1 тест).

Явно вне скоупа Фазы 0 (переносится в следующие фазы): адресная строка,
навигация, сайдбар, дизайн-система (цвета/отступы/шрифты в UI), spaces,
персистентность, command bar.

## Тестирование

Юнит-тесты — GoogleTest, подключается в Фазе 0 как пустой скелет. Реальные
тесты бизнес-логики (Space/Tab модели, SQLite-слой) появятся с Фазы 4, когда
эта логика будет написана. UI/`cef_views` компоненты и сама интеграция с CEF
тестируются вручную запуском приложения (см. критерий готовности выше), а не
юнит-тестами — это стандартная практика для CEF-приложений, GUI-слой плохо
поддаётся автоматическому тестированию без headless-инфраструктуры, которую
в этот проект сейчас закладывать не нужно (YAGNI).
