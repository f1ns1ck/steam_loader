# steam_loader

Переключатель аккаунтов Steam с запуском программы после входа. Показывает аккаунты, которыми заходили на этом компьютере, и блокировки Deadlock на них (бан поиска, Low Priority, бан чата — из кеша игрового координатора `game/citadel/cache_<id>.soc`), переключает Steam на выбранный аккаунт и запускает выбранную программу.

Переключение как у TcNo Account Switcher: Steam закрывается, в автологин пишется выбранный аккаунт, Steam запускается снова. Работает для аккаунтов, вход в которые Steam запомнил (галка «Запомнить меня»). **Паролей и токенов программа не читает и не хранит.**

Три части:
- **`AccountPanelCore`** — библиотека без интерфейса (`core/`): список аккаунтов, поиск, переключение, разбор кеша. Кроссплатформенная (Windows и Linux).
- **`account-panel`** — командная строка поверх библиотеки.
- **`AccountPanel.exe`** — окно (ImGui + Direct3D 11), только Windows.

## Сборка

### Windows (MSVC, Visual Studio 2022/2026 с C++20)

```powershell
powershell -File build.ps1            # библиотека, account-panel.exe и окно AccountPanel.exe
powershell -File build.ps1 -NoGui     # без окна (только библиотека и консоль)
```

Результат в `out\`. ImGui, stb и freetype лежат в `third_party\` — ставить ничего не нужно. Путь к `vcvars64.bat` при необходимости поправь в начале `build.ps1`.

### Linux (g++ 9+ или clang++ 10+, C++20) — библиотека и консоль

```sh
./build.sh                 # out/libAccountPanelCore.a, out/libAccountPanelCore.so, out/account-panel
CXX=clang++ ./build.sh
```

или через CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ~/.local   # bin/account-panel, lib/, include/account_panel/
# -DBUILD_SHARED_LIBS=ON — собрать как .so/.dll
```

Окно под Linux не собирается (оно на Direct3D 11).

## Командная строка

```
account-panel <команда> [опции]

  -l, --list               список аккаунтов (* — в сети)
  -i, --info <аккаунт>     всё об аккаунте: Steam, блокировки, статистика
      --status             Steam, папка Deadlock, файлы, команда программы
  -s, --switch <аккаунт>   закрыть Steam и войти в аккаунт
  -p, --play [аккаунт]     войти и запустить Deadlock
  -r, --run [аккаунт]      войти (Steam без окна), закрыть Steam и запустить программу из --command
      --login <аккаунт>    только войти в аккаунт (Steam без окна) и закрыть Steam
      --hide/--unhide      убрать/вернуть аккаунт в списке панели
      --game-dir [папка]   показать или задать папку Deadlock
      --command [строка]   показать или задать команду программы после входа
  -h, --help / -v, --version

Опции: --all, --json, -y (не спрашивать), --wait <сек>, --keep-steam, --config <папка>
```

Аккаунт — логин, ник, account id, SteamID64, `[U:1:id]`, `#N` из `--list` или начало логина/ника.

Примеры:

```sh
account-panel --list
account-panel --switch my_login
account-panel --login smurf && ./loader.exe
account-panel --run smurf --command "./loader.exe --silent"
```

### Вход и запуск программы

`--run`, `--login` и кнопка «Запустить» в окне: Steam закрывается → в автологин пишется выбранный аккаунт → Steam стартует без окна (`-silent`) → как только вошёл, Steam закрывается снова (сохранив вход) → запускается программа. Чтобы Steam остался в трее (программе нужен работающий Steam API) — флаг `--keep-steam`.

Авторизоваться в аккаунт без запуска Steam нельзя: сессию создаёт именно вход. Если Steam не помнит вход (истёк токен, Steam Guard), появится окно входа Steam.

## Откуда данные

- Аккаунты: `Steam/config/loginusers.vdf` (логины, ники, SteamID; без паролей).
- Текущий аккаунт и автологин: `HKCU\Software\Valve\Steam` (Windows) или `~/.steam/registry.vdf` (Linux).
- Блокировки и статистика Deadlock: `game/citadel/cache_<account id>.soc` (обновляется при входе аккаунта в игру).
- Файлы панели (`account_panel*.txt`): рядом с программой, на Linux — в `~/.config/account-panel`.

Окну нужны шрифты и портреты героев в папке `fonts/` и `heroes/` рядом с `.exe`; без них — системный шрифт и инициалы вместо аватаров.

## Linux

Поддержаны обычная установка, Flatpak и Snap. Переключение правит `registry.vdf`, пока Steam закрыт (с копией `registry.vdf.bak`). Процессы ищутся по `/proc` (`steam`, `deadlock.exe` под Proton). **Linux-ветки собраны, но вживую со Steam не проверялись** — проверяйте на своей системе.

## Лицензии

Код в `core/`, `cli/`, `main.cpp`, `Style.hpp` — автора репозитория. В `third_party/`:
- Dear ImGui — MIT
- stb_image — public domain / MIT
- FreeType (`third_party/freetype.lib` и заголовки) — FreeType License (BSD-подобная)
