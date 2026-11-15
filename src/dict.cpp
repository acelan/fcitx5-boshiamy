/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "dict.h"
#include <algorithm>
#include <array>
#include <fcitx-utils/log.h>
#include <fstream>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace fcitx {

namespace {

FCITX_DEFINE_LOG_CATEGORY(boshiamyDictLog, "boshiamy-dict");

constexpr std::array<char, 4> kVrsfSuffixes = {'v', 'r', 's', 'f'};

std::string_view trimLine(std::string_view line) {
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' ||
            line.back() == ' ' || line.back() == '\t')) {
        line.remove_suffix(1);
    }
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
    }
    return line;
}

std::string joinCodes(const std::vector<std::string> &codes) {
    std::string out;
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) {
            out += " / ";
        }
        out += codes[i];
    }
    return out;
}

size_t utf8CharByteLen(unsigned char lead) {
    if ((lead & 0x80) == 0) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

bool matchWildcard(std::string_view pattern, std::string_view text) {
    size_t pi = 0;
    size_t ti = 0;
    size_t starIdx = std::string_view::npos;
    size_t matchIdx = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() &&
            (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            ++pi;
            ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starIdx = pi++;
            matchIdx = ti;
        } else if (starIdx != std::string_view::npos) {
            pi = starIdx + 1;
            ti = ++matchIdx;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }
    return pi == pattern.size();
}

} // namespace

bool BoshiamyDictionary::load(const std::filesystem::path &tablePath,
                              const std::filesystem::path &chewingPath,
                              const std::filesystem::path &t2sPath,
                              const std::filesystem::path &customPath) {
    clear();
    std::ifstream tableStream(tablePath);
    if (!tableStream.is_open() || !loadBoshiamyTable(tableStream)) {
        clear();
        return false;
    }

    if (!chewingPath.empty()) {
        std::ifstream chewingStream(chewingPath);
        if (chewingStream.is_open()) {
            loadChewingCsv(chewingStream);
        }
    }

    if (!t2sPath.empty()) {
        std::ifstream t2sStream(t2sPath);
        if (t2sStream.is_open()) {
            loadT2STable(t2sStream);
        }
    }

    if (!customPath.empty()) {
        reloadCustomTable(customPath);
    }
    return true;
}

void BoshiamyDictionary::clear() {
    baseExactMap_.clear();
    baseEntries_.clear();
    customEntries_.clear();
    exactMap_.clear();
    prefixEntries_.clear();
    shortestCodes_.clear();
    allCodes_.clear();
    t2sMap_.clear();
    wordToBopomofo_.clear();
    bopomofoToWords_.clear();
}

bool BoshiamyDictionary::isInputCode(uint32_t c) {
    if (c >= 'a' && c <= 'z') {
        return true;
    }
    switch (c) {
    case ',':
    case '.':
    case '\'':
    case '[':
    case ']':
        return true;
    default:
        return false;
    }
}

bool BoshiamyDictionary::isWildcardChar(uint32_t c) {
    return c == '?' || c == '*';
}

bool BoshiamyDictionary::hasWildcard(std::string_view code) {
    return code.find('?') != std::string_view::npos ||
           code.find('*') != std::string_view::npos;
}

bool BoshiamyDictionary::isValidCode(std::string_view code) {
    if (code.empty() || code.size() > kMaxCodeLength) {
        return false;
    }
    return std::all_of(code.begin(), code.end(), [](char c) {
        return isInputCode(static_cast<unsigned char>(c));
    });
}

int BoshiamyDictionary::vrsfSelectionIndex(char c) {
    for (size_t i = 0; i < kVrsfSuffixes.size(); ++i) {
        if (c == kVrsfSuffixes[i]) {
            return static_cast<int>(i + 1);
        }
    }
    return -1;
}

