// SPDX-License-Identifier: MIT
#pragma once

#include "core/MarkdownSyntax.h"

#include <QPalette>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>

#include <array>

namespace inkwell {

/// Styles Markdown in a plain-text document as it is typed: larger headings,
/// bold and italic text, code in a fixed-width font, and dimmed syntax
/// characters. The text itself is never changed.
///
/// Colours are derived from a palette, so highlighting follows light and dark
/// themes; call setPalette() when it changes.
///
/// It can also hide syntax such as ** and the URL of a link, so that only
/// its effect shows, except in the block being edited (setRevealedBlock()).
/// Hidden characters take no space but stay in the text: they are copied,
/// searched and saved like any other.
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

    /// Hides the syntax that markdown::Style::Hideable marks, outside the
    /// revealed block. Off by default.
    void setHideSyntax(bool hide);
    [[nodiscard]] bool hidesSyntax() const { return m_hideSyntax; }
    /// Shows the syntax of @p block, the one being edited, and hides that of
    /// the block revealed before. It stays revealed when lines are added or
    /// removed around it.
    void setRevealedBlock(const QTextBlock &block);

    /// The format used for @p style, for tests.
    [[nodiscard]] QTextCharFormat formatFor(markdown::Style style) const;

protected:
    void highlightBlock(const QString &text) override;

private:
    void updateFormats();
    void hide(int start, int length);

    QPalette m_palette;
    qreal m_basePointSize = 0;
    bool m_hideSyntax = false;
    QTextCursor m_revealed; ///< a cursor follows its block through edits elsewhere
    std::array<QTextCharFormat, size_t(markdown::Style::Hideable) + 1> m_formats;
};

} // namespace inkwell
