// SPDX-License-Identifier: MIT
#include "EditorView.h"

#include "MarkdownHighlighter.h"
#include "core/AutosaveTimer.h"
#include "core/BackgroundWriter.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

/// Lines longer than this are hard to read; extra width becomes margin.
constexpr int kMaxTextWidth = 876;
/// Around the text, inside the area the editor paints: on the left, quote
/// bars are drawn there, three levels deep at most.
constexpr int kDocumentMargin = 20;
/// Outside that area, at least.
constexpr int kMinSideMargin = 8;
constexpr qreal kQuoteBarWidth = 2;
constexpr qreal kQuoteBarGap = 8; ///< between a quote bar and the text
constexpr qreal kQuoteBarSpacing = 3; ///< between the bars of nested quotes
constexpr int kTabStopSpaces = 4;

/// Case-folds @p text and strips accents character by character, so that
/// positions in the result are positions in @p text.
QString foldForMatching(QStringView text)
{
    QString folded(text.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i < text.size(); ++i) {
        QChar c = text[i];
        if (c.unicode() >= 0x80 && c.decompositionTag() == QChar::Canonical)
            c = c.decomposition().at(0); // "é" decomposes to "e" plus a combining accent
        folded[i] = c.toCaseFolded();
    }
    return folded;
}

/// Where a symbol goes, in the coordinates of the block's origin.
struct SymbolGeometry
{
    QRectF cell; ///< the space the hidden syntax keeps
    QRectF line; ///< the line it is on, across the width of the text
    QRectF lines; ///< all lines of the block, across the width of the text
    qreal baseline = 0;
};

SymbolGeometry symbolGeometry(const QTextBlock &block, const markdown::Span &symbol)
{
    const QTextLayout *layout = block.layout();
    const auto start = int(symbol.start);
    const QTextLine line = layout->lineForTextPosition(start);
    if (!line.isValid())
        return {};
    const QPointF origin = layout->position();
    const qreal left = line.cursorToX(start);
    const qreal right = line.cursorToX(start + int(symbol.length));
    const QTextLine first = layout->lineAt(0);
    const QTextLine last = layout->lineAt(layout->lineCount() - 1);
    return {
        QRectF(left, line.y(), right - left, line.height()).translated(origin),
        QRectF(line.x(), line.y(), line.width(), line.height()).translated(origin),
        QRectF(first.x(), first.y(), first.width(), last.rect().bottom() - first.y())
            .translated(origin),
        origin.y() + line.y() + line.ascent(),
    };
}

} // namespace

EditorView::EditorView(PageId id, const QString &body, BackgroundWriter &writer, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_pageId(id)
    , m_writer(writer)
    , m_autosave(new AutosaveTimer(this))
    , m_highlighter(new MarkdownHighlighter(static_cast<QObject *>(this)))
    , m_revealTimer(new QTimer(this))
{
    setFrameShape(QFrame::NoFrame);
    // Paint the margins around the text column like the text area itself.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    viewport()->setMouseTracking(true); // to show where a task can be clicked
    m_baseFont = font();
    updateTabStops();
    document()->setDocumentMargin(kDocumentMargin);

    setPlainText(body);
    document()->setModified(false);
    m_highlighter->setPalette(palette());
    m_highlighter->setHideSyntax(true);
    m_highlighter->setRevealedBlock(textCursor().block());
    setMarkdownHighlighting(true);
    m_revealTimer->setSingleShot(true);
    m_revealTimer->setInterval(QGuiApplication::styleHints()->mouseDoubleClickInterval());
    connect(m_revealTimer, &QTimer::timeout, this, [this] {
        if (!m_mouseSelecting)
            revealCursorLine();
    });
    const auto followCursor = [this] {
        if (!m_mouseSelecting)
            revealCursorLine();
    };
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, followCursor);
    // Qt 6.4 moves the cursor without cursorPositionChanged() in selectAll().
    connect(this, &QPlainTextEdit::selectionChanged, this, followCursor);

    connect(document(), &QTextDocument::contentsChanged, m_autosave, &AutosaveTimer::notifyChanged);
    connect(document(), &QTextDocument::modificationChanged, this, &EditorView::saveStateChanged);
    connect(m_autosave, &AutosaveTimer::timeout, this, &EditorView::saveNow);
}

EditorView::~EditorView()
{
    saveNow();
}

