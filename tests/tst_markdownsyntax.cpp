// SPDX-License-Identifier: MIT
#include "core/MarkdownSyntax.h"

#include <QTest>

#include <tuple>

using namespace Qt::StringLiterals;
using namespace inkwell::markdown;

namespace {

/// The parts of @p line highlighted with @p style, in order.
QStringList styled(const QString &line, Style style, LineState state = LineState::Normal)
{
    QList<Span> spans;
    std::ignore = highlightLine(line, state, spans);
    QStringList parts;
    for (const Span &span : spans) {
        if (span.style == style)
            parts.append(line.mid(span.start, span.length));
    }
    return parts;
}

LineState stateAfter(const QStringList &lines)
{
    LineState state = LineState::Normal;
    for (const QString &line : lines) {
        QList<Span> spans;
        state = highlightLine(line, state, spans);
    }
    return state;
}

} // namespace

namespace inkwell::markdown {

/// How QCOMPARE prints a link that differs.
char *toString(const Link &link)
{
    return QTest::toString(u"Link(%1, %2, %3)"_s.arg(link.start).arg(link.length).arg(link.target));
}

} // namespace inkwell::markdown

class TestMarkdownSyntax : public QObject
{
    Q_OBJECT

private slots:
    void headings()
    {
        QCOMPARE(styled(u"# Title"_s, Style::Heading1), QStringList {u"# Title"_s});
        QCOMPARE(styled(u"# Title"_s, Style::Marker), QStringList {u"#"_s});
        QCOMPARE(styled(u"## Two"_s, Style::Heading2).size(), 1);
        QCOMPARE(styled(u"### Three ###"_s, Style::Marker), (QStringList {u"###"_s, u" ###"_s}));
        QCOMPARE(styled(u"#### Four"_s, Style::Heading).size(), 1);
        QCOMPARE(styled(u"## **Big** idea"_s, Style::Bold), QStringList {u"Big"_s});

        QVERIFY(styled(u"#hashtag"_s, Style::Heading1).isEmpty()); // needs a space
        QVERIFY(styled(u"####### seven"_s, Style::Heading).isEmpty());
        QVERIFY(styled(u"    # indented code"_s, Style::Heading1).isEmpty());
    }

    void emphasis()
    {
        const QString line = u"a **bold** and *it* and _it_ and ~~gone~~ and __also__"_s;
        QCOMPARE(styled(line, Style::Bold), (QStringList {u"bold"_s, u"also"_s}));
        QCOMPARE(styled(line, Style::Italic), (QStringList {u"it"_s, u"it"_s}));
        QCOMPARE(styled(line, Style::Strikethrough), QStringList {u"gone"_s});
        QCOMPARE(styled(line, Style::Marker).size(), 10);
        QCOMPARE(styled(u"***both***"_s, Style::Bold), QStringList {u"both"_s});
        QCOMPARE(styled(u"***both***"_s, Style::Italic), QStringList {u"both"_s});
        QCOMPARE(styled(u"***both***"_s, Style::Marker), (QStringList {u"***"_s, u"***"_s}));
    }

    void leavesLookalikesAlone()
    {
        QVERIFY(styled(u"snake_case_name and __init__.py"_s, Style::Italic).isEmpty());
        QVERIFY(styled(u"2 * 3 * 4"_s, Style::Italic).isEmpty());
        QVERIFY(styled(u"a ** b ** c"_s, Style::Bold).isEmpty());
        QVERIFY(styled(u"not \\*escaped\\*"_s, Style::Italic).isEmpty());
        QVERIFY(styled(u"~~ spaced ~~"_s, Style::Strikethrough).isEmpty());
    }

    void inlineCode()
    {
        QCOMPARE(styled(u"use `a*b*c` here"_s, Style::Code), QStringList {u"a*b*c"_s});
        QVERIFY(styled(u"use `a*b*c` here"_s, Style::Italic).isEmpty());
        QCOMPARE(styled(u"``a`b``"_s, Style::Code), QStringList {u"a`b"_s});
        QCOMPARE(styled(u"use `a*b*c` here"_s, Style::Marker), (QStringList {u"`"_s, u"`"_s}));
    }