bool BoshiamyDictionary::loadBoshiamyTable(std::istream &in) {
    baseExactMap_.clear();
    baseEntries_.clear();

    std::string rawLine;
    bool inData = false;
    size_t order = 0;

    while (std::getline(in, rawLine)) {
        std::string_view line = trimLine(rawLine);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (!inData) {
            if (line == "[数据]" || line == "[數據]") {
                inData = true;
            }
            continue;
        }

        const size_t sep = line.find_first_of(" \t");
        if (sep == std::string_view::npos) {
            continue;
        }
        std::string_view code = line.substr(0, sep);
        std::string_view word = trimLine(line.substr(sep + 1));
        if (code.empty() || word.empty()) {
            continue;
        }

        std::string codeStr(code);
        std::string wordStr(word);
        auto &words = baseExactMap_[codeStr];
        if (std::find(words.begin(), words.end(), wordStr) == words.end()) {
            words.push_back(wordStr);
            baseEntries_.push_back(
                PrefixEntry{std::move(codeStr), std::move(wordStr), order++});
        }
    }

    if (baseExactMap_.empty()) {
        rebuildIndex();
        return false;
    }

    rebuildIndex();
    return true;
}

bool BoshiamyDictionary::loadT2STable(std::istream &in) {
    t2sMap_.clear();
    std::string rawLine;
    while (std::getline(in, rawLine)) {
        std::string_view line = trimLine(rawLine);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const size_t sep = line.find_first_of(" \t");
        if (sep == std::string_view::npos) {
            continue;
        }
        std::string_view trad = line.substr(0, sep);
        std::string_view simp = trimLine(line.substr(sep + 1));
        if (trad.empty() || simp.empty()) {
            continue;
        }
        t2sMap_.emplace(std::string(trad), std::string(simp));
    }
    return !t2sMap_.empty();
}

bool BoshiamyDictionary::loadCustomTable(std::istream &in) {
    customEntries_.clear();
    std::string rawLine;
    size_t lineNo = 0;
    while (std::getline(in, rawLine)) {
        ++lineNo;
        std::string_view line = trimLine(rawLine);
        if (line.empty() || line.front() == '#' || line == "[数据]" ||
            line == "[數據]") {
            continue;
        }
        const size_t sep = line.find_first_of(" \t");
        if (sep == std::string_view::npos) {
            continue;
        }
        std::string_view code = line.substr(0, sep);
        std::string_view word = trimLine(line.substr(sep + 1));
        if (code.empty() || word.empty()) {
            continue;
        }
        if (!isValidCode(code)) {
            FCITX_LOGC(boshiamyDictLog, Warn)
                << "custom table line " << lineNo << ": invalid code \""
                << code << "\" (must be 1-" << kMaxCodeLength
                << " keys of a-z , . ' [ ]), ignored";
            continue;
        }
        std::pair<std::string, std::string> entry{std::string(code),
                                                  std::string(word)};
        if (std::find(customEntries_.begin(), customEntries_.end(), entry) !=
            customEntries_.end()) {
            continue;
        }
        customEntries_.push_back(std::move(entry));
    }
    rebuildIndex();
    return true;
}

bool BoshiamyDictionary::reloadCustomTable(
    const std::filesystem::path &customPath) {
    customEntries_.clear();
    if (!customPath.empty()) {
        std::ifstream customStream(customPath);
        if (customStream.is_open()) {
            return loadCustomTable(customStream);
        }
    }
    rebuildIndex();
    return false;
}

void BoshiamyDictionary::rebuildIndex() {
    exactMap_ = baseExactMap_;
    prefixEntries_ = baseEntries_;
    size_t order = baseEntries_.size();

    std::unordered_set<std::string> customCodes;
    for (const auto &[code, word] : customEntries_) {
        auto &words = exactMap_[code];
        if (std::find(words.begin(), words.end(), word) == words.end()) {
            words.push_back(word);
            prefixEntries_.push_back(PrefixEntry{code, word, order++});
            customCodes.insert(code);
        }
    }

    for (const auto &code : customCodes) {
        auto it = exactMap_.find(code);
        if (it == exactMap_.end() || it->second.size() < 2) {
            continue;
        }
        const auto &words = it->second;
        for (size_t i = 0; i < kVrsfSuffixes.size() && (i + 1) < words.size();
             ++i) {
            std::string synth = code + kVrsfSuffixes[i];
            if (exactMap_.find(synth) != exactMap_.end()) {
                continue;
            }
            const std::string &targetWord = words[i + 1];
            exactMap_[synth].push_back(targetWord);
            prefixEntries_.push_back(
                PrefixEntry{std::move(synth), targetWord, order++});
        }
    }

    std::sort(prefixEntries_.begin(), prefixEntries_.end(),
              [](const PrefixEntry &lhs, const PrefixEntry &rhs) {
                  return std::tie(lhs.code, lhs.order) <
                         std::tie(rhs.code, rhs.order);
              });

    buildCodeIndexes();
}