bool EditorView::hasUnsavedChanges() const
{
    return document()->isModified() || m_durableTicket < m_queuedTicket;
}

void EditorView::saveNow()
{
    m_autosave->cancel();
    if (!document()->isModified())
        return;
    m_queuedTicket = m_writer.savePage(m_pageId, toPlainText());
    // From now on "modified" means "changed since the last snapshot". Undoing
    // back to exactly this text makes the document unmodified again, which is
    // correct: that text is already saved.
    document()->setModified(false);
}

void EditorView::discardChanges()
{
    m_autosave->cancel();
    document()->setModified(false);
    m_durableTicket = m_queuedTicket;
}

void EditorView::markDurable(quint64 ticket)
{
    if (ticket <= m_durableTicket)
        return;
    m_durableTicket = ticket;
    emit saveStateChanged();
}

TabState EditorView::viewState() const
{
    const int scroll = m_pendingScroll >= 0 ? m_pendingScroll : verticalScrollBar()->value();
    return {m_pageId, textCursor().position(), scroll};
}

void EditorView::restoreViewState(int cursor, int scroll)
{
    QTextCursor textCursor(document());
    textCursor.setPosition(qBound(0, cursor, document()->characterCount() - 1));
    setTextCursor(textCursor);
    m_pendingScroll = std::max(0, scroll);
    if (isVisible())
        applyPendingScroll();
}

bool EditorView::selectFirstMatch(const QStringList &words)
{
    const QString text = foldForMatching(toPlainText());
    qsizetype bestStart = -1;
    qsizetype bestLength = 0;
    bool bestAtWordStart = false;
    for (const QString &word : words) {
        // Search ignores punctuation around words, such as quotes.
        qsizetype first = 0;
        qsizetype last = word.size();
        while (first < last && !word[first].isLetterOrNumber())
            ++first;
        while (last > first && !word[last - 1].isLetterOrNumber())
            --last;
        const QString needle = foldForMatching(QStringView(word).sliced(first, last - first));
        if (needle.isEmpty())
            continue;

        for (qsizetype at = text.indexOf(needle); at >= 0; at = text.indexOf(needle, at + 1)) {
            const bool atWordStart = at == 0 || !text[at - 1].isLetterOrNumber();
            // A word start anywhere beats a match inside a word; then the earliest wins.
            if (bestStart < 0 || (atWordStart && !bestAtWordStart)
                || (atWordStart == bestAtWordStart && at < bestStart)) {
                bestStart = at;
                bestLength = needle.size();
                bestAtWordStart = atWordStart;
            }
            if (atWordStart)
                break; // later ones in the text cannot be better
        }
    }
    if (bestStart < 0)
        return false;

    QTextCursor cursor(document());
    cursor.setPosition(int(bestStart));
    cursor.setPosition(int(bestStart + bestLength), QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    // Centre once laid out, which a page that was just opened is not yet.
    QTimer::singleShot(0, this, &QPlainTextEdit::centerCursor);
    return true;
}

void EditorView::setMarkdownHighlighting(bool enabled)
{
    if (enabled != markdownHighlighting())
        m_highlighter->setDocument(enabled ? document() : nullptr);
}

bool EditorView::markdownHighlighting() const
{
    return m_highlighter->document() != nullptr;
}

void EditorView::setHideMarkdownSyntax(bool hide)
{
    m_highlighter->setHideSyntax(hide);
}

bool EditorView::hidesMarkdownSyntax() const
{
    return m_highlighter->hidesSyntax();
}

void EditorView::setZoom(int percent)
{
    if (percent == m_zoom || percent <= 0)
        return;
    m_zoom = percent;
    QFont zoomed = m_baseFont;
    if (m_baseFont.pointSizeF() > 0)
        zoomed.setPointSizeF(m_baseFont.pointSizeF() * percent / 100);
    else
        zoomed.setPixelSize(std::max(1, qRound(m_baseFont.pixelSize() * percent / 100.0)));
    setFont(zoomed);
    updateTabStops();
    updateMargins();
    if (markdownHighlighting())
        m_highlighter->rehighlight(); // heading sizes follow the font
}

void EditorView::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    // Follow the system switching between light and dark themes.
    if (event->type() == QEvent::PaletteChange)
        m_highlighter->setPalette(palette());
}