    void linksAndUrls()
    {
        const QString link = u"see [the docs](https://x.y/a_b_c) now"_s;
        QCOMPARE(styled(link, Style::LinkText), QStringList {u"the docs"_s});
        QCOMPARE(styled(link, Style::Url), QStringList {u"https://x.y/a_b_c"_s});
        QCOMPARE(styled(link, Style::Marker), (QStringList {u"["_s, u"]("_s, u")"_s}));
        QVERIFY(styled(link, Style::Italic).isEmpty());

        QCOMPARE(styled(u"go to https://example.com/a_b. Then"_s, Style::Url),
            QStringList {u"https://example.com/a_b"_s});
        QCOMPARE(
            styled(u"(https://example.com)"_s, Style::Url), QStringList {u"https://example.com"_s});
        QVERIFY(styled(u"go to https://example.com/a_b_c"_s, Style::Italic).isEmpty());
    }

    void lists()
    {
        QCOMPARE(styled(u"- item"_s, Style::ListMarker), QStringList {u"-"_s});
        QCOMPARE(styled(u"  * nested"_s, Style::ListMarker), QStringList {u"*"_s});
        QCOMPARE(styled(u"12. twelfth"_s, Style::ListMarker), QStringList {u"12."_s});
        QCOMPARE(styled(u"- *it*"_s, Style::Italic), QStringList {u"it"_s});
        QVERIFY(styled(u"-dash"_s, Style::ListMarker).isEmpty());
        QVERIFY(styled(u"3.14 is pi"_s, Style::ListMarker).isEmpty());
    }

    void tasks()
    {
        QCOMPARE(styled(u"- [ ] todo"_s, Style::Marker), QStringList {u"[ ]"_s});
        QVERIFY(styled(u"- [ ] todo"_s, Style::TaskDone).isEmpty());
        QCOMPARE(styled(u"- [x] done **now**"_s, Style::TaskDone), QStringList {u"done **now**"_s});
        QCOMPARE(styled(u"- [X] done **now**"_s, Style::Bold), QStringList {u"now"_s});
    }

    void quotes()
    {
        QCOMPARE(styled(u"> quoted *x*"_s, Style::Marker), (QStringList {u"> "_s, u"*"_s, u"*"_s}));
        QCOMPARE(styled(u"> quoted *x*"_s, Style::Quote), QStringList {u"quoted *x*"_s});
        QCOMPARE(styled(u"> quoted *x*"_s, Style::Italic), QStringList {u"x"_s});
        QCOMPARE(styled(u"> > - deep"_s, Style::ListMarker), QStringList {u"-"_s});
    }

    void rules()
    {
        for (const QString &rule : {u"---"_s, u"* * *"_s, u"___"_s, u"  -----  "_s})
            QCOMPARE(styled(rule, Style::Rule), QStringList {rule});
        QVERIFY(styled(u"--"_s, Style::Rule).isEmpty());
        QVERIFY(styled(u"-- a"_s, Style::Rule).isEmpty());
    }

    void fencedCodeBlocks()
    {
        QCOMPARE(stateAfter({u"```cpp"_s}), LineState::InBacktickFence);
        QCOMPARE(styled(u"```cpp"_s, Style::Marker), QStringList {u"```cpp"_s});
        // Inside: no Markdown, just code.
        QCOMPARE(styled(u"int *p = **q; // # no"_s, Style::CodeBlock, LineState::InBacktickFence),
            QStringList {u"int *p = **q; // # no"_s});
        QVERIFY(styled(u"**q**"_s, Style::Bold, LineState::InBacktickFence).isEmpty());
        // Only the same kind of fence closes it.
        QCOMPARE(stateAfter({u"```"_s, u"~~~"_s}), LineState::InBacktickFence);
        QCOMPARE(stateAfter({u"```"_s, u"code"_s, u"```"_s}), LineState::Normal);
        QCOMPARE(stateAfter({u"~~~"_s, u"```"_s, u"~~~~"_s}), LineState::Normal);
        // A closing fence has nothing after it.
        QCOMPARE(stateAfter({u"```"_s, u"``` not closing"_s}), LineState::InBacktickFence);
        // A backtick fence's info string cannot contain backticks: inline code instead.
        QCOMPARE(stateAfter({u"```a`b"_s}), LineState::Normal);
    }

