# fcitx5-boshiamy

English | [正體中文](README.zh-TW.md)

A standalone C++ Boshiamy (嘸蝦米) input method engine for **Fcitx5**, designed to replace static table-based implementations (`fcitx5-table-boshiamy`) with native interactive features without static table bloat.

## Why a Standalone Engine?

Traditional table-based Fcitx5 Boshiamy (`fcitx5-table-boshiamy`) cannot implement stateful or dynamic features cleanly. Adding homophone entries by pre-expanding a static table blows up dictionary size and slows down loading/lookup, while interactive features such as Speed Mode shortest-code coaching cannot be expressed in a static table at all.

`fcitx5-boshiamy` keeps the clean Boshiamy table (`data/boshiamy.txt`, ~334 KB) and Chewing pronunciation table (`data/chewing_word.csv`, ~391 KB) separate and performs indexing, hints, lookups, and conversions dynamically in C++.

## Features

- **Speed Mode (`,,sp`) & Strict Speed Mode**:
  - Toggle with `,,sp` + `Space` (or via the Fcitx5 status bar action `boshiamy-speed-mode`).
  - When committing a character with a non-shortest code (e.g., `faa` instead of `a` for `對`), displays its shortest direct code (`💡「對」最短碼: a`).
  - Computes shortest codes accurately using each character's shortest *first-candidate* code (avoiding false warnings for `v`/`r`/`s`/`f` disambiguation codes such as `aaav` for `龘`).
  - Optional **Strict Speed Mode** blocks non-shortest codes until the shortest code is typed.
- **Dynamic Homophone Lookup (`` ` `` prefix)**:
  - Prefix any Boshiamy code with `` ` `` (e.g., `` `ix ``) to look up homophones dynamically via `chewing_word.csv`, showing each candidate's shortest Boshiamy code beside it (`業 [fr]`).
  - Press `` `` `` or `` ` `` + `Space` to commit a literal backtick `` ` ``.
- **Simplified / Traditional Output Mode (`,,c` / `,,t`)**:
  - Switch between Simplified Chinese (`,,c` + `Space`) and Traditional Chinese (`,,t` + `Space`) output on the fly using the OpenCC `tw2s`-derived mapping table (`data/t2s.txt`), with automatic candidate deduplication.
  - Preserves underlying Traditional characters so shortest-code checking and `,,q` reverse lookup continue to work accurately even in Simplified mode.
- **Wildcard Code Lookup (`?` and `*`)**:
  - Use `?` (matches any single key) or `*` (matches zero or more keys) mid-code (e.g., `f?a`, `a*q`) to search matching characters and display `[matched_code | shortest_code]`.
- **Native `V` / `R` / `S` / `F` Selection Enhancement**:
  - On **Page 2+**, pressing `v`, `r`, `s`, or `f` immediately selects the 2nd–5th candidate on the current page.
  - In **homophone**, **wildcard**, and **standard** lookups (when the suffixed code has no direct dictionary entry), appending `v`/`r`/`s`/`f` (e.g., `` `frv ``, `aaa?v`) selects the 2nd–5th candidate via `Space`.
- **Custom Dictionary ("加加詞庫") & Hot Reload (`,,r`)**:
  - Reads user phrases and custom codes from `~/.local/share/fcitx5/boshiamy/custom.txt` (starter template created automatically on first run/reload).
  - Supports multi-word/ASCII phrases (e.g., `mail user@example.com`; codes are 1-5 keys (plus an optional v/r/s/f selection key) of `a-z , . ' [ ]`), auto-synthesizes `+ v/r/s/f` selection codes for multi-candidate custom entries, and reloads live with `,,r` + `Space` without restarting Fcitx5.
- **Reverse Code Lookup (`,,q`) & Session Statistics (`,,st`)**:
  - `,,q` + `Space` lists all Boshiamy codes (primary first-candidate codes sorted first) for the last committed character.
  - `,,st` + `Space` shows typed character count, shortest-code hit rate, average code length, and top words to practice (`待加強`).

## Commands Reference

| Input | Action | Notes |
| --- | --- | --- |
| `` ` `` + *code* | Dynamic homophone lookup | `` `` `` or `` ` `` + `Space` outputs `` ` `` |
| *prefix* + `?` / `*` | Wildcard code lookup (`?` = 1 char, `*` = 0+ chars) | e.g., `f?a`, `a*q` |
| `,,sp` | Toggle Speed Mode | Also available in the Fcitx5 status menu |
| `,,c` | Switch to Simplified Chinese output | Candidate `2` still selects `《` |
| `,,t` | Switch to Traditional Chinese output | Restores Traditional Chinese output |
| `,,q` | Reverse-lookup codes for last committed character | Lists shortest & full codes |
| `,,st` | Show session Speed Mode statistics | Shows count, hit rate, avg length, practice words |
| `,,r` | Hot-reload custom dictionary (`custom.txt`) | Candidate `2` still selects `〈` |
| `,,h` or `,,?` | Display quick command reference | Shown in auxiliary bar |

## Configuration

Settings are stored in `~/.config/fcitx5/conf/boshiamy.conf` (and editable via the Fcitx5 configuration tool):

- `SpeedMode` (`False`): Show shortest-code hint after committing a non-shortest code
- `StrictSpeedMode` (`False`): Block committing characters typed with non-shortest codes
- `SimplifiedMode` (`False`): Convert output to Simplified Chinese (`,,c` / `,,t`)
- `ExactMatch` (`False`): Show exact code matches only without prefix completion
- `ShowHomophoneCode` (`True`): Show `[shortest_code]` comments in homophone candidates
- `PageSize` (`9`): Number of candidates per page (`3`–`10`)
- `PrevPage` (`Up`, `-`, `Page_Up`): Previous candidate page
- `NextPage` (`Down`, `=`, `Page_Down`): Next candidate page

## Building & Testing

### Dependencies (Debian / Ubuntu)

```bash
sudo apt-get install cmake extra-cmake-modules pkgconf gettext \
  libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev
```

### Build & Run Unit Tests

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Build & Install Debian Package

`fcitx5-boshiamy` conflicts with and replaces `fcitx5-table-boshiamy` (and `fcitx-table-boshiamy`):

```bash
dpkg-buildpackage -us -uc -b
sudo apt-get install -y ../fcitx5-boshiamy_1.0.0_amd64.deb
```

Restart Fcitx5 (`fcitx5 -r`) after installation and add **Boshiamy (嘸蝦米)** in Fcitx5 configuration.

## License

- Engine source code & packaging: `LGPL-2.1-or-later`
- `data/boshiamy.txt`: public domain, derived from [fcitx5-table-extra](https://github.com/fcitx/fcitx5-table-extra) `tables/boshiamy.txt` (unmodified)
- `icons/`: `GPL-3.0-or-later`, copied from fcitx5-table-extra
- `data/chewing_word.csv`: `LGPL-2.1-or-later` (libchewing Core Team)
- `data/t2s.txt`: `Apache-2.0`, generated from [OpenCC](https://github.com/BYVoid/OpenCC) `tw2s`
