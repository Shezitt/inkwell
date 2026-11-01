// SPDX-License-Identifier: MIT
#pragma once

#include "core/MarkdownSyntax.h"

#include <QFontMetricsF>
#include <QHash>
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
/// searched and saved like any other. Syntax drawn as a symbol instead, such
/// as the - of a list item, mostly keeps its space for the symbol; the
/// highlighter leaves drawing it to the editor (see symbols()).
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

    /// The symbols to draw in @p block in place of its hidden syntax, in the
    /// order of the text: spans of the markdown::Style::BulletSymbol style
    /// and those after it. Empty while the syntax of the block shows.
    [[nodiscard]] QList<markdown::Span> symbols(const QTextBlock &block) const;

    /// The format used for @p style, one with a look of its own, which comes
    /// before markdown::Style::Hideable.
    [[nodiscard]] QTextCharFormat formatFor(markdown::Style style) const;

protected:
    void highlightBlock(const QString &text) override;

private:
    void updateFormats();
    void hide(int start, int length, QStringView keepWidthOf, bool nothingLeft);

    /// What hide() draws the characters of a font in.
    struct HiddenFont
    {
        QFontMetricsF metrics; ///< of the font hidden
        QFont amidText; ///< as high as fits among text in the font hidden
        QFont alone; ///< as high as a line of the font hidden
    };
    const HiddenFont &hiddenFont(const QFont &font);

    QPalette m_palette;
    qreal m_basePointSize = 0;
    bool m_hideSyntax = false;
    QTextCursor m_revealed; ///< a cursor follows its block through edits elsewhere
    std::array<QTextCharFormat, size_t(markdown::Style::Hideable)> m_formats;
    QHash<QFont, HiddenFont> m_hiddenFonts; ///< by the font hidden, as computing them is slow
};

} // namespace inkwell