void BoshiamyDictionary::buildCodeIndexes() {
    shortestCodes_.clear();
    allCodes_.clear();

    std::unordered_map<std::string, std::vector<std::string>> firstCandCodes;
    std::unordered_map<std::string, std::vector<std::string>> allCandCodes;

    for (const auto &[code, words] : exactMap_) {
        if (words.empty()) {
            continue;
        }
        firstCandCodes[words.front()].push_back(code);
        for (const auto &word : words) {
            allCandCodes[word].push_back(code);
        }
    }

    for (auto &[word, rawCodes] : allCandCodes) {
        auto firstIt = firstCandCodes.find(word);
        const bool hasFirst =
            firstIt != firstCandCodes.end() && !firstIt->second.empty();
        const auto &pool = hasFirst ? firstIt->second : rawCodes;

        size_t minLen = std::numeric_limits<size_t>::max();
        for (const auto &c : pool) {
            minLen = std::min(minLen, c.size());
        }

        auto &shortestOut = shortestCodes_[word];
        for (const auto &c : pool) {
            if (c.size() == minLen) {
                shortestOut.push_back(c);
            }
        }
        std::sort(shortestOut.begin(), shortestOut.end());
        shortestOut.erase(std::unique(shortestOut.begin(), shortestOut.end()),
                          shortestOut.end());

        std::unordered_set<std::string> firstSet;
        if (hasFirst) {
            firstSet.insert(firstIt->second.begin(), firstIt->second.end());
        }
        std::sort(rawCodes.begin(), rawCodes.end());
        rawCodes.erase(std::unique(rawCodes.begin(), rawCodes.end()),
                       rawCodes.end());
        std::sort(rawCodes.begin(), rawCodes.end(),
                  [&firstSet](const std::string &lhs, const std::string &rhs) {
                      const bool lFirst = firstSet.count(lhs) > 0;
                      const bool rFirst = firstSet.count(rhs) > 0;
                      if (lFirst != rFirst) {
                          return lFirst > rFirst;
                      }
                      if (lhs.size() != rhs.size()) {
                          return lhs.size() < rhs.size();
                      }
                      return lhs < rhs;
                  });
        allCodes_[word] = std::move(rawCodes);
    }
}

bool BoshiamyDictionary::loadChewingCsv(std::istream &in) {
    wordToBopomofo_.clear();
    bopomofoToWords_.clear();

    struct PrimaryCandidate {
        size_t rank;
        size_t lineIdx;
        std::string bopomofo;
    };
    std::unordered_map<std::string, PrimaryCandidate> bestRank;

    std::string rawLine;
    size_t lineIdx = 0;
    while (std::getline(in, rawLine)) {
        std::string_view line = trimLine(rawLine);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const size_t c1 = line.find(',');
        if (c1 == std::string_view::npos) {
            continue;
        }
        const size_t c2 = line.find(',', c1 + 1);
        if (c2 == std::string_view::npos) {
            continue;
        }

        std::string_view word = trimLine(line.substr(0, c1));
        std::string_view bopomofo = trimLine(line.substr(c2 + 1));
        if (word.empty() || bopomofo.empty()) {
            continue;
        }

        std::string wordStr(word);
        std::string bopoStr(bopomofo);
        auto &wordList = bopomofoToWords_[bopoStr];
        if (std::find(wordList.begin(), wordList.end(), wordStr) ==
            wordList.end()) {
            wordList.push_back(wordStr);
        }
        const size_t rank = wordList.size() - 1;

        auto it = bestRank.find(wordStr);
        if (it == bestRank.end() ||
            std::tie(rank, lineIdx) <
                std::tie(it->second.rank, it->second.lineIdx)) {
            bestRank[std::move(wordStr)] =
                PrimaryCandidate{rank, lineIdx, std::move(bopoStr)};
        }
        ++lineIdx;
    }

    for (auto &[word, cand] : bestRank) {
        wordToBopomofo_.emplace(word, std::move(cand.bopomofo));
    }
    return !bopomofoToWords_.empty();
}

