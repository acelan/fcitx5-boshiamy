/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/flags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <fstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fcitx {

namespace {

FCITX_DEFINE_LOG_CATEGORY(boshiamyLog, "boshiamy");

constexpr std::string_view kHelpMessage =
    "📖 指令: ,,sp 快打 | ,,c 簡體 | ,,t 繁體 | ,,q 反查 | ,,st 統計 | "
    ",,r 重載詞庫 | `同音 | *?萬用碼";

bool isCommand(std::string_view buf) {
    return buf == ",,sp" || buf == ",,c" || buf == ",,t" || buf == ",,q" ||
           buf == ",,st" || buf == ",,r" || buf == ",,h" || buf == ",,?";
}

class BoshiamyCandidateWord final : public CandidateWord {
public:
    BoshiamyCandidateWord(BoshiamyState *state, Text text, Text comment,
                          std::string word, std::string origWord,
                          std::string typedCode, bool isLookup, bool isCommand)
        : CandidateWord(std::move(text)), state_(state),
          word_(std::move(word)), origWord_(std::move(origWord)),
          typedCode_(std::move(typedCode)), isLookup_(isLookup),
          isCommand_(isCommand) {
        if (!comment.empty()) {
            setComment(std::move(comment));
        }
    }

    void select(InputContext * /*inputContext*/) const override {
        if (isCommand_) {
            state_->executeCommand(typedCode_);
        } else {
            state_->commitCandidate(word_, origWord_, typedCode_, isLookup_);
        }
    }

private:
    BoshiamyState *state_;
    std::string word_;
    std::string origWord_;
    std::string typedCode_;
    bool isLookup_;
    bool isCommand_;
};

const KeyList &digitSelectionKeys() {
    static const KeyList keys = Key::keyListFromString("1 2 3 4 5 6 7 8 9 0");
    return keys;
}

} // namespace

BoshiamyState::BoshiamyState(InputContext *ic, BoshiamyEngine *engine)
    : ic_(ic), engine_(engine) {}

void BoshiamyState::showHint(const std::string &hint, uint64_t durationUsec) {
    lastHint_ = hint;
    hintTimer_ = engine_->instance()->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + durationUsec, 100000,
        [this, ref = ic_->watch()](EventSourceTime *, uint64_t) {
            hintTimer_.reset();
            if (ref.get()) {
                lastHint_.clear();
                updateUI();
            }
            return true;
        });
}

bool BoshiamyState::isSensitiveContext() const {
    return ic_->capabilityFlags().testAny(
        CapabilityFlags{CapabilityFlag::Password, CapabilityFlag::Sensitive});
}

void BoshiamyState::clearHint() {
    lastHint_.clear();
    hintTimer_.reset();
}

