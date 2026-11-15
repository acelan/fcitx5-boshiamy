# fcitx5-boshiamy

[English](README.md) | 正體中文

專為 **Fcitx5** 打造的獨立 C++ 嘸蝦米輸入法引擎，用來取代以靜態表格實作的 `fcitx5-table-boshiamy`，在不膨脹碼表體積與查詢速度的前提下，原生支援官方嘸蝦米常見的快打與查詢特色。

## 為什麼要重寫成獨立引擎？

傳統靜態表格模式（`fcitx5-table-boshiamy`）無法實作具狀態或動態運算的功能；若將新酷音同音字直接以笛卡爾積預先展開進靜態碼表，會使碼表檔案暴增、載入與查詢變慢，也依然無法實作「快打模式」等互動提示。

`fcitx5-boshiamy` 將純淨的嘸蝦米碼表（`data/boshiamy.txt`，約 334 KB）與注音同音字表（`data/chewing_word.csv`，約 391 KB）分開載入，所有索引建置、同音查詢、萬用碼比對、繁簡轉換與最短碼提示皆於 C++ 引擎內即時完成。

## 功能特色

- **快打模式（`,,sp`）與嚴格快打模式**：
  - 輸入 `,,sp` + `空白鍵`（或透過 Fcitx5 狀態列按鈕 `boshiamy-speed-mode`）開關快打模式。
  - 若未使用最短碼出字（例如「對」打了 `faa` 而非 `a`），出字後會在提示列提醒最短碼（`💡「對」最短碼: a`）。
  - 計算最短碼時會優先依據該字作為「第 1 候選字」的最短字碼，不會對 `aaav`（`龘`）等選字補強碼產生誤報。
  - 可選擇開啟**嚴格快打模式（StrictSpeedMode）**，強制必須輸入最短碼才允許出字。
- **動態同音字查詢（`` ` `` 開頭）**：
  - 以反引號 `` ` `` 加上任意嘸蝦米字碼（例如 `` `ix ``），透過 `chewing_word.csv` 即時列出同音字，並在候選字旁標示該字的最短嘸蝦米拆碼（如 `業 [fr]`）。
  - 連續按兩次 `` `` `` 或 `` ` `` + `空白鍵` 可直接輸入半形反引號 `` ` ``。
- **繁／簡體輸出模式切換（`,,c` / `,,t`）**：
  - 支援以 `,,c` + `空白鍵` 切換為簡體中文輸出，`,,t` + `空白鍵` 切回繁體中文輸出。
  - 內建由 OpenCC（`tw2s`）產生的繁簡對照表（`data/t2s.txt`），切換簡體輸出時會自動為轉換後的候選字去重。
  - 引擎內部保留原始繁體字資訊，因此在簡體輸出模式下仍可正常使用快打最短碼提示與 `,,q` 反查拆碼。
- **萬用字元查碼（`?` 與 `*`）**：
  - 在輸入字碼途中加入 `?`（代表任意 1 碼）或 `*`（代表 0～多碼），例如 `f?a`、`a*q`，候選窗會列出所有符合的字並提示 `[符合字碼 | 最短碼]`。
- **`V` / `R` / `S` / `F` 選字鍵補強**：
  - 候選窗翻到**第 2 頁之後**時，直接按 `v`、`r`、`s`、`f` 可立即選取當前頁面的第 2～5 個候選字。
  - 在**同音字**、**萬用碼**或字表無後續碼的情況下，字碼末尾加上 `v`/`r`/`s`/`f`（例如 `` `frv ``、`aaa?v`）再按 `空白鍵`，可直接選取第 2～5 個候選字。
- **「加加詞庫」自訂詞庫與熱重載（`,,r`）**：
  - 自動載入 `~/.local/share/fcitx5/boshiamy/custom.txt`（若尚不存在會自動建立含格式範例的檔案）。
  - 支援中英文詞組、片語與自訂簡碼（如 `mail user@example.com`；字碼為 1-5 碼（可再加 v/r/s/f 選字）的 `a-z , . ' [ ]`），多候選自訂字碼會自動補齊 `+ v/r/s/f` 選字碼，編輯後輸入 `,,r` + `空白鍵` 即可即時重載，無須重啟 Fcitx5。
