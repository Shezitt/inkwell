// SPDX-License-Identifier: MIT
#include "EditorView.h"

#include "MarkdownHighlighter.h"
#include "core/AutosaveTimer.h"
#include "core/BackgroundWriter.h"

#include <QFontMetricsF>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>

namespace inkwell {

namespace {

/// Lines longer than this are hard to read; extra width becomes margin.
constexpr int kMaxTextWidth = 860;
constexpr int kMinSideMargin = 16;
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

} // namespace

EditorView::EditorView(PageId id, const QString &body, BackgroundWriter &writer, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_pageId(id)
    , m_writer(writer)
    , m_autosave(new AutosaveTimer(this))
    , m_highlighter(new MarkdownHighlighter(static_cast<QObject *>(this)))
{
    setFrameShape(QFrame::NoFrame);
    // Paint the margins around the text column like the text area itself.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * kTabStopSpaces);
    document()->setDocumentMargin(12);

    setPlainText(body);
    document()->setModified(false);
    m_highlighter->setPalette(palette());
    setMarkdownHighlighting(true);

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

void EditorView::updateMargins()
{
    const int side = std::max(kMinSideMargin, (width() - kMaxTextWidth) / 2);
    if (viewportMargins().left() != side)
        setViewportMargins(side, 0, side, 0);
}

} // namespace inkwell