void BoshiamyState::reset() {
    buffer_.clear();
    clearHint();
    ic_->inputPanel().reset();
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void BoshiamyState::executeCommand(const std::string &cmd) {
    if (cmd == ",,sp") {
        engine_->toggleSpeedMode(ic_);
    } else if (cmd == ",,c") {
        engine_->setSimplifiedMode(true, ic_);
    } else if (cmd == ",,t") {
        engine_->setSimplifiedMode(false, ic_);
    } else if (cmd == ",,q") {
        buffer_.clear();
        const std::string &orig = lastCommittedOrigWord_;
        const std::string &disp = lastCommittedWord_;
        if (orig.empty()) {
            showHint("🔍 尚無最近輸入的字", 3000000);
        } else {
            std::string codes = engine_->dict().formatAllCodes(orig);
            if (codes.empty() && disp != orig) {
                codes = engine_->dict().formatAllCodes(disp);
            }
            if (codes.empty()) {
                showHint("🔍 「" + disp + "」查無拆碼", 4000000);
            } else if (disp != orig) {
                showHint("🔍 「" + disp + "」(原: " + orig + ") 拆碼: " + codes,
                         5000000);
            } else {
                showHint("🔍 「" + disp + "」拆碼: " + codes, 5000000);
            }
        }
        updateUI();
    } else if (cmd == ",,st") {
        buffer_.clear();
        showHint(engine_->formatStats(), 5000000);
        updateUI();
    } else if (cmd == ",,r") {
        engine_->reloadCustomDictionary(ic_);
    } else if (cmd == ",,h" || cmd == ",,?") {
        buffer_.clear();
        showHint(std::string(kHelpMessage), 6000000);
        updateUI();
    }
}

void BoshiamyState::commitCandidate(const std::string &word,
                                    const std::string &origWord,
                                    const std::string &typedCode,
                                    bool isLookup) {
    const auto &config = engine_->config();
    const std::string &checkWord = origWord.empty() ? word : origWord;
    const auto &shortest = engine_->dict().shortestCodes(checkWord);
    std::string hintMsg;

    if (!isLookup && *config.speedMode) {
        if (!shortest.empty() && shortest[0].size() < typedCode.size()) {
            const std::string formatted =
                engine_->dict().formatShortestCodes(checkWord);
            if (*config.strictSpeedMode) {
                buffer_.clear();
                showHint("⛔「" + word + "」請打最短碼: " + formatted);
                updateUI();
                return;
            }
            hintMsg = "💡「" + word + "」最短碼: " + formatted;
        }
    } else if (isLookup && *config.showHomophoneCode && *config.speedMode &&
               !shortest.empty()) {
        hintMsg = "💡「" + word + "」最短碼: " +
                  engine_->dict().formatShortestCodes(checkWord);
    }

    if (hintMsg.empty()) {
        clearHint();
    } else {
        showHint(hintMsg);
    }

    ic_->commitString(word);
    // Never retain anything typed into password/sensitive fields.
    if (!isSensitiveContext()) {
        lastCommittedWord_ = word;
        lastCommittedOrigWord_ = checkWord;
        engine_->recordCommit(checkWord, typedCode, isLookup);
    }
    buffer_.clear();
    updateUI();
}

bool BoshiamyState::handleCandidateNavigation(KeyEvent &event) {
    auto candidateList = ic_->inputPanel().candidateList();
    if (!candidateList || candidateList->empty() || event.isVirtual()) {
        return false;
    }

    const int idx = event.key().digitSelection();
    if (idx >= 0) {
        event.filterAndAccept();
        if (idx < candidateList->size()) {
            candidateList->candidate(idx).select(ic_);
        }
        return true;
    }

    const auto &config = engine_->config();
    if (event.key().checkKeyList(*config.prevPage)) {
        event.filterAndAccept();
        if (auto *pageable = candidateList->toPageable();
            pageable && pageable->hasPrev()) {
            pageable->prev();
            ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
        return true;
    }

    if (event.key().checkKeyList(*config.nextPage)) {
        event.filterAndAccept();
        if (auto *pageable = candidateList->toPageable();
            pageable && pageable->hasNext()) {
            pageable->next();
            ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
        return true;
    }

    // After paging beyond page 1, v/r/s/f directly pick candidates 2..5 of the
    // visible page so selection matches official Boshiamy muscle memory even
    // where baked-in table suffixes don't exist.
    if (!event.key().hasModifier()) {
        if (auto *pageable = candidateList->toPageable();
            pageable && pageable->currentPage() > 0) {
            const uint32_t chr = Key::keySymToUnicode(event.key().sym());
            if (chr >= 'a' && chr <= 'z') {
                const int vrsfIdx = BoshiamyDictionary::vrsfSelectionIndex(
                    static_cast<char>(chr));
                if (vrsfIdx >= 0) {
                    event.filterAndAccept();
                    if (vrsfIdx < candidateList->size()) {
                        candidateList->candidate(vrsfIdx).select(ic_);
                    }
                    return true;
                }
            }
        }
    }

    if (auto *movable = candidateList->toCursorMovable()) {
        if (event.key().check(FcitxKey_Left) ||
            event.key().check(FcitxKey_KP_Left)) {
            movable->prevCandidate();
            ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        }
        if (event.key().check(FcitxKey_Right) ||
            event.key().check(FcitxKey_KP_Right)) {
            movable->nextCandidate();
            ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        }
    }

    return false;
}

bool BoshiamyState::handleSpecialKeys(KeyEvent &event) {
    if (buffer_.empty()) {
        return false;
    }

    if (event.key().check(FcitxKey_Escape)) {
        buffer_.clear();
        clearHint();
        updateUI();
        event.filterAndAccept();
        return true;
    }

    if (event.key().check(FcitxKey_BackSpace)) {
        buffer_.pop_back();
        updateUI();
        event.filterAndAccept();
        return true;
    }

    if (event.key().check(FcitxKey_space) ||
        event.key().check(FcitxKey_KP_Space)) {
        event.filterAndAccept();

        auto candidateList = ic_->inputPanel().candidateList();
        if (candidateList && !candidateList->empty()) {
            const int idx = std::max(candidateList->cursorIndex(), 0);
            if (idx < candidateList->size()) {
                candidateList->candidate(idx).select(ic_);
            }
            return true;
        }

        if (isCommand(buffer_)) {
            executeCommand(buffer_);
            return true;
        }

        if (buffer_ == "`") {
            clearHint();
            ic_->commitString("`");
            buffer_.clear();
            updateUI();
            return true;
        }

        showHint("⚠️ 無此字碼: " + buffer_);
        buffer_.clear();
        updateUI();
        return true;
    }

    if (event.key().check(FcitxKey_Return) ||
        event.key().check(FcitxKey_KP_Enter)) {
        event.filterAndAccept();
        // Enter always commits the raw buffer; commands run only on Space.
        clearHint();
        ic_->commitString(buffer_);
        buffer_.clear();
        updateUI();
        return true;
    }

    return false;
}

bool BoshiamyState::handleInputCharacter(KeyEvent &event) {
    // Allow Shift so shifted punctuation such as '?' and '*' works in real
    // X11/Wayland sessions; reject Ctrl/Alt/Super shortcuts.
    if (event.key().states().testAny(
            KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Super})) {
        return false;
    }

    const uint32_t chr = Key::keySymToUnicode(event.key().sym());
    if (!chr) {
        return false;
    }

    if (buffer_.empty()) {
        if (chr == '`' || BoshiamyDictionary::isInputCode(chr)) {
            buffer_.push_back(static_cast<char>(chr));
            updateUI();
            event.filterAndAccept();
            return true;
        }
        return false;
    }

    if (buffer_[0] == '`') {
        if (chr == '`' && buffer_.size() == 1) {
            clearHint();
            ic_->commitString("`");
            buffer_.clear();
            updateUI();
            event.filterAndAccept();
            return true;
        }
        if (chr >= 'a' && chr <= 'z') {
            if (buffer_.size() < 6) {
                buffer_.push_back(static_cast<char>(chr));
                updateUI();
            }
            event.filterAndAccept();
            return true;
        }
        return false;
    }

    if (buffer_ == ",," && chr == '?') {
        buffer_.push_back('?');
        updateUI();
        event.filterAndAccept();
        return true;
    }

    if (buffer_.starts_with(",,")) {
        if (buffer_ != ",,?" && BoshiamyDictionary::isInputCode(chr)) {
            if (buffer_.size() < 5) {
                buffer_.push_back(static_cast<char>(chr));
                updateUI();
            }
            event.filterAndAccept();
            return true;
        }
        return false;
    }

    if (BoshiamyDictionary::isWildcardChar(chr)) {
        if (buffer_.size() < 8) {
            buffer_.push_back(static_cast<char>(chr));
            updateUI();
        }
        event.filterAndAccept();
        return true;
    }

    if (BoshiamyDictionary::isInputCode(chr)) {
        const size_t maxLen = BoshiamyDictionary::hasWildcard(buffer_)
                                  ? 8
                                  : engine_->dict().maxCodeLength();
        if (buffer_.size() < maxLen) {
            buffer_.push_back(static_cast<char>(chr));
            updateUI();
        }
        event.filterAndAccept();
        return true;
    }

    return false;
}

void BoshiamyState::keyEvent(KeyEvent &event) {
    if (event.isRelease() || event.key().isModifier()) {
        return;
    }

    if (handleSpecialKeys(event)) {
        return;
    }

    if (handleCandidateNavigation(event)) {
        return;
    }

    if (handleInputCharacter(event)) {
        return;
    }

    if (!buffer_.empty() &&
        !event.key().states().testAny(
            KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Super})) {
        if (Key::keySymToUnicode(event.key().sym())) {
            if (isCommand(buffer_)) {
                buffer_.clear();
                updateUI();
                return;
            }
            auto candidateList = ic_->inputPanel().candidateList();
            if (candidateList && !candidateList->empty()) {
                const int idx = std::max(candidateList->cursorIndex(), 0);
                if (idx < candidateList->size()) {
                    candidateList->candidate(idx).select(ic_);
                }
            } else {
                clearHint();
                ic_->commitString(buffer_);
                buffer_.clear();
                updateUI();
            }
        }
    }
}

void BoshiamyState::updateUI() {
    auto &inputPanel = ic_->inputPanel();
    inputPanel.reset();

    if (!buffer_.empty()) {
        Text preedit(buffer_);
        preedit.setCursor(preedit.textLength());
        if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
            inputPanel.setClientPreedit(preedit);
        } else {
            inputPanel.setPreedit(preedit);
        }
    }

    const auto &config = engine_->config();
    const bool simplify = *config.simplifiedMode;

    auto createCandidateList = [&config]() {
        auto list = std::make_unique<CommonCandidateList>();
        list->setSelectionKey(digitSelectionKeys());
        list->setPageSize(*config.pageSize);
        list->setCursorPositionAfterPaging(
            CursorPositionAfterPaging::ResetToFirst);
        return list;
    };

    std::unordered_set<std::string> seenDisplayWords;
    auto appendDictCandidate = [&](std::unique_ptr<CommonCandidateList> &list,
                                   const std::string &origWord, Text comment,
                                   const std::string &typedCode, bool isLookup) {
        std::string displayWord =
            simplify ? engine_->dict().toSimplified(origWord) : origWord;
        if (!seenDisplayWords.insert(displayWord).second) {
            return;
        }
        if (!list) {
            list = createCandidateList();
        }
        list->append<BoshiamyCandidateWord>(
            this, Text(displayWord), std::move(comment), std::move(displayWord),
            origWord, typedCode, isLookup, /*isCommand=*/false);
    };

    std::unique_ptr<CommonCandidateList> candidateList;

    auto appendExactDictMatches = [&](std::string_view code) {
        auto exactMatches = engine_->dict().lookup(code, /*exactMatch=*/true);
        for (const auto &cand : exactMatches) {
            appendDictCandidate(candidateList, cand.word, Text{},
                                std::string(code), /*isLookup=*/false);
        }
    };

    if (buffer_ == ",,sp") {
        inputPanel.setAuxUp(Text("⚡ 按空白鍵切換快打模式"));
        candidateList = createCandidateList();
        candidateList->append<BoshiamyCandidateWord>(
            this, Text("[切換快打模式]"),
            Text(*config.speedMode ? "目前：開啟" : "目前：關閉"), "", "",
            ",,sp", false, true);
    } else if (buffer_ == ",,c") {
        inputPanel.setAuxUp(Text("🀄 按空白鍵切換為簡體中文輸出"));
        candidateList = createCandidateList();
        candidateList->append<BoshiamyCandidateWord>(
            this, Text("[切換簡體模式]"),
            Text(simplify ? "目前：簡體" : "目前：繁體"), "", "", ",,c", false,
            true);
        appendExactDictMatches(buffer_);
    } else if (buffer_ == ",,t") {
        inputPanel.setAuxUp(Text("🀄 按空白鍵切換為繁體中文輸出"));
        candidateList = createCandidateList();
        candidateList->append<BoshiamyCandidateWord>(
            this, Text("[切換繁體模式]"),
            Text(simplify ? "目前：簡體" : "目前：繁體"), "", "", ",,t", false,
            true);
    } else if (buffer_ == ",,q") {
        inputPanel.setAuxUp(Text("🔍 按空白鍵反查上一字拆碼"));
        candidateList = createCandidateList();
        const std::string &lastWord = lastCommittedWord_;
        candidateList->append<BoshiamyCandidateWord>(
            this, Text("[反查拆碼]"),
            Text(lastWord.empty() ? "尚無紀錄" : ("上一字：" + lastWord)), "",
            "", ",,q", false, true);
    } else if (buffer_ == ",,st") {
        inputPanel.setAuxUp(Text("📊 按空白鍵顯示快打統計"));
        candidateList = createCandidateList();
        candidateList->append<BoshiamyCandidateWord>(
            this, Text("[快打統計]"),
            Text("字數：" + std::to_string(engine_->totalCommits())), "", "",
            ",,st", false, true);
    } else if (buffer_ == ",,r") {
        inputPanel.setAuxUp(Text("🔄 按空白鍵重新載入自訂詞庫"));
        candidateList = createCandidateList();
        candidateList->append<BoshiamyCandidateWord>(
            this, Text("[重載自訂詞庫]"),
            Text("目前：" + std::to_string(engine_->dict().customEntryCount()) +
                 " 筆"),
            "", "", ",,r", false, true);
        appendExactDictMatches(buffer_);
    } else if (buffer_ == ",,h" || buffer_ == ",,?") {
        inputPanel.setAuxUp(Text(std::string(kHelpMessage)));
    } else if (buffer_ == "`") {
        inputPanel.setAuxUp(Text("🔍 同音字查詢：請輸入嘸蝦米字碼 (a-z)"));
    } else if (buffer_.size() > 1 && buffer_[0] == '`') {
        const std::string_view code = std::string_view(buffer_).substr(1);
        auto homophones = engine_->dict().lookupHomophones(code);

        if (!homophones.basePronunciations.empty()) {
            std::string aux = "🔍 同音字：";
            const size_t limit =
                std::min<size_t>(homophones.basePronunciations.size(), 3);
            for (size_t i = 0; i < limit; ++i) {
                if (i > 0) {
                    aux += " / ";
                }
                const std::string &baseWord =
                    homophones.basePronunciations[i].first;
                aux += (simplify ? engine_->dict().toSimplified(baseWord)
                                 : baseWord) +
                       "(" + homophones.basePronunciations[i].second + ")";
            }
            if (homophones.basePronunciations.size() > limit) {
                aux += " ...";
            }
            inputPanel.setAuxUp(Text(std::move(aux)));
        } else if (!homophones.entries.empty()) {
            inputPanel.setAuxUp(Text("🔍 同音字查詢"));
        } else {
            inputPanel.setAuxUp(
                Text("🔍 無此字碼的同音字: " + std::string(code)));
        }

        const std::string codeStr(code);
        for (const auto &entry : homophones.entries) {
            Text comment;
            if (*config.showHomophoneCode && !entry.shortestCode.empty()) {
                comment.append("[" + entry.shortestCode + "]");
            }
            appendDictCandidate(candidateList, entry.word, std::move(comment),
                                codeStr, /*isLookup=*/true);
        }
    } else if (BoshiamyDictionary::hasWildcard(buffer_)) {
        auto matches = engine_->dict().lookupWildcard(buffer_);
        if (matches.empty()) {
            inputPanel.setAuxUp(Text("🌟 萬用碼無符合字: " + buffer_));
        } else {
            inputPanel.setAuxUp(Text("🌟 萬用碼查詢: " + buffer_));
            for (const auto &entry : matches) {
                std::string label = "[" + entry.matchedCode;
                if (!entry.shortestCode.empty() &&
                    entry.shortestCode != entry.matchedCode) {
                    label += " | " + entry.shortestCode;
                }
                label += "]";
                appendDictCandidate(candidateList, entry.word,
                                    Text(std::move(label)), buffer_,
                                    /*isLookup=*/true);
            }
        }
    } else {
        if (!lastHint_.empty()) {
            inputPanel.setAuxUp(Text(lastHint_));
        }
        if (!buffer_.empty()) {
            auto candidates =
                engine_->dict().lookup(buffer_, *config.exactMatch);
            for (const auto &cand : candidates) {
                appendDictCandidate(candidateList, cand.word, Text{}, buffer_,
                                    /*isLookup=*/false);
            }
        }
    }

    if (candidateList && !candidateList->empty()) {
        candidateList->setGlobalCursorIndex(0);
        inputPanel.setCandidateList(std::move(candidateList));
    }

    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

BoshiamyEngine::BoshiamyEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &ic) { return new BoshiamyState(&ic, this); }) {
    loadDictionary();
    reloadConfig();

    instance_->inputContextManager().registerProperty("boshiamyState",
                                                      &factory_);

    speedModeAction_.setCheckable(true);
    speedModeAction_.setLongText(_("Toggle Boshiamy Speed Mode (,,sp)"));
    speedModeAction_.connect<SimpleAction::Activated>(
        [this](InputContext *ic) { toggleSpeedMode(ic); });
    instance_->userInterfaceManager().registerAction("boshiamy-speed-mode",
                                                     &speedModeAction_);
    updateSpeedModeAction(nullptr);
}