- **反查拆碼（`,,q`）與快打統計（`,,st`）**：
  - 輸入 `,,q` + `空白鍵` 可立即查詢剛才輸出文字的所有嘸蝦米拆碼（將第 1 候選字碼排在前面）。
  - 輸入 `,,st` + `空白鍵` 可顯示本次工作階段累積打字字數、最短碼命中率、平均碼長，以及最常未打最短碼的「待加強」字清單。

## 指令一覽表

| 輸入 | 功能 | 備註 |
| --- | --- | --- |
| `` ` `` + 字碼 | 同音字查詢模式 | 按 `` `` `` 或 `` ` `` + `空白鍵` 可輸入 `` ` `` |
| 字首 + `?` / `*` | 萬用碼查字（`?` = 1 碼，`*` = 0～多碼） | 例如 `f?a`、`a*q` |
| `,,sp` | 開關快打模式 | 亦可從 Fcitx5 狀態列選單切換 |
| `,,c` | 切換為簡體中文輸出 | 按數字鍵 `2` 仍可選取 `《` |
| `,,t` | 切換為繁體中文輸出 | 恢復預設繁體中文輸出 |
| `,,q` | 反查上一字的所有嘸蝦米拆碼 | 依首選碼／碼長排序顯示 |
| `,,st` | 顯示本次快打統計 | 顯示字數、最短碼率、均碼長與待加強字 |
| `,,r` | 即時重新載入自訂詞庫（`custom.txt`） | 按數字鍵 `2` 仍可選取 `〈` |
| `,,h` 或 `,,?` | 顯示簡易指令說明 | 顯示於提示列 |

## 設定選項

設定檔儲存於 `~/.config/fcitx5/conf/boshiamy.conf`（也可透過 Fcitx5 設定介面調整）：

- `SpeedMode`（預設 `False`）：快打模式，未用最短碼時顯示提示
- `StrictSpeedMode`（預設 `False`）：嚴格快打模式，強制必須打最短碼才能出字
- `SimplifiedMode`（預設 `False`）：簡體中文輸出模式（`,,c` / `,,t`）
- `ExactMatch`（預設 `False`）：僅顯示完全符合的候選字，不做字首補全
- `ShowHomophoneCode`（預設 `True`）：同音字候選旁顯示 `[最短碼]`
- `PageSize`（預設 `9`）：每頁候選字數量（`3`～`10`）
- `PrevPage`（預設 `Up`, `-`, `Page_Up`）：上一頁按鍵
- `NextPage`（預設 `Down`, `=`, `Page_Down`）：下一頁按鍵

## 編譯、測試與打包安裝

### 相依套件（Debian / Ubuntu）

```bash
sudo apt-get install cmake extra-cmake-modules pkgconf gettext \
  libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev
```

### 編譯與執行單元測試

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### 建立與安裝 Debian 套件

本套件設定為與 `fcitx5-table-boshiamy`（及 `fcitx-table-boshiamy`）互斥並取代：

```bash
dpkg-buildpackage -us -uc -b
sudo apt-get install -y ../fcitx5-boshiamy_1.0.0_amd64.deb
```

安裝完成後重新啟動 Fcitx5（執行 `fcitx5 -r` 或重新登入），於輸入法設定中加入 **嘸蝦米（Boshiamy）** 即可使用。

## 授權條款

- 引擎程式碼與打包設定：`LGPL-2.1-or-later`
- `data/boshiamy.txt`：公有領域（public domain），取自 [fcitx5-table-extra](https://github.com/fcitx/fcitx5-table-extra) 的 `tables/boshiamy.txt`（未修改）
- `icons/`：`GPL-3.0-or-later`，取自 fcitx5-table-extra
- `data/chewing_word.csv`：`LGPL-2.1-or-later`（libchewing Core Team）
- `data/t2s.txt`：`Apache-2.0`，由 [OpenCC](https://github.com/BYVoid/OpenCC) `tw2s` 產生
