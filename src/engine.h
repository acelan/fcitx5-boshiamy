/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX5_BOSHIAMY_ENGINE_H_
#define _FCITX5_BOSHIAMY_ENGINE_H_

#include "dict.h"
#include <cstddef>
#include <cstdint>
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace fcitx {

FCITX_CONFIGURATION(
    BoshiamyConfig,
    Option<bool> speedMode{this, "SpeedMode",
                           _("Speed Mode (Hint shortest code)"), false};
    Option<bool> strictSpeedMode{
        this, "StrictSpeedMode",
        _("Strict Speed Mode (Block non-shortest code)"), false};
    Option<bool> simplifiedMode{
        this, "SimplifiedMode",
        _("Simplified Chinese Output Mode (,,c / ,,t)"), false};
    Option<bool> exactMatch{this, "ExactMatch", _("Exact Match Only"), false};
    Option<bool> showHomophoneCode{
        this, "ShowHomophoneCode",
        _("Show Boshiamy code in homophone candidates"), true};
    Option<int, IntConstrain> pageSize{this, "PageSize",
                                       _("Candidate Page Size"), 9,
                                       IntConstrain(3, 10)};
    KeyListOption prevPage{
        this,
        "PrevPage",
        _("Previous Page"),
        {Key(FcitxKey_Up), Key(FcitxKey_minus), Key(FcitxKey_Page_Up)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption nextPage{
        this,
        "NextPage",
        _("Next Page"),
        {Key(FcitxKey_Down), Key(FcitxKey_equal), Key(FcitxKey_Page_Down)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};);

class BoshiamyEngine;

class BoshiamyState final : public InputContextProperty {
public:
    BoshiamyState(InputContext *ic, BoshiamyEngine *engine);

    void keyEvent(KeyEvent &event);
    void reset();
    void updateUI();

    void clearBuffer() { buffer_.clear(); }
    const std::string &lastCommittedWord() const { return lastCommittedWord_; }
    void showHint(const std::string &hint, uint64_t durationUsec = 3000000);
    void clearHint();

    void executeCommand(const std::string &cmd);
    void commitCandidate(const std::string &word, const std::string &origWord,
                         const std::string &typedCode, bool isLookup);

private:
    bool handleCandidateNavigation(KeyEvent &event);
    bool handleSpecialKeys(KeyEvent &event);
    bool handleInputCharacter(KeyEvent &event);
    bool isSensitiveContext() const;

    InputContext *ic_;
    BoshiamyEngine *engine_;
    std::string buffer_;
    std::string lastHint_;
    std::unique_ptr<EventSourceTime> hintTimer_;
    // Kept per input context so ,,q never reveals text typed in another
    // application.
    std::string lastCommittedWord_;
    std::string lastCommittedOrigWord_;
};

class BoshiamyEngine final : public InputMethodEngineV2 {
public:
    BoshiamyEngine(Instance *instance);
    ~BoshiamyEngine() override;

    Instance *instance() { return instance_; }
    const BoshiamyDictionary &dict() const { return dict_; }
    const BoshiamyConfig &config() const { return config_; }

    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;

    std::string subMode(const InputMethodEntry &entry,
                        InputContext &ic) override;
    std::string subModeLabelImpl(const InputMethodEntry &entry,
                                 InputContext &ic) override;

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override;
    void reloadConfig() override;
    void save() override;

    void toggleSpeedMode(InputContext *ic);
    void setSimplifiedMode(bool simplified, InputContext *ic);
    void reloadCustomDictionary(InputContext *ic);

    void recordCommit(const std::string &origWord, const std::string &typedCode,
                      bool isLookup);
    std::string formatStats() const;
    size_t totalCommits() const { return totalCommits_; }

    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());

private:
    void loadDictionary();
    std::filesystem::path resolveCustomPath() const;
    void ensureCustomFileExists(const std::filesystem::path &path) const;
    void updateSpeedModeAction(InputContext *ic);

    Instance *instance_;
    BoshiamyConfig config_;
    BoshiamyDictionary dict_;
    FactoryFor<BoshiamyState> factory_;
    SimpleAction speedModeAction_;

    size_t totalCommits_ = 0;
    size_t shortestHitCount_ = 0;
    size_t totalKeystrokes_ = 0;
    std::unordered_map<std::string, size_t> missedCount_;
};

class BoshiamyEngineFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx

#endif // _FCITX5_BOSHIAMY_ENGINE_H_