BoshiamyEngine::~BoshiamyEngine() = default;

std::filesystem::path BoshiamyEngine::resolveCustomPath() const {
    auto userExisting = StandardPaths::global().locate(
        StandardPathsType::PkgData, "boshiamy/custom.txt",
        StandardPathsMode::User);
    if (!userExisting.empty()) {
        return userExisting;
    }

    auto anyExisting = StandardPaths::global().locate(
        StandardPathsType::PkgData, "boshiamy/custom.txt");
    if (!anyExisting.empty()) {
        return anyExisting;
    }

    if (!StandardPaths::global().skipUserPath()) {
        const auto &userDir = StandardPaths::global().userDirectory(
            StandardPathsType::PkgData);
        if (!userDir.empty()) {
            return userDir / "boshiamy/custom.txt";
        }
    }
    return {};
}

void BoshiamyEngine::ensureCustomFileExists(
    const std::filesystem::path &path) const {
    if (path.empty() || StandardPaths::global().skipUserPath() ||
        std::filesystem::exists(path)) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return;
    }
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (out.is_open()) {
        out << "# 嘸蝦米自訂詞庫（加加詞庫）\n"
            << "# 格式：字碼 詞組（以空白或 Tab 分隔，# 開頭為註解）\n"
            << "# 編輯後於輸入法中輸入 ,,r 即可重新載入\n"
            << "# 範例：\n"
            << "# 字碼最長 5 碼（可再加 v/r/s/f 選字），限 a-z , . ' [ ]\n"
            << "# mail user@example.com\n";
    }
}