    void hideableSyntax()
    {
        QCOMPARE(styled(u"## Two ##"_s, Style::Hideable), (QStringList {u"## "_s, u" ##"_s}));
        QCOMPARE(styled(u"#\tTab"_s, Style::Hideable), QStringList {u"#\t"_s});
        QVERIFY(styled(u"##"_s, Style::Hideable).isEmpty()); // nothing would be left
        QCOMPARE(styled(u"a **b** *c* ~~d~~ `e` ***f***"_s, Style::Hideable),
            (QStringList {u"`"_s, u"`"_s, u"~~"_s, u"~~"_s, u"***"_s, u"***"_s, u"**"_s, u"**"_s,
                u"*"_s, u"*"_s}));
        QCOMPARE(styled(u"see [the docs](https://x.y \"Title\") now"_s, Style::Hideable),
            (QStringList {u"["_s, u"](https://x.y \"Title\")"_s}));

        // A task's bullet goes, its checkbox standing for it; a quote's >s
        // are drawn as bars, beside the text, so the spaces around them go.
        QCOMPARE(styled(u"  -\t[ ] task"_s, Style::Hideable), QStringList {u"-\t"_s});
        QCOMPARE(styled(u" > >\tdeep"_s, Style::Hideable), (QStringList {u" "_s, u" "_s, u"\t"_s}));
        QVERIFY(styled(u">>"_s, Style::Hideable).isEmpty());

        // Bare URLs stay, and so do the numbers of lists and code fences.
        for (const QString &line : {u"go to https://example.com"_s, u"1. [ ] item"_s, u"```cpp"_s,
                 u"---"_s, u"1. first"_s}) {
            QVERIFY2(styled(line, Style::Hideable).isEmpty(), qPrintable(line));
        }
        QVERIFY(styled(u"*x*"_s, Style::Hideable, LineState::InBacktickFence).isEmpty());
    }

    void links()
    {
        const QString line = u"see [the docs](https://x.y/a) or http://b.c, `http://code`"_s;
        QCOMPARE(findLinks(line, LineState::Normal),
            (QList<Link> {{4, 25, u"https://x.y/a"_s}, {33, 10, u"http://b.c"_s}}));
        QCOMPARE(findLinks(u"[a](x \"title\") and [b](y)"_s, LineState::Normal),
            (QList<Link> {{0, 14, u"x"_s}, {19, 6, u"y"_s}}));
        QCOMPARE(findLinks(u"# [Home](https://h.i) #"_s, LineState::Normal),
            (QList<Link> {{2, 19, u"https://h.i"_s}}));
        QCOMPARE(findLinks(u"> - [ ] read https://a.b"_s, LineState::Normal),
            (QList<Link> {{13, 11, u"https://a.b"_s}}));
        QVERIFY(findLinks(u"[a](https://x.y)"_s, LineState::InBacktickFence).isEmpty());
        QCOMPARE(findLinks(u"http://a.b [x](y)"_s, LineState::Normal),
            (QList<Link> {{0, 10, u"http://a.b"_s}, {11, 6, u"y"_s}}));
        QVERIFY(findLinks(u"no links [here] (x)"_s, LineState::Normal).isEmpty());
    }

