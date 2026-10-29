// SPDX-License-Identifier: MIT
#include "MarkdownSyntax.h"

#include <QRegularExpression>
#include <QString>

using namespace Qt::StringLiterals;

namespace inkwell::markdown {

namespace {

/// Stands in for characters that must not be parsed again, such as code.
constexpr QChar kMasked = u'\x01';

struct Patterns
{
    QRegularExpression fence {u"^ {0,3}(`{3,}|~{3,})(.*)$"_s};
    QRegularExpression heading {u"^ {0,3}(#{1,6})(?:[ \\t]+(.*?))?(?:[ \\t]+#+)?[ \\t]*$"_s};
    QRegularExpression rule {u"^ {0,3}([-*_])(?:[ \\t]*\\1){2,}[ \\t]*$"_s};
    QRegularExpression quote {u"^ {0,3}(?:>[ \\t]?)+"_s};
    QRegularExpression listItem {u"^[ \\t]*([-*+]|\\d{1,9}[.)])[ \\t]+"_s};
    QRegularExpression task {u"^\\[([ xX])\\][ \\t]+"_s};
    QRegularExpression codeSpan {u"(`+)(?!`)(.+?)(?<!`)\\1(?!`)"_s};
    QRegularExpression link {
        u"(\\[)([^\\]\\x01]+)(\\]\\()([^)\\s\\x01]+)(?:[ \\t]+\"[^\"]*\")?(\\))"_s};
    QRegularExpression url {u"(?:https?|ftp)://[^\\s<>\\x01]*[^\\s<>\\x01.,;:!?'\")\\]]"_s};
    QRegularExpression strike {u"(~~)(?=\\S)(.+?)(?<=\\S)(~~)"_s};
    QRegularExpression boldItalic {u"(\\*\\*\\*|___)(?=\\S)(.+?)(?<=\\S)(\\1)"_s};
    QRegularExpression bold {u"(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)(\\1)"_s};
    // A single * or _, not part of ** and, for _, not inside a word (snake_case).
    QRegularExpression italicStar {
        u"(?<![*\\\\])(\\*)(?![*\\s])(.+?)(?<![*\\s\\\\])(\\*)(?!\\*)"_s};
    QRegularExpression italicUnderscore {
        u"(?<![\\w_])(_)(?![_\\s])(.+?)(?<![_\\s])(_)(?![\\w_])"_s};
};

const Patterns &patterns()
{
    static const Patterns instance;
    return instance;
}

void add(QList<Span> &spans, qsizetype start, qsizetype length, Style style)
{
    if (length > 0)
        spans.append({start, length, style});
}

void mask(QString &text, qsizetype start, qsizetype length)
{
    for (qsizetype i = start; i < start + length; ++i)
        text[i] = kMasked;
}

/// Emphasis, code, links and URLs within line[from, to).
void highlightInline(QStringView line, qsizetype from, qsizetype to, QList<Span> &spans)
{
    const Patterns &p = patterns();
    // Work on a copy in which parsed code and URLs are masked, so that
    // emphasis characters inside them are left alone.
    QString text(to, u' ');
    for (qsizetype i = from; i < to; ++i)
        text[i] = line[i];
    for (qsizetype i = 0; i < from; ++i)
        text[i] = kMasked;

    for (auto it = p.codeSpan.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        add(spans, m.capturedStart(1), m.capturedLength(1), Style::Marker);
        add(spans, m.capturedStart(2), m.capturedLength(2), Style::Code);
        add(spans, m.capturedEnd(2), m.capturedLength(1), Style::Marker);
        mask(text, m.capturedStart(), m.capturedLength());
    }
    for (auto it = p.link.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        add(spans, m.capturedStart(1), 1, Style::Marker);
        add(spans, m.capturedStart(2), m.capturedLength(2), Style::LinkText);
        add(spans, m.capturedStart(3), 2, Style::Marker);
        add(spans, m.capturedStart(4), m.capturedLength(4), Style::Url);
        add(spans, m.capturedStart(5), 1, Style::Marker);
        mask(text, m.capturedStart(3), m.capturedEnd() - m.capturedStart(3));
    }
    for (auto it = p.url.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        add(spans, m.capturedStart(), m.capturedLength(), Style::Url);
        mask(text, m.capturedStart(), m.capturedLength());
    }
    const auto emphasis = [&](const QRegularExpression &re, Style style) {
        for (auto it = re.globalMatch(text, from); it.hasNext();) {
            const auto m = it.next();
            add(spans, m.capturedStart(1), m.capturedLength(1), Style::Marker);
            add(spans, m.capturedStart(2), m.capturedLength(2), style);
            add(spans, m.capturedStart(3), m.capturedLength(3), Style::Marker);
        }
    };
    emphasis(p.strike, Style::Strikethrough);
    // ***text*** is bold and italic; its markers must not be read again as ** or *.
    for (auto it = p.boldItalic.globalMatch(text, from); it.hasNext();) {
        const auto m = it.next();
        add(spans, m.capturedStart(1), 3, Style::Marker);
        add(spans, m.capturedStart(2), m.capturedLength(2), Style::Bold);
        add(spans, m.capturedStart(2), m.capturedLength(2), Style::Italic);
        add(spans, m.capturedStart(3), 3, Style::Marker);
        mask(text, m.capturedStart(1), 3);
        mask(text, m.capturedStart(3), 3);
    }
    emphasis(p.bold, Style::Bold);
    emphasis(p.italicStar, Style::Italic);
    emphasis(p.italicUnderscore, Style::Italic);
}

} // namespace

LineState highlightLine(QStringView line, LineState previous, QList<Span> &spans)
{
    const Patterns &p = patterns();
    const QString text = line.toString();
    const qsizetype length = text.size();

    if (previous != LineState::Normal) {
        const QChar fenceChar = previous == LineState::InBacktickFence ? u'`' : u'~';
        const auto m = p.fence.match(text);
        if (m.hasMatch() && m.captured(1).front() == fenceChar
            && m.captured(2).trimmed().isEmpty()) {
            add(spans, 0, length, Style::Marker);
            return LineState::Normal;
        }
        add(spans, 0, length, Style::CodeBlock);
        return previous;
    }

    if (const auto m = p.fence.match(text); m.hasMatch()) {
        const bool backticks = m.captured(1).front() == u'`';
        // A backtick fence's info string cannot contain backticks.
        if (!backticks || !m.captured(2).contains(u'`')) {
            add(spans, 0, length, Style::Marker);
            return backticks ? LineState::InBacktickFence : LineState::InTildeFence;
        }
    }

    if (const auto m = p.heading.match(text); m.hasMatch()) {
        const qsizetype level = m.capturedLength(1);
        const Style style = level == 1 ? Style::Heading1
            : level == 2               ? Style::Heading2
            : level == 3               ? Style::Heading3
                                       : Style::Heading;
        add(spans, 0, length, style);
        add(spans, m.capturedStart(1), level, Style::Marker);
        if (m.capturedLength(2) > 0) {
            if (m.capturedEnd(2) < length)
                add(spans, m.capturedEnd(2), length - m.capturedEnd(2), Style::Marker);
            highlightInline(line, m.capturedStart(2), m.capturedEnd(2), spans);
        }
        return LineState::Normal;
    }

    if (p.rule.match(text).hasMatch()) {
        add(spans, 0, length, Style::Rule);
        return LineState::Normal;
    }

    qsizetype contentStart = 0;
    bool quoted = false;
    if (const auto m = p.quote.match(text); m.hasMatch()) {
        add(spans, 0, m.capturedEnd(), Style::Marker);
        contentStart = m.capturedEnd();
        quoted = true;
    }

    if (const auto m = p.listItem.match(text.sliced(contentStart)); m.hasMatch()) {
        add(spans, contentStart + m.capturedStart(1), m.capturedLength(1), Style::ListMarker);
        contentStart += m.capturedEnd();
        if (const auto task = p.task.match(text.sliced(contentStart)); task.hasMatch()) {
            add(spans, contentStart, 3, Style::Marker);
            const bool done = task.captured(1) != u" "_s;
            contentStart += task.capturedEnd();
            if (done)
                add(spans, contentStart, length - contentStart, Style::TaskDone);
        }
    }

    if (quoted)
        add(spans, contentStart, length - contentStart, Style::Quote);
    highlightInline(line, contentStart, length, spans);
    return LineState::Normal;
}

} // namespace inkwell::markdown