void BoshiamyEngine::loadDictionary() {
    auto tablePath = StandardPaths::global().locate(StandardPathsType::PkgData,
                                                    "boshiamy/boshiamy.txt");
    auto chewingPath = StandardPaths::global().locate(
        StandardPathsType::PkgData, "boshiamy/chewing_word.csv");
    auto t2sPath = StandardPaths::global().locate(StandardPathsType::PkgData,
                                                  "boshiamy/t2s.txt");
    auto customPath = resolveCustomPath();
    ensureCustomFileExists(customPath);

    if (tablePath.empty() ||
        !dict_.load(tablePath, chewingPath, t2sPath, customPath)) {
        FCITX_LOGC(boshiamyLog, Error)
            << "Failed to load boshiamy dictionary from " << tablePath;
    }
}

void BoshiamyEngine::activate(const InputMethodEntry &entry,
                              InputContextEvent &event) {
    FCITX_UNUSED(entry);
    auto *inputContext = event.inputContext();
    // Traditional/Simplified conversion is handled natively (,,c / ,,t);
    // chttrans is intentionally not exposed to avoid double conversion.
    fullwidth();
    if (auto *action =
            instance_->userInterfaceManager().lookupAction("fullwidth")) {
        inputContext->statusArea().addAction(StatusGroup::InputMethod, action);
    }
    updateSpeedModeAction(inputContext);
    inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                         &speedModeAction_);
}

