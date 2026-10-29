// SPDX-License-Identifier: MIT
#pragma once

#include "core/MarkdownSyntax.h"

#include <QPalette>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <array>

namespace inkwell {

/// Styles Markdown in a plain-text document as it is typed: larger headings,
/// bold and italic text, code in a fixed-width font, and dimmed syntax
/// characters. The text itself is never changed.
///
/// Colours are derived from a palette, so highlighting follows light and dark
/// themes; call setPalette() when it changes.
class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /// Highlights @p document, which owns the highlighter.
    explicit MarkdownHighlighter(QTextDocument *document);
    /// Highlights nothing until setDocument() is called.
    explicit MarkdownHighlighter(QObject *parent);

    /// Derives the colours from @p palette and highlights the document again.
    void setPalette(const QPalette &palette);

    /// The format used for @p style, for tests.
    [[nodiscard]] QTextCharFormat formatFor(markdown::Style style) const;

protected:
    void highlightBlock(const QString &text) override;

private:
    void updateFormats();

    QPalette m_palette;
    qreal m_basePointSize = 0;
    std::array<QTextCharFormat, size_t(markdown::Style::Rule) + 1> m_formats;
};

} // namespace inkwell
