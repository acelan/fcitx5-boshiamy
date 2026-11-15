/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "testdir.h"
#include "testfrontend_public.h"
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace fcitx;

namespace {

void sendKeys(AddonInstance *testfrontend, const ICUUID &uuid,
              const std::vector<Key> &keys) {
    for (const auto &key : keys) {
        FCITX_ASSERT(
            testfrontend->call<ITestFrontend::sendKeyEvent>(uuid, key, false));
    }
}

void sendKeyStrings(AddonInstance *testfrontend, const ICUUID &uuid,
                    const std::vector<const char *> &keys) {
    for (const char *keyStr : keys) {
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(keyStr), false));
    }
}

void setupDefaultGroup(Instance *instance) {
    auto defaultGroup = instance->inputMethodManager().currentGroup();
    defaultGroup.inputMethodList().clear();
    defaultGroup.inputMethodList().push_back(
        InputMethodGroupItem("keyboard-us"));
    defaultGroup.inputMethodList().push_back(
        InputMethodGroupItem("boshiamy"));
    defaultGroup.setDefaultInputMethod("");
    instance->inputMethodManager().setGroup(defaultGroup);
}

void testStandardTyping(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *boshiamy = instance->addonManager().addon("boshiamy", true);
        FCITX_ASSERT(boshiamy);
        setupDefaultGroup(instance);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "boshiamy");

        // a + Space -> 對
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("a"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "a");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("對");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        // ix + Space -> 我
        sendKeyStrings(testfrontend, uuid, {"i", "x"});
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ix");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("我");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        // ix + 2 -> 曳
        sendKeyStrings(testfrontend, uuid, {"i", "x"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>("曳");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("2"), false));

        // , + Space -> ，
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("comma"), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>("，");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        // BackSpace & Escape editing
        sendKeyStrings(testfrontend, uuid, {"i", "x"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "i");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_Escape), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

