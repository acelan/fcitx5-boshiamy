/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX5_BOSHIAMY_DICT_H_
#define _FCITX5_BOSHIAMY_DICT_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fcitx {

struct CandidateEntry {
    std::string word;
    std::string code;
    bool isExact = false;
};

struct WildcardEntry {
    std::string word;
    std::string matchedCode;
    std::string shortestCode;
};

struct HomophoneEntry {
    std::string word;
    std::string bopomofo;
    std::string shortestCode;
};

struct HomophoneResult {
    std::vector<std::pair<std::string, std::string>> basePronunciations;
    std::vector<HomophoneEntry> entries;
};

class BoshiamyDictionary {
public:
    bool load(const std::filesystem::path &tablePath,
              const std::filesystem::path &chewingPath,
              const std::filesystem::path &t2sPath = {},
              const std::filesystem::path &customPath = {});
    bool loadBoshiamyTable(std::istream &in);
    bool loadChewingCsv(std::istream &in);
    bool loadT2STable(std::istream &in);
    bool loadCustomTable(std::istream &in);
    bool reloadCustomTable(const std::filesystem::path &customPath);
    void clear();

    bool isLoaded() const { return !exactMap_.empty(); }
    bool hasHomophones() const { return !bopomofoToWords_.empty(); }
    bool hasT2S() const { return !t2sMap_.empty(); }
    size_t customEntryCount() const { return customEntries_.size(); }
    size_t maxCodeLength() const { return kMaxInputLength; }

    // Boshiamy codes are at most 5 keys long; one extra v/r/s/f key may be
    // appended to select a candidate, so typed input is at most 6 keys.
    static constexpr size_t kMaxCodeLength = 5;
    static constexpr size_t kMaxInputLength = kMaxCodeLength + 1;
    // Upper bound of candidates returned by prefix and wildcard lookups.
    static constexpr size_t kMaxLookupResults = 200;

    static bool isInputCode(uint32_t c);
    static bool isWildcardChar(uint32_t c);
    static bool hasWildcard(std::string_view code);
    static bool isValidCode(std::string_view code);
    static int vrsfSelectionIndex(char c);

    std::vector<CandidateEntry> lookup(std::string_view code,
                                       bool exactMatch) const;
    std::vector<WildcardEntry> lookupWildcard(std::string_view pattern) const;
    HomophoneResult lookupHomophones(std::string_view code) const;

    std::string toSimplified(std::string_view text) const;

    const std::vector<std::string> &shortestCodes(const std::string &word) const;
    std::string formatShortestCodes(const std::string &word) const;
    const std::vector<std::string> &allCodes(const std::string &word) const;
    std::string formatAllCodes(const std::string &word) const;

private:
    struct PrefixEntry {
        std::string code;
        std::string word;
        size_t order = 0;
    };

    void rebuildIndex();
    void buildCodeIndexes();
    std::vector<CandidateEntry> lookupDirect(std::string_view code,
                                             bool exactMatch) const;
    std::vector<WildcardEntry>
    lookupWildcardDirect(std::string_view pattern) const;
    HomophoneResult lookupHomophonesDirect(std::string_view code) const;

    std::unordered_map<std::string, std::vector<std::string>> baseExactMap_;
    std::vector<PrefixEntry> baseEntries_;
    std::vector<std::pair<std::string, std::string>> customEntries_;

    std::unordered_map<std::string, std::vector<std::string>> exactMap_;
    std::vector<PrefixEntry> prefixEntries_;
    std::unordered_map<std::string, std::vector<std::string>> shortestCodes_;
    std::unordered_map<std::string, std::vector<std::string>> allCodes_;

    std::unordered_map<std::string, std::string> t2sMap_;
    std::unordered_map<std::string, std::string> wordToBopomofo_;
    std::unordered_map<std::string, std::vector<std::string>> bopomofoToWords_;
};

} // namespace fcitx

#endif // _FCITX5_BOSHIAMY_DICT_H_