    void linkUrl_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QUrl>("url");
        QTest::newRow("https") << u"https://example.com/a?b=c#d"_s
                               << QUrl(u"https://example.com/a?b=c#d"_s);
        QTest::newRow("http") << u"http://example.com"_s << QUrl(u"http://example.com"_s);
        QTest::newRow("ftp") << u"ftp://files.example.com/x"_s
                             << QUrl(u"ftp://files.example.com/x"_s);
        QTest::newRow("upper case") << u"HTTPS://Example.com"_s << QUrl(u"https://example.com"_s);
        QTest::newRow("www") << u"www.example.com/x"_s << QUrl(u"https://www.example.com/x"_s);
        QTest::newRow("mail") << u"mailto:me@example.com"_s << QUrl(u"mailto:me@example.com"_s);
        QTest::newRow("spaces around") << u" https://a.b "_s << QUrl(u"https://a.b"_s);
        QTest::newRow("relative") << u"notes/todo.md"_s << QUrl();
        QTest::newRow("anchor") << u"#heading"_s << QUrl();
        QTest::newRow("bare domain") << u"example.com"_s << QUrl();
        QTest::newRow("file") << u"file:///usr/bin/rm"_s << QUrl();
        QTest::newRow("file on a host") << u"file://localhost/usr/bin/rm"_s << QUrl();
        QTest::newRow("other scheme") << u"ssh://example.com"_s << QUrl();
        QTest::newRow("javascript") << u"javascript:alert(1)"_s << QUrl();
        QTest::newRow("no host") << u"https:///path"_s << QUrl();
        QTest::newRow("empty mail") << u"mailto:"_s << QUrl();
        QTest::newRow("empty") << QString() << QUrl();
    }

    void linkUrl()
    {
        QFETCH(QString, target);
        QFETCH(QUrl, url);
        QCOMPARE(inkwell::markdown::linkUrl(target), url);
    }

    void symbols()
    {
        QCOMPARE(styled(u"- item"_s, Style::BulletSymbol), QStringList {u"-"_s});
        QCOMPARE(styled(u"  + *it*"_s, Style::BulletSymbol), QStringList {u"+"_s});
        QVERIFY(styled(u"1. first"_s, Style::BulletSymbol).isEmpty()); // numbers stay

        // A task's checkbox replaces its bullet.
        QCOMPARE(styled(u"- [ ] todo"_s, Style::CheckboxSymbol), QStringList {u"[ ]"_s});
        QVERIFY(styled(u"- [ ] todo"_s, Style::BulletSymbol).isEmpty());
        QVERIFY(styled(u"- [ ] todo"_s, Style::CheckedBoxSymbol).isEmpty());
        QCOMPARE(styled(u"* [X] done"_s, Style::CheckedBoxSymbol), QStringList {u"[X]"_s});
        QCOMPARE(styled(u"2. [x] done"_s, Style::CheckedBoxSymbol), QStringList {u"[x]"_s});
        QCOMPARE(styled(u"- [y] no"_s, Style::BulletSymbol), QStringList {u"-"_s});
        QVERIFY(styled(u"- [y] no"_s, Style::CheckboxSymbol).isEmpty());

        // Each > of nested quotes gets a bar.
        QCOMPARE(styled(u"> > - deep"_s, Style::QuoteBarSymbol), (QStringList {u">"_s, u">"_s}));
        QCOMPARE(styled(u"> > - deep"_s, Style::BulletSymbol), QStringList {u"-"_s});
        QCOMPARE(styled(u">"_s, Style::QuoteBarSymbol), QStringList {u">"_s});

        QCOMPARE(styled(u" * * *"_s, Style::RuleSymbol), QStringList {u" * * *"_s});
        QVERIFY(styled(u"- * -"_s, Style::RuleSymbol).isEmpty());

        // Nothing in code.
        for (const Style style : {Style::BulletSymbol, Style::QuoteBarSymbol, Style::RuleSymbol})
            QVERIFY(styled(u"> - ---"_s, style, LineState::InTildeFence).isEmpty());
    }

    void plainTextHasNoSpans()
    {
        QList<Span> spans;
        QCOMPARE(highlightLine(u"Just a sentence, nothing special."_s, LineState::Normal, spans),
            LineState::Normal);
        QVERIFY(spans.isEmpty());
        QCOMPARE(highlightLine(u""_s, LineState::Normal, spans), LineState::Normal);
        QVERIFY(spans.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMarkdownSyntax)
#include "tst_markdownsyntax.moc"
