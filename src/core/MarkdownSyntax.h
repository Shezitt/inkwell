// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QStringView>

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
    /// only its effect shows: heading #s, emphasis and code markers, and all
    /// of a link but its text. Overlaps the spans above.
    Hideable,
};

struct Span
{
    qsizetype start = 0;
    qsizetype length = 0;
    Style style = Style::Marker;

    friend bool operator==(const Span &, const Span &) = default;
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
/// Style::Hideable spans come in addition to the others, which describe the
/// line as it is when its syntax shows.
[[nodiscard]] LineState highlightLine(QStringView line, LineState previous, QList<Span> &spans);

} // namespace inkwell::markdown
