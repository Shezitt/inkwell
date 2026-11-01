// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QString>
#include <QStringView>
#include <QUrl>

namespace inkwell::markdown {

/// What a span of a Markdown line is, for highlighting.
enum class Style {
    Marker, ///< syntax characters such as #, **, `, > and fences
    Heading1,
    Heading2,
    Heading3,
    Heading, ///< levels 4 to 6
    Bold,
    Italic,
    Strikethrough,
    Code, ///< `inline code`
    CodeBlock, ///< lines inside a fenced code block
    Quote,
    ListMarker, ///< -, *, + or 1. starting a list item
    TaskDone, ///< the text of a checked task, - [x]
    LinkText,
    Url,
    Rule, ///< a horizontal rule, ---
    /// Syntax that can be hidden while the line is not being edited, so that
    /// only its effect shows: heading #s, emphasis and code markers, all of a
    /// link but its text, the bullet of a task and the spaces of a quote's
    /// >s. Overlaps the spans above.
    Hideable,
    // Syntax that, while hidden, is drawn as a symbol. These overlap the
    // spans above too, and keep their width for the symbol, but for quote
    // bars, which are drawn beside the text.
    BulletSymbol, ///< the -, * or + of a list item, drawn as a bullet
    CheckboxSymbol, ///< the [ ] of an open task, drawn as a checkbox
    CheckedBoxSymbol, ///< the [x] of a done task, drawn as a checked checkbox
    QuoteBarSymbol, ///< each > of a quote, drawn as a bar along the line
    RuleSymbol, ///< a horizontal rule, the whole line, drawn as a line across
};

struct Span
{
    qsizetype start = 0;
    qsizetype length = 0;
    Style style = Style::Marker;

    friend bool operator==(const Span &, const Span &) = default;
};

/// A link in a line of text: a [text](target) from its [ to its ), or a
/// bare URL.
struct Link
{
    qsizetype start = 0;
    qsizetype length = 0;
    QString target; ///< as written, such as https://example.com

    friend bool operator==(const Link &, const Link &) = default;
};

/// State carried from one line to the next: whether it is inside a fenced
/// code block, and which fence closes it.
enum class LineState { Normal = 0, InBacktickFence = 1, InTildeFence = 2 };

/// Finds the Markdown in one line of text, given the state the previous line
/// ended in, and returns the state this line ends in. Spans can nest, as
/// with bold text in a heading: apply them in order.
///
/// Covers what is common in notes: ATX headings, emphasis, strikethrough,
/// inline code, fenced code blocks, quotes, lists and task lists, links,
/// bare URLs and horizontal rules.
///
/// Style::Hideable and symbol spans come in addition to the others, which
/// describe the line as it is when its syntax shows.
[[nodiscard]] LineState highlightLine(QStringView line, LineState previous, QList<Span> &spans);

/// The links in one line, in order, given the state the previous line
/// ended in: those that highlightLine() styles.
[[nodiscard]] QList<Link> findLinks(QStringView line, LineState previous);

/// The address a link's @p target opens: a web page, or an email to write.
/// Invalid for anything else, such as a relative path or a local file,
/// which a click must not open. A target starting with www. is a web page.
[[nodiscard]] QUrl linkUrl(QStringView target);

} // namespace inkwell::markdown