void BoshiamyEngine::deactivate(const InputMethodEntry &entry,
                                InputContextEvent &event) {
    reset(entry, event);
}

void BoshiamyEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->keyEvent(event);
}

void BoshiamyEngine::reset(const InputMethodEntry &entry,
                           InputContextEvent &event) {
    FCITX_UNUSED(entry);
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->reset();
}

std::string BoshiamyEngine::subMode(const InputMethodEntry &entry,
                                    InputContext &ic) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(ic);
    if (*config_.simplifiedMode && *config_.speedMode) {
        return _("簡體/快打");
    }
    if (*config_.simplifiedMode) {
        return _("簡體");
    }
    return *config_.speedMode ? _("快打") : std::string{};
}

std::string BoshiamyEngine::subModeLabelImpl(const InputMethodEntry &entry,
                                             InputContext &ic) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(ic);
    if (*config_.simplifiedMode) {
        return "簡";
    }
    return *config_.speedMode ? "快" : "嘸";
}

void BoshiamyEngine::setConfig(const RawConfig &config) {
    config_.load(config, true);
    save();
    updateSpeedModeAction(nullptr);
}

void BoshiamyEngine::reloadConfig() {
    readAsIni(config_, StandardPathsType::PkgConfig, "conf/boshiamy.conf");
    updateSpeedModeAction(nullptr);
}