std::vector<CandidateEntry>
BoshiamyDictionary::lookupDirect(std::string_view code, bool exactMatch) const {
    std::vector<CandidateEntry> result;
    if (code.empty()) {
        return result;
    }

    std::unordered_set<std::string> seenWords;
    if (auto it = exactMap_.find(std::string(code)); it != exactMap_.end()) {
        for (const auto &word : it->second) {
            if (seenWords.insert(word).second) {
                result.push_back(
                    CandidateEntry{word, std::string(code), true});
            }
        }
    }

    if (exactMatch) {
        return result;
    }

    auto lower = std::lower_bound(
        prefixEntries_.begin(), prefixEntries_.end(), code,
        [](const PrefixEntry &entry, std::string_view target) {
            return entry.code < target;
        });

    std::vector<const PrefixEntry *> prefixMatches;
    for (auto it = lower; it != prefixEntries_.end(); ++it) {
        if (!it->code.starts_with(code)) {
            break;
        }
        if (it->code.size() > code.size()) {
            prefixMatches.push_back(&(*it));
        }
    }

    std::sort(prefixMatches.begin(), prefixMatches.end(),
              [](const PrefixEntry *lhs, const PrefixEntry *rhs) {
                  const auto lLen = lhs->code.size();
                  const auto rLen = rhs->code.size();
                  return std::tie(lLen, lhs->order) <
                         std::tie(rLen, rhs->order);
              });

    for (const PrefixEntry *entry : prefixMatches) {
        if (result.size() >= kMaxLookupResults) {
            break;
        }
        if (seenWords.insert(entry->word).second) {
            result.push_back(
                CandidateEntry{entry->word, entry->code, false});
        }
    }

    return result;
}

std::vector<CandidateEntry> BoshiamyDictionary::lookup(std::string_view code,
                                                       bool exactMatch) const {
    auto result = lookupDirect(code, exactMatch);
    if (!result.empty() || code.size() < 2) {
        return result;
    }

    const int idx = vrsfSelectionIndex(code.back());
    if (idx < 0) {
        return result;
    }

    auto base = lookupDirect(code.substr(0, code.size() - 1), exactMatch);
    if (static_cast<size_t>(idx) < base.size()) {
        result.push_back(std::move(base[idx]));
    }
    return result;
}

std::vector<WildcardEntry>
BoshiamyDictionary::lookupWildcardDirect(std::string_view pattern) const {
    std::vector<WildcardEntry> result;
    if (pattern.empty()) {
        return result;
    }

    // prefixEntries_ is sorted by code, so only scan the range sharing the
    // literal prefix in front of the first wildcard.
    const std::string_view literal =
        pattern.substr(0, std::min(pattern.find_first_of("?*"), pattern.size()));
    auto begin = std::lower_bound(
        prefixEntries_.begin(), prefixEntries_.end(), literal,
        [](const PrefixEntry &entry, std::string_view target) {
            return entry.code < target;
        });

    std::vector<const PrefixEntry *> matches;
    for (auto it = begin; it != prefixEntries_.end(); ++it) {
        if (!it->code.starts_with(literal)) {
            break;
        }
        if (matchWildcard(pattern, it->code)) {
            matches.push_back(&(*it));
        }
    }

    std::sort(matches.begin(), matches.end(),
              [](const PrefixEntry *lhs, const PrefixEntry *rhs) {
                  const auto lLen = lhs->code.size();
                  const auto rLen = rhs->code.size();
                  return std::tie(lLen, lhs->order) <
                         std::tie(rLen, rhs->order);
              });

    std::unordered_set<std::string> seenWords;
    for (const PrefixEntry *entry : matches) {
        if (result.size() >= kMaxLookupResults) {
            break;
        }
        if (seenWords.insert(entry->word).second) {
            result.push_back(WildcardEntry{
                entry->word, entry->code, formatShortestCodes(entry->word)});
        }
    }
    return result;
}