void testHomophoneMode(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "boshiamy");

        // `ix -> 我, 婐, 捰, 婑, 業, 葉, 頁, 夜, ...
        sendKeyStrings(testfrontend, uuid, {"grave", "i", "x"});
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "`ix");

        auto candList = ic->inputPanel().candidateList();
        FCITX_ASSERT(candList);
        FCITX_ASSERT(candList->size() >= 8);

        const std::vector<std::string> expected = {
            "我", "婐", "捰", "婑", "業", "葉", "頁", "夜"};
        for (size_t i = 0; i < expected.size(); ++i) {
            FCITX_ASSERT(candList->candidate(static_cast<int>(i))
                             .text()
                             .toString() == expected[i]);
        }
        FCITX_ASSERT(candList->candidate(4).comment().toString() == "[fr]");

        // Test paging forward and back
        auto *pageable = candList->toPageable();
        FCITX_ASSERT(pageable && pageable->hasNext());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("equal"), false));
        FCITX_ASSERT(
            ic->inputPanel().candidateList()->toPageable()->currentPage() == 1);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("minus"), false));
        FCITX_ASSERT(
            ic->inputPanel().candidateList()->toPageable()->currentPage() == 0);

        // Select 5th candidate ("業") via key "5"
        testfrontend->call<ITestFrontend::pushCommitExpectation>("業");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("5"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        // ` + Space -> `
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("grave"), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>("`");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

void testSpeedMode(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *boshiamy = instance->addonManager().addon("boshiamy", true);
        FCITX_ASSERT(boshiamy);
        RawConfig resetConfig;
        resetConfig.setValueByPath("SpeedMode", "False");
        resetConfig.setValueByPath("StrictSpeedMode", "False");
        resetConfig.setValueByPath("SimplifiedMode", "False");
        boshiamy->setConfig(resetConfig);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "boshiamy");

        // Enable speed mode via ,,sp + Space (should not commit text)
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "s", "p"});
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == ",,sp");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().find(
                         "快打模式：開啟") != std::string::npos);

        // Typing faa + Space commits "對" and warns shortest code "a"
        sendKeyStrings(testfrontend, uuid, {"f", "a", "a"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>("對");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() ==
                     "💡「對」最短碼: a");

        // Typing shortest code a + Space commits "對" and clears warning
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("a"), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>("對");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());

        // Typing aaav + Space commits "龘" and must NOT warn about "aaa"
        sendKeyStrings(testfrontend, uuid, {"a", "a", "a", "v"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>("龘");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());

        // Strict speed mode blocks faa + Space without committing
        RawConfig strictConfig;
        strictConfig.setValueByPath("SpeedMode", "True");
        strictConfig.setValueByPath("StrictSpeedMode", "True");
        boshiamy->setConfig(strictConfig);
        sendKeyStrings(testfrontend, uuid, {"f", "a", "a"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() ==
                     "⛔「對」請打最短碼: a");

        // Disable speed mode via ,,sp + Space
        strictConfig.setValueByPath("StrictSpeedMode", "False");
        boshiamy->setConfig(strictConfig);
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "s", "p"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().find(
                         "快打模式：關閉") != std::string::npos);

        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

void testSimplifiedReverseAndStats(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *boshiamy = instance->addonManager().addon("boshiamy", true);
        FCITX_ASSERT(boshiamy);
        RawConfig resetConfig;
        resetConfig.setValueByPath("SimplifiedMode", "False");
        resetConfig.setValueByPath("SpeedMode", "False");
        resetConfig.setValueByPath("StrictSpeedMode", "False");
        boshiamy->setConfig(resetConfig);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "boshiamy");

        // ,,c + 2 should still commit dictionary punctuation 《
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "c"});
        auto candList = ic->inputPanel().candidateList();
        FCITX_ASSERT(candList && candList->size() >= 2);
        FCITX_ASSERT(candList->candidate(1).text().toString() == "《");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("《");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("2"), false));

        // ,,c + Space switches to Simplified mode
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "c"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().find("簡體中文") !=
                     std::string::npos);

        // a -> 对, qd -> 体 in Simplified mode
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("a"), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>("对");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        sendKeyStrings(testfrontend, uuid, {"q", "d"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>("体");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        // ,,q + Space reverse-looks up last committed char 体 (原: 體)
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "q"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        const std::string revSimp = ic->inputPanel().auxUp().toString();
        FCITX_ASSERT(revSimp.find("「体」(原: 體) 拆碼:") != std::string::npos);
        FCITX_ASSERT(revSimp.find("qd") != std::string::npos);

        // ,,t + Space switches back to Traditional mode
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "t"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().find("繁體中文") !=
                     std::string::npos);

        // qd -> 體 in Traditional mode
        sendKeyStrings(testfrontend, uuid, {"q", "d"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>("體");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        // ,,q + Space reverse-looks up 體
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "q"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        const std::string revTrad = ic->inputPanel().auxUp().toString();
        FCITX_ASSERT(revTrad.find("「體」拆碼:") != std::string::npos);
        FCITX_ASSERT(revTrad.find("qd") != std::string::npos);

        // Type non-shortest faa -> 對 then check ,,st statistics
        sendKeyStrings(testfrontend, uuid, {"f", "a", "a"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>("對");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "s", "t"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        const std::string statsMsg = ic->inputPanel().auxUp().toString();
        FCITX_ASSERT(statsMsg.find("📊 字數:") != std::string::npos);
        FCITX_ASSERT(statsMsg.find("最短碼率:") != std::string::npos);
        FCITX_ASSERT(statsMsg.find("對(a)") != std::string::npos);

        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

void testWildcardAndVrsfSelection(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "boshiamy");

        // Wildcard f?a (testing Shift+question just like a real X11/Wayland
        // keyboard)
        sendKeys(testfrontend, uuid,
                 {Key("f"), Key(FcitxKey_question, KeyState::Shift), Key("a")});
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "f?a");
        auto candList = ic->inputPanel().candidateList();
        FCITX_ASSERT(candList && candList->size() >= 4);
        FCITX_ASSERT(candList->candidate(0).text().toString() == "對");
        FCITX_ASSERT(candList->candidate(0).comment().toString() ==
                     "[faa | a]");
        FCITX_ASSERT(candList->candidate(3).text().toString() == "典");
        FCITX_ASSERT(candList->candidate(3).comment().toString() == "[fba]");

        testfrontend->call<ITestFrontend::pushCommitExpectation>("典");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("4"), false));

        // Wildcard aaa? -> [龘, 鑆], aaa?v + Space selects 2nd candidate 鑆
        sendKeys(testfrontend, uuid,
                 {Key("a"), Key("a"), Key("a"), Key("question"), Key("v")});
        testfrontend->call<ITestFrontend::pushCommitExpectation>("鑆");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        // Star wildcard a*q
        sendKeys(testfrontend, uuid, {Key("a"), Key("asterisk"), Key("q")});
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "a*q");
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_Escape), false));

        // Homophone V/R/S/F suffix selection: `frv + Space -> 葉 (2nd of `fr)
        sendKeyStrings(testfrontend, uuid, {"grave", "f", "r", "v"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>("葉");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        // Page 2+ immediate V/R/S/F selection on `fr
        sendKeyStrings(testfrontend, uuid, {"grave", "f", "r"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("equal"), false));
        auto page2List = ic->inputPanel().candidateList();
        FCITX_ASSERT(page2List && page2List->toPageable()->currentPage() == 1 &&
                     page2List->size() >= 2);
        const std::string page2Second = page2List->candidate(1).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(page2Second);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("v"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

void testCustomDictionaryAndHelp(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        const std::filesystem::path customPath =
            std::filesystem::path(TESTING_BINARY_DIR) /
            "test/boshiamy/custom.txt";
        {
            std::ofstream out(customPath, std::ios::out | std::ios::trunc);
            FCITX_ASSERT(out.is_open());
            out << "# Test custom dictionary\n"
                << "zmail test@example.com\n"
                << "zmult 第一詞\n"
                << "zmult 第二詞\n"
                << "zmult 第二詞\n"         // duplicate, ignored
                << "zmulti 太長\n"          // > 5 keys, ignored
                << "z1 數字\n";             // invalid key, ignored
        }

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "boshiamy");

        // ,,r + 2 still commits 〈
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "r"});
        auto candList = ic->inputPanel().candidateList();
        FCITX_ASSERT(candList && candList->size() >= 2);
        FCITX_ASSERT(candList->candidate(1).text().toString() == "〈");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("〈");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("2"), false));

        // ,,r + Space reloads custom.txt hot
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "r"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().find(
                         "已重載自訂詞庫（3 筆）") != std::string::npos);

        // Custom phrase zmail + Space
        sendKeyStrings(testfrontend, uuid, {"z", "m", "a", "i", "l"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "test@example.com");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        // Auto-synthesized v suffix on 5-key custom code -> 6 typed keys
        sendKeyStrings(testfrontend, uuid, {"z", "m", "u", "l", "t", "v"});
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "zmultv");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("第二詞");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        // Enter on a command commits the raw text instead of running it
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "s", "t"});
        testfrontend->call<ITestFrontend::pushCommitExpectation>(",,st");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        // ,,? help command
        sendKeys(testfrontend, uuid,
                 {Key("comma"), Key("comma"), Key("question")});
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == ",,?");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().find("指令:") !=
                     std::string::npos);

        // Remove temp custom.txt and reload cleanly
        std::error_code ec;
        std::filesystem::remove(customPath, ec);
        sendKeyStrings(testfrontend, uuid, {"comma", "comma", "r"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));

        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