void BoshiamyEngine::save() {
    safeSaveAsIni(config_, StandardPathsType::PkgConfig, "conf/boshiamy.conf");
}

void BoshiamyEngine::toggleSpeedMode(InputContext *ic) {
    config_.speedMode.setValue(!(*config_.speedMode));
    save();
    updateSpeedModeAction(ic);
    if (ic) {
        auto *state = ic->propertyFor(&factory_);
        state->clearBuffer();
        state->showHint(*config_.speedMode ? "⚡ 快打模式：開啟"
                                           : "⚡ 快打模式：關閉");
        state->updateUI();
        ic->updateUserInterface(UserInterfaceComponent::StatusArea);
    }
}

void BoshiamyEngine::setSimplifiedMode(bool simplified, InputContext *ic) {
    config_.simplifiedMode.setValue(simplified);
    save();
    if (ic) {
        auto *state = ic->propertyFor(&factory_);
        state->clearBuffer();
        state->showHint(simplified ? "🀄 輸出模式：簡體中文"
                                   : "🀄 輸出模式：繁體中文");
        state->updateUI();
        ic->updateUserInterface(UserInterfaceComponent::StatusArea);
    }
}

void BoshiamyEngine::reloadCustomDictionary(InputContext *ic) {
    auto customPath = resolveCustomPath();
    ensureCustomFileExists(customPath);
    const bool loaded = dict_.reloadCustomTable(customPath);
    if (ic) {
        auto *state = ic->propertyFor(&factory_);
        state->clearBuffer();
        if (loaded) {
            state->showHint("🔄 已重載自訂詞庫（" +
                                std::to_string(dict_.customEntryCount()) +
                                " 筆）",
                            4000000);
        } else if (customPath.empty()) {
            state->showHint("⚠️ 無可用的自訂詞庫路徑", 4000000);
        } else {
            state->showHint("⚠️ 無法讀取自訂詞庫: " + customPath.string(),
                            4000000);
        }
        state->updateUI();
    }
}

