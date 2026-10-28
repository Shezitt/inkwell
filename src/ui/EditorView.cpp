// SPDX-License-Identifier: MIT
#include "EditorView.h"

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

} // namespace

EditorView::EditorView(PageId id, const QString &body, BackgroundWriter &writer, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_pageId(id)
    , m_writer(writer)
    , m_autosave(new AutosaveTimer(this))
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

void EditorView::focusOutEvent(QFocusEvent *event)
{
    saveNow();
    QPlainTextEdit::focusOutEvent(event);
}

void EditorView::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateMargins();
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