void EditorView::focusOutEvent(QFocusEvent *event)
{
    saveNow();
    QPlainTextEdit::focusOutEvent(event);
}

void EditorView::mousePressEvent(QMouseEvent *event)
{
    // A click on a checkbox toggles its task, and leaves the cursor where it
    // is. Without the press, the base class neither selects on a drag nor
    // moves the cursor on the release.
    if (event->button() == Qt::LeftButton && toggleTaskAt(event->position().toPoint()))
        return;
    // Revealing the syntax moves the text. While the button is down, that
    // would move it under the pointer, which then selects text. After a
    // click, it would move the word a double-click is meant to select. So
    // the line of a click is revealed once no double-click can follow.
    if (event->button() == Qt::LeftButton)
        m_mouseSelecting = true;
    QPlainTextEdit::mousePressEvent(event);
}

void EditorView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // The second click of a double-click toggles the task again, as with
    // any checkbox.
    if (event->button() == Qt::LeftButton && toggleTaskAt(event->position().toPoint()))
        return;
    if (event->button() == Qt::LeftButton)
        m_mouseSelecting = true;
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

void EditorView::mouseMoveEvent(QMouseEvent *event)
{
    QPlainTextEdit::mouseMoveEvent(event);
    if (event->buttons() == Qt::NoButton) {
        const bool onTask = !taskMarkAt(event->position().toPoint()).isNull();
        const Qt::CursorShape shape = onTask ? Qt::PointingHandCursor : Qt::IBeamCursor;
        if (viewport()->cursor().shape() != shape)
            viewport()->setCursor(shape);
    }
}

void EditorView::mouseReleaseEvent(QMouseEvent *event)
{
    QPlainTextEdit::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && m_mouseSelecting) {
        m_mouseSelecting = false;
        m_revealTimer->start();
    }
}

void EditorView::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    paintSymbols(event->rect());
}

void EditorView::resizeEvent(QResizeEvent *event)
{
    // Margins first: the base class wraps the text to the width of the area
    // inside them, and does not wrap again when only the margins change.
    updateMargins();
    QPlainTextEdit::resizeEvent(event);
}

void EditorView::showEvent(QShowEvent *event)
{
    QPlainTextEdit::showEvent(event);
    // Wait for the first layout, so the scroll bar has its real range.
    if (m_pendingScroll >= 0)
        QTimer::singleShot(0, this, &EditorView::applyPendingScroll);
}

void EditorView::applyPendingScroll()
{
    if (m_pendingScroll < 0)
        return;
    verticalScrollBar()->setValue(m_pendingScroll);
    m_pendingScroll = -1;
}

void EditorView::revealCursorLine()
{
    m_revealTimer->stop();
    m_highlighter->setRevealedBlock(textCursor().block());
}