void BoshiamyEngine::recordCommit(const std::string &origWord,
                                  const std::string &typedCode, bool isLookup) {
    if (isLookup || typedCode.empty()) {
        return;
    }
    ++totalCommits_;
    totalKeystrokes_ += typedCode.size();
    const auto &shortest = dict_.shortestCodes(origWord);
    if (!shortest.empty() && shortest[0].size() < typedCode.size()) {
        ++missedCount_[origWord];
    } else {
        ++shortestHitCount_;
    }
}

std::string BoshiamyEngine::formatStats() const {
    if (totalCommits_ == 0) {
        return "📊 快打統計：尚無打字紀錄";
    }
    const auto hitRate = static_cast<long>(
        std::lround(100.0 * static_cast<double>(shortestHitCount_) /
                    static_cast<double>(totalCommits_)));
    const double avgLen = static_cast<double>(totalKeystrokes_) /
                          static_cast<double>(totalCommits_);
    char avgBuf[32];
    std::snprintf(avgBuf, sizeof(avgBuf), "%.1f", avgLen);

    std::string out = "📊 字數: " + std::to_string(totalCommits_) +
                      " | 最短碼率: " + std::to_string(hitRate) +
                      "% | 均碼長: " + std::string(avgBuf);

    if (!missedCount_.empty()) {
        std::vector<std::pair<std::string, size_t>> missed(missedCount_.begin(),
                                                           missedCount_.end());
        std::sort(missed.begin(), missed.end(),
                  [](const auto &lhs, const auto &rhs) {
                      if (lhs.second != rhs.second) {
                          return lhs.second > rhs.second;
                      }
                      return lhs.first < rhs.first;
                  });
        out += " | 待加強: ";
        const size_t limit = std::min<size_t>(missed.size(), 3);
        for (size_t i = 0; i < limit; ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += missed[i].first;
            const std::string shortest =
                dict_.formatShortestCodes(missed[i].first);
            if (!shortest.empty()) {
                out += "(" + shortest + ")";
            }
        }
    }
    return out;
}

void BoshiamyEngine::updateSpeedModeAction(InputContext *ic) {
    const bool on = *config_.speedMode;
    speedModeAction_.setChecked(on);
    speedModeAction_.setShortText(on ? _("快打模式：開") : _("快打模式：關"));
    speedModeAction_.setIcon(on ? "fcitx-remind-active"
                                : "fcitx-remind-inactive");
    if (ic) {
        speedModeAction_.update(ic);
    }
}

AddonInstance *BoshiamyEngineFactory::create(AddonManager *manager) {
    return new BoshiamyEngine(manager->instance());
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(boshiamy, fcitx::BoshiamyEngineFactory);
