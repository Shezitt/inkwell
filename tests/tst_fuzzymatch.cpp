// SPDX-License-Identifier: MIT
#include "core/FuzzyMatch.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace inkwell;

namespace {

int score(const QString &pattern, const QString &text)
{
    const auto match = fuzzyMatch(pattern, text);
    return match ? match->score : -1000;
}

} // namespace

class TestFuzzyMatch : public QObject
{
    Q_OBJECT

private slots:
    void matchesSubsequencesIgnoringCase()
    {
        QVERIFY(fuzzyMatch(u"feed"_s, u"Feedback for Sav"_s));
        QVERIFY(fuzzyMatch(u"FFS"_s, u"Feedback for Sav"_s));
        QVERIFY(fuzzyMatch(u"fbsav"_s, u"Feedback for Sav"_s));
        QVERIFY(fuzzyMatch(u"été"_s, u"Notes d'Été"_s));
    }

    void rejectsNonMatches()
    {
        QVERIFY(!fuzzyMatch(u"xyz"_s, u"Feedback"_s));
        QVERIFY(!fuzzyMatch(u"kcabdeef"_s, u"Feedback"_s)); // order matters
        QVERIFY(!fuzzyMatch(u"feedbacks"_s, u"Feedback"_s));
        QVERIFY(!fuzzyMatch(u"a"_s, u""_s));
    }

    void emptyPatternMatchesEverything()
    {
        const auto match = fuzzyMatch(u""_s, u"Anything"_s);
        QVERIFY(match);
        QCOMPARE(match->score, 0);
        QVERIFY(match->positions.isEmpty());
        QVERIFY(fuzzyMatch(u"   "_s, u""_s));
    }

    void ignoresWhitespaceInPattern()
    {
        const auto match = fuzzyMatch(u"oct  feed"_s, u"Oct 27 - Feedback"_s);
        QVERIFY(match);
        QCOMPARE(match->positions, (QList<qsizetype> {0, 1, 2, 9, 10, 11, 12}));
    }

    void reportsBestPositions()
    {
        // "fs" could match the "f" of "offset"-like letters, but word starts win.
        const auto match = fuzzyMatch(u"fs"_s, u"Feedback for Sav"_s);
        QVERIFY(match);
        QCOMPARE(match->positions, (QList<qsizetype> {0, 13}));

        // Adjacent letters are preferred over scattered ones.
        const auto run = fuzzyMatch(u"bar"_s, u"bxaxr bar"_s);
        QVERIFY(run);
        QCOMPARE(run->positions, (QList<qsizetype> {6, 7, 8}));
    }

    void ranksBetterMatchesHigher()
    {
        // Word starts beat letters in the middle of words.
        QVERIFY(score(u"fs"_s, u"Feedback for Sav"_s) > score(u"fs"_s, u"offset"_s));
        // A prefix beats the same letters later in the text.
        QVERIFY(score(u"meet"_s, u"Meeting notes"_s) > score(u"meet"_s, u"Notes meeting"_s));
        // Contiguous beats scattered.
        QVERIFY(score(u"plan"_s, u"Plan"_s) > score(u"plan"_s, u"Please learn again"_s));
        // camelCase and digit boundaries count as word starts.
        QVERIFY(score(u"nv"_s, u"notesV2"_s) > score(u"nv"_s, u"nov"_s));
    }
};

QTEST_GUILESS_MAIN(TestFuzzyMatch)
#include "tst_fuzzymatch.moc"
