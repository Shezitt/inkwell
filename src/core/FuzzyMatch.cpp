// SPDX-License-Identifier: MIT
#include "FuzzyMatch.h"

#include <limits>
#include <vector>

namespace inkwell {

namespace {

constexpr int kMatch = 1;
constexpr int kWordStart = 8;
constexpr int kTextStart = 4;
constexpr int kAdjacent = 6;
constexpr int kGap = -1;
constexpr int kImpossible = std::numeric_limits<int>::min() / 2;

bool isWordStart(QStringView text, qsizetype i)
{
    if (i == 0)
        return true;
    const QChar previous = text[i - 1];
    const QChar current = text[i];
    if (!previous.isLetterOrNumber())
        return current.isLetterOrNumber();
    // camelCase and letter-digit boundaries, as in "notesV2".
    return (previous.isLower() && current.isUpper()) || (previous.isLetter() && current.isDigit());
}

} // namespace

std::optional<FuzzyMatch> fuzzyMatch(QStringView pattern, QStringView text)
{
    QList<QChar> wanted;
    wanted.reserve(pattern.size());
    for (const QChar c : pattern) {
        if (!c.isSpace())
            wanted.append(c.toCaseFolded());
    }
    if (wanted.isEmpty())
        return FuzzyMatch {};

    const auto m = qsizetype(wanted.size());
    const qsizetype n = text.size();
    if (m > n)
        return std::nullopt;

    // Cheap rejection first: most texts do not contain the pattern at all.
    QList<QChar> folded;
    folded.reserve(n);
    for (const QChar c : text)
        folded.append(c.toCaseFolded());
    for (qsizetype i = 0, j = 0; i < m; ++i, ++j) {
        while (j < n && folded[j] != wanted[i])
            ++j;
        if (j == n)
            return std::nullopt;
    }

    // best[i][j]: best score with pattern char i matched at text index j.
    // from[i][j]: where pattern char i - 1 was matched on that best path.
    std::vector<int> best(size_t(m * n), kImpossible);
    std::vector<qsizetype> from(size_t(m * n), -1);
    const auto at = [n](qsizetype i, qsizetype j) { return size_t(i * n + j); };

    for (qsizetype j = 0; j < n; ++j) {
        if (folded[j] == wanted[0]) {
            best[at(0, j)]
                = kMatch + (isWordStart(text, j) ? kWordStart : 0) + (j == 0 ? kTextStart : 0);
        }
    }
    for (qsizetype i = 1; i < m; ++i) {
        // Best previous match strictly before j - 1, for non-adjacent steps.
        int runningBest = kImpossible;
        qsizetype runningFrom = -1;
        for (qsizetype j = i; j < n; ++j) {
            if (j >= 2 && best[at(i - 1, j - 2)] > runningBest) {
                runningBest = best[at(i - 1, j - 2)];
                runningFrom = j - 2;
            }
            if (folded[j] != wanted[i])
                continue;
            const int bonus = kMatch + (isWordStart(text, j) ? kWordStart : 0);
            const int adjacent = best[at(i - 1, j - 1)];
            int score = kImpossible;
            if (adjacent > kImpossible) {
                score = adjacent + bonus + kAdjacent;
                from[at(i, j)] = j - 1;
            }
            if (runningBest > kImpossible && runningBest + bonus + kGap > score) {
                score = runningBest + bonus + kGap;
                from[at(i, j)] = runningFrom;
            }
            best[at(i, j)] = score;
        }
    }

    qsizetype end = -1;
    for (qsizetype j = m - 1; j < n; ++j) {
        if (best[at(m - 1, j)] > kImpossible
            && (end < 0 || best[at(m - 1, j)] > best[at(m - 1, end)]))
            end = j;
    }
    if (end < 0)
        return std::nullopt;

    FuzzyMatch match;
    match.score = best[at(m - 1, end)];
    match.positions.resize(m);
    for (qsizetype i = m - 1, j = end; i >= 0; --i) {
        match.positions[i] = j;
        j = from[at(i, j)];
    }
    return match;
}

} // namespace inkwell
