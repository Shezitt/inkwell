// SPDX-License-Identifier: MIT
#include "MarkdownHighlighter.h"

#include <QFontDatabase>
#include <QTextDocument>

namespace inkwell {

namespace {

using markdown::Style;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(float(alpha));
    return color;
}

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    updateFormats();
}

MarkdownHighlighter::MarkdownHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
    updateFormats();
}

void MarkdownHighlighter::setPalette(const QPalette &palette)
{
    m_palette = palette;
    updateFormats();
    rehighlight();
}

QTextCharFormat MarkdownHighlighter::formatFor(markdown::Style style) const
{
    return m_formats.at(size_t(style));
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    // Heading sizes are relative to the document's font, which can change.
    if (const qreal size = document()->defaultFont().pointSizeF(); size != m_basePointSize) {
        m_basePointSize = size;
        updateFormats();
    }

    const auto previous = previousBlockState() < 0 ? markdown::LineState::Normal
                                                   : markdown::LineState(previousBlockState());
    QList<markdown::Span> spans;
    setCurrentBlockState(int(markdown::highlightLine(text, previous, spans)));

    // Spans nest (bold inside a heading): merge each into what is already there.
    for (const markdown::Span &span : std::as_const(spans)) {
        const QTextCharFormat &style = m_formats.at(size_t(span.style));
        const auto end = int(span.start + span.length);
        for (auto run = int(span.start); run < end;) {
            const QTextCharFormat current = format(run);
            int runEnd = run + 1;
            while (runEnd < end && format(runEnd) == current)
                ++runEnd;
            QTextCharFormat merged = current;
            merged.merge(style);
            setFormat(run, runEnd - run, merged);
            run = runEnd;
        }
    }
}

void MarkdownHighlighter::updateFormats()
{
    const QColor text = m_palette.color(QPalette::Text);
    const QColor link = m_palette.color(QPalette::Link);
    const QStringList fixedFamilies {QFontDatabase::systemFont(QFontDatabase::FixedFont).family()};
    const auto format = [this](Style style) -> QTextCharFormat & {
        m_formats.at(size_t(style)) = {};
        return m_formats.at(size_t(style));
    };

    format(Style::Marker).setForeground(withAlpha(text, 0.4));
    format(Style::Rule).setForeground(withAlpha(text, 0.4));

    const auto heading = [&](Style style, qreal scale) {
        QTextCharFormat &f = format(style);
        f.setFontWeight(QFont::Bold);
        if (m_basePointSize > 0)
            f.setFontPointSize(m_basePointSize * scale);
    };
    heading(Style::Heading1, 1.6);
    heading(Style::Heading2, 1.35);
    heading(Style::Heading3, 1.15);
    heading(Style::Heading, 1.0);

    format(Style::Bold).setFontWeight(QFont::Bold);
    format(Style::Italic).setFontItalic(true);
    QTextCharFormat &strike = format(Style::Strikethrough);
    strike.setFontStrikeOut(true);
    strike.setForeground(withAlpha(text, 0.7));

    for (const Style style : {Style::Code, Style::CodeBlock}) {
        QTextCharFormat &f = format(style);
        f.setFontFamilies(fixedFamilies);
        f.setBackground(withAlpha(text, 0.08));
    }

    QTextCharFormat &quote = format(Style::Quote);
    quote.setFontItalic(true);
    quote.setForeground(withAlpha(text, 0.75));

    QTextCharFormat &listMarker = format(Style::ListMarker);
    listMarker.setForeground(link);
    listMarker.setFontWeight(QFont::Bold);

    QTextCharFormat &done = format(Style::TaskDone);
    done.setFontStrikeOut(true);
    done.setForeground(withAlpha(text, 0.5));

    QTextCharFormat &linkText = format(Style::LinkText);
    linkText.setForeground(link);
    linkText.setFontUnderline(true);
    QTextCharFormat &url = format(Style::Url);
    url.setForeground(withAlpha(link, 0.8));
    url.setFontUnderline(true);
}

} // namespace inkwell