void EditorView::paintSymbols(const QRect &area)
{
    using markdown::Style;
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    const QFontMetricsF metrics(font());
    // Symbols take the colours of the syntax they stand for.
    const auto colour
        = [this](Style style) { return m_highlighter->formatFor(style).foreground().color(); };

    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF rect = blockBoundingGeometry(block).translated(contentOffset());
        if (rect.top() > area.bottom())
            break;
        const QList<markdown::Span> symbols = m_highlighter->symbols(block);
        const auto quoteDepth = std::count_if(symbols.cbegin(), symbols.cend(),
            [](const markdown::Span &symbol) { return symbol.style == Style::QuoteBarSymbol; });
        int quoteBar = 0;
        for (const markdown::Span &symbol : symbols) {
            const SymbolGeometry place = symbolGeometry(block, symbol);
            const QRectF cell = place.cell.translated(rect.topLeft());
            const qreal baseline = place.baseline + rect.top();
            switch (symbol.style) {
            case Style::BulletSymbol: {
                const qreal radius = metrics.xHeight() * 0.33;
                painter.setPen(Qt::NoPen);
                painter.setBrush(colour(Style::ListMarker));
                painter.drawEllipse(
                    QPointF(cell.center().x(), baseline - metrics.xHeight() / 2), radius, radius);
                break;
            }
            case Style::CheckboxSymbol:
            case Style::CheckedBoxSymbol: {
                const qreal side = std::min(cell.width(), metrics.capHeight() * 1.15);
                QRectF box(0, 0, side, side);
                box.moveCenter(QPointF(cell.center().x(), baseline - metrics.capHeight() / 2));
                const qreal radius = side / 5;
                if (symbol.style == Style::CheckboxSymbol) {
                    const qreal width = std::max(1.0, side / 10);
                    painter.setPen(QPen(colour(Style::ListMarker), width));
                    painter.setBrush(Qt::NoBrush);
                    const qreal inset = width / 2;
                    painter.drawRoundedRect(
                        box.adjusted(inset, inset, -inset, -inset), radius, radius);
                    break;
                }
                painter.setPen(Qt::NoPen);
                painter.setBrush(colour(Style::ListMarker));
                painter.drawRoundedRect(box, radius, radius);
                painter.setPen(QPen(palette().color(QPalette::Base), side / 7, Qt::SolidLine,
                    Qt::RoundCap, Qt::RoundJoin));
                const QPointF tick[] {
                    box.topLeft() + QPointF(0.26, 0.52) * side,
                    box.topLeft() + QPointF(0.43, 0.69) * side,
                    box.topLeft() + QPointF(0.75, 0.33) * side,
                };
                painter.drawPolyline(tick, 3);
                break;
            }
            case Style::QuoteBarSymbol: {
                // Beside the text, in the margin, as wrapped lines start
                // where the > is. From the top of the block to its bottom,
                // so the bars of a quote's lines join; the outermost quote's
                // bar is the leftmost.
                const QRectF lines = place.lines.translated(rect.topLeft());
                const qreal right = lines.left() - kQuoteBarGap
                    - qreal(quoteDepth - 1 - quoteBar++) * (kQuoteBarWidth + kQuoteBarSpacing);
                painter.fillRect(
                    QRectF(right - kQuoteBarWidth, lines.top(), kQuoteBarWidth, lines.height()),
                    colour(Style::Marker));
                break;
            }
            case Style::RuleSymbol: {
                const QRectF line = place.line.translated(rect.topLeft());
                const qreal thickness = std::max(1.0, std::round(metrics.height() / 16));
                const qreal top = std::round(line.center().y() - thickness / 2);
                painter.fillRect(
                    QRectF(line.left(), top, line.width(), thickness), colour(Style::Rule));
                break;
            }
            default:
                break;
            }
        }
    }
}

QTextCursor EditorView::taskMarkAt(const QPoint &point) const
{
    const QTextBlock block = cursorForPosition(point).block();
    const QPointF origin = blockBoundingGeometry(block).translated(contentOffset()).topLeft();
    for (const markdown::Span &symbol : m_highlighter->symbols(block)) {
        if (symbol.style != markdown::Style::CheckboxSymbol
            && symbol.style != markdown::Style::CheckedBoxSymbol)
            continue;
        if (symbolGeometry(block, symbol).cell.translated(origin).contains(point)) {
            // Between the brackets: a space, or an x when done.
            QTextCursor mark(block);
            mark.setPosition(block.position() + int(symbol.start) + 1);
            mark.setPosition(mark.position() + 1, QTextCursor::KeepAnchor);
            return mark;
        }
    }
    return {};
}

bool EditorView::toggleTaskAt(const QPoint &point)
{
    QTextCursor mark = taskMarkAt(point);
    if (mark.isNull())
        return false;
    // An edit like any other: it can be undone, and is saved.
    mark.insertText(mark.selectedText() == u" "_s ? u"x"_s : u" "_s);
    return true;
}

void EditorView::updateMargins()
{
    const int maxTextWidth = kMaxTextWidth * m_zoom / 100;
    const int side = std::max(kMinSideMargin, (width() - maxTextWidth) / 2);
    if (viewportMargins().left() != side)
        setViewportMargins(side, 0, side, 0);
}

void EditorView::updateTabStops()
{
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * kTabStopSpaces);
}

void EditorView::wheelEvent(QWheelEvent *event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QPlainTextEdit::wheelEvent(event);
        return;
    }
    // A mouse wheel moves 120 per notch; touchpads send many small deltas.
    m_wheelDelta += event->angleDelta().y();
    const int steps = m_wheelDelta / QWheelEvent::DefaultDeltasPerStep;
    m_wheelDelta %= QWheelEvent::DefaultDeltasPerStep;
    if (steps != 0)
        emit zoomRequested(steps);
    event->accept();
}

} // namespace inkwell
