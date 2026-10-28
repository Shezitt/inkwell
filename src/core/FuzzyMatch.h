// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QStringView>

#include <optional>

namespace inkwell {

struct FuzzyMatch
{
    /// Higher is better. Only comparable between matches of the same pattern.
    int score = 0;
    /// Indexes into the text of the characters that matched, ascending.
    QList<qsizetype> positions;
};

/// Matches @p pattern against @p text the way quick-open boxes do: every
/// character of the pattern must appear in the text, in order but not
/// necessarily adjacent, ignoring case. Whitespace in the pattern is ignored.
///
/// Among all ways to match, the best-scoring one is returned. Matches at the
/// start of words and runs of adjacent characters score higher, so "fs"
/// prefers "Feedback for Sav" over "offset". An empty pattern matches
/// everything with score 0. Returns std::nullopt if the text does not match.
[[nodiscard]] std::optional<FuzzyMatch> fuzzyMatch(QStringView pattern, QStringView text);

} // namespace inkwell