void testPasswordAndPerContextHistory(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");

        // Normal context: commit 對 so it has history.
        auto uuidA =
            testfrontend->call<ITestFrontend::createInputContext>("appA");
        auto *icA = instance->inputContextManager().findByUUID(uuidA);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuidA, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(icA) == "boshiamy");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuidA, Key("a"), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>("對");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuidA, Key("space"), false));

        // Sensitive context (Password fields are normally blocked by fcitx
        // core unless AllowInputMethodForPassword is set): commits still
        // work but nothing is remembered, and history from another context
        // is not visible.
        auto uuidB =
            testfrontend->call<ITestFrontend::createInputContext>("appB");
        auto *icB = instance->inputContextManager().findByUUID(uuidB);
        icB->setCapabilityFlags(CapabilityFlags{CapabilityFlag::Sensitive});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuidB, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(icB) == "boshiamy");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuidB, Key("a"), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>("對");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuidB, Key("space"), false));
        sendKeyStrings(testfrontend, uuidB, {"comma", "comma", "q"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuidB, Key("space"), false));
        FCITX_ASSERT(icB->inputPanel().auxUp().toString().find(
                         "尚無最近輸入的字") != std::string::npos);

        // Context A still sees its own history.
        sendKeyStrings(testfrontend, uuidA, {"comma", "comma", "q"});
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuidA, Key("space"), false));
        FCITX_ASSERT(icA->inputPanel().auxUp().toString().find("「對」") !=
                     std::string::npos);

        testfrontend->call<ITestFrontend::destroyInputContext>(uuidA);
        testfrontend->call<ITestFrontend::destroyInputContext>(uuidB);
    });
}

void testLookupLimit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *boshiamy = instance->addonManager().addon("boshiamy", true);
        RawConfig cfg;
        cfg.setValueByPath("ExactMatch", "False");
        cfg.setValueByPath("PageSize", "10");
        boshiamy->setConfig(cfg);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("a"), false));
        auto *list = ic->inputPanel().candidateList()->toBulk();
        FCITX_ASSERT(list);
        FCITX_ASSERT(list->totalSize() == 200) << list->totalSize();
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        // Wildcard is capped too.
        sendKeys(testfrontend, uuid, {Key("a"), Key("asterisk")});
        list = ic->inputPanel().candidateList()->toBulk();
        FCITX_ASSERT(list && list->totalSize() == 200) << list->totalSize();
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

} // namespace

int main() {
    setupTestingEnvironment(TESTING_BINARY_DIR, {TESTING_BINARY_DIR "/src"},
                            {TESTING_BINARY_DIR "/test"});
    char arg0[] = "testboshiamy";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,boshiamy";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,boshiamy=5");

    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    testStandardTyping(&instance);
    testHomophoneMode(&instance);
    testSpeedMode(&instance);
    testSimplifiedReverseAndStats(&instance);
    testWildcardAndVrsfSelection(&instance);
    testCustomDictionaryAndHelp(&instance);
    testPasswordAndPerContextHistory(&instance);
    testLookupLimit(&instance);

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    return instance.exec();
}