std::vector<WildcardEntry>
BoshiamyDictionary::lookupWildcard(std::string_view pattern) const {
    auto result = lookupWildcardDirect(pattern);
    if (!result.empty() || pattern.size() < 2) {
        return result;
    }

    const int idx = vrsfSelectionIndex(pattern.back());
    if (idx < 0) {
        return result;
    }

    const std::string_view basePattern = pattern.substr(0, pattern.size() - 1);
    if (!hasWildcard(basePattern)) {
        return result;
    }

    auto base = lookupWildcardDirect(basePattern);
    if (static_cast<size_t>(idx) < base.size()) {
        result.push_back(std::move(base[idx]));
    }
    return result;
}

HomophoneResult
BoshiamyDictionary::lookupHomophonesDirect(std::string_view code) const {
    HomophoneResult result;
    if (code.empty()) {
        return result;
    }

    auto exactIt = exactMap_.find(std::string(code));
    if (exactIt == exactMap_.end()) {
        return result;
    }

    std::unordered_set<std::string> seenBopomofos;
    std::unordered_set<std::string> seenWords;

    for (const auto &baseWord : exactIt->second) {
        auto bopoIt = wordToBopomofo_.find(baseWord);
        if (bopoIt == wordToBopomofo_.end()) {
            if (seenWords.insert(baseWord).second) {
                result.entries.push_back(HomophoneEntry{
                    baseWord, "", formatShortestCodes(baseWord)});
            }
            continue;
        }

        const std::string &bopomofo = bopoIt->second;
        result.basePronunciations.emplace_back(baseWord, bopomofo);

        if (!seenBopomofos.insert(bopomofo).second) {
            continue;
        }

        auto wordsIt = bopomofoToWords_.find(bopomofo);
        if (wordsIt == bopomofoToWords_.end()) {
            continue;
        }
        for (const auto &homophone : wordsIt->second) {
            if (seenWords.insert(homophone).second) {
                result.entries.push_back(HomophoneEntry{
                    homophone, bopomofo, formatShortestCodes(homophone)});
            }
        }
    }

    return result;
}

HomophoneResult
BoshiamyDictionary::lookupHomophones(std::string_view code) const {
    auto result = lookupHomophonesDirect(code);
    if (!result.entries.empty() || code.size() < 2) {
        return result;
    }

    const int idx = vrsfSelectionIndex(code.back());
    if (idx < 0) {
        return result;
    }

    auto base = lookupHomophonesDirect(code.substr(0, code.size() - 1));
    if (static_cast<size_t>(idx) >= base.entries.size()) {
        return {};
    }

    HomophoneEntry selected = std::move(base.entries[idx]);
    base.entries.clear();
    base.entries.push_back(std::move(selected));
    return base;
}

std::string BoshiamyDictionary::toSimplified(std::string_view text) const {
    if (text.empty() || t2sMap_.empty()) {
        return std::string(text);
    }
    if (auto it = t2sMap_.find(std::string(text)); it != t2sMap_.end()) {
        return it->second;
    }

    std::string out;
    out.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t len = utf8CharByteLen(static_cast<unsigned char>(text[pos]));
        len = std::min(len, text.size() - pos);
        const std::string_view ch = text.substr(pos, len);
        if (auto it = t2sMap_.find(std::string(ch)); it != t2sMap_.end()) {
            out += it->second;
        } else {
            out.append(ch);
        }
        pos += len;
    }
    return out;
}

const std::vector<std::string> &
BoshiamyDictionary::shortestCodes(const std::string &word) const {
    static const std::vector<std::string> empty;
    auto it = shortestCodes_.find(word);
    if (it == shortestCodes_.end()) {
        return empty;
    }
    return it->second;
}

std::string
BoshiamyDictionary::formatShortestCodes(const std::string &word) const {
    return joinCodes(shortestCodes(word));
}

const std::vector<std::string> &
BoshiamyDictionary::allCodes(const std::string &word) const {
    static const std::vector<std::string> empty;
    auto it = allCodes_.find(word);
    if (it == allCodes_.end()) {
        return empty;
    }
    return it->second;
}

std::string BoshiamyDictionary::formatAllCodes(const std::string &word) const {
    return joinCodes(allCodes(word));
}

} // namespace fcitx
