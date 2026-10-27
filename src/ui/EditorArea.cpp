// SPDX-License-Identifier: MIT
#include "EditorArea.h"

#include "EditorView.h"
#include "core/BackgroundWriter.h"
#include "core/NoteStore.h"

#include <QLabel>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace inkwell {

namespace {

/// Upper bound for waiting on pending saves before (re)loading a page.
constexpr int kLoadFlushTimeoutMs = 5000;

} // namespace

EditorArea::EditorArea(NoteStore &store, BackgroundWriter &writer, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_writer(writer)
    , m_stack(new QStackedWidget(this))
    , m_tabs(new QTabWidget(this))
    , m_placeholder(new QLabel(tr("Select a page or create a new one."), this))
{
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->tabBar()->setExpanding(false);

    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setEnabled(false);

    m_stack->addWidget(m_placeholder);
    m_stack->addWidget(m_tabs);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    connect(
        m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index, true); });
    connect(m_tabs, &QTabWidget::currentChanged, this, &EditorArea::onCurrentChanged);
    connect(m_tabs->tabBar(), &QTabBar::tabMoved, this, &EditorArea::tabsChanged);

    // One connection for all editors: route durability reports by page id.
    connect(&m_writer, &BackgroundWriter::pageSaved, this, [this](PageId id, quint64 ticket) {
        if (EditorView *view = m_editors.value(id))
            view->markDurable(ticket);
    });
}

EditorArea::~EditorArea()
{
    saveAll();
    // Tear the editors down while our members are still alive, and without
    // reacting to the tab changes that causes.
    m_tabs->disconnect(this);
    m_tabs->tabBar()->disconnect(this);
    const auto editors = std::exchange(m_editors, {});
    for (EditorView *view : editors) {
        view->disconnect(this);
        delete view;
    }
}

bool EditorArea::openPage(PageId id, const QString &title)
{
    if (EditorView *existing = m_editors.value(id)) {
        m_tabs->setCurrentWidget(existing);
        existing->setFocus();
        return true;
    }

    // A save of this page may still be queued from an earlier tab; make sure
    // we read the latest text.
    m_writer.flush(kLoadFlushTimeoutMs);
    const std::optional<QString> body = m_store.pageBody(id);
    if (!body) {
        emit errorOccurred(tr("Could not open “%1”:\n%2").arg(title, m_store.lastError()));
        return false;
    }

    auto *view = new EditorView(id, *body, m_writer, m_tabs);
    m_editors.insert(id, view);
    connect(view, &EditorView::saveStateChanged, this, &EditorArea::saveStateChanged);
    connect(view, &QPlainTextEdit::cursorPositionChanged, this, [this, view] {
        if (view == currentEditor())
            emitCursorPosition();
        emit viewStateChanged();
    });
    connect(
        view->verticalScrollBar(), &QScrollBar::valueChanged, this, &EditorArea::viewStateChanged);

    const int index = m_tabs->addTab(view, title);
    m_tabs->setTabToolTip(index, title);
    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentWidget(m_tabs);
    view->setFocus();
    emit tabsChanged();
    return true;
}

void EditorArea::closePage(PageId id)
{
    if (EditorView *view = m_editors.value(id))
        closeTab(m_tabs->indexOf(view), true);
}

void EditorArea::discardPages(const QList<PageId> &ids)
{
    for (const PageId id : ids) {
        if (EditorView *view = m_editors.value(id))
            closeTab(m_tabs->indexOf(view), false);
    }
}

void EditorArea::renamePage(PageId id, const QString &title)
{
    if (EditorView *view = m_editors.value(id)) {
        const int index = m_tabs->indexOf(view);
        m_tabs->setTabText(index, title);
        m_tabs->setTabToolTip(index, title);
    }
}

EditorView *EditorArea::currentEditor() const
{
    return editorAt(m_tabs->currentIndex());
}

PageId EditorArea::currentPage() const
{
    const EditorView *view = currentEditor();
    return view ? view->pageId() : 0;
}

QList<PageId> EditorArea::openPages() const
{
    QList<PageId> result;
    result.reserve(m_tabs->count());
    for (int i = 0; i < m_tabs->count(); ++i)
        result.append(editorAt(i)->pageId());
    return result;
}

bool EditorArea::hasUnsavedChanges() const
{
    return std::any_of(m_editors.cbegin(), m_editors.cend(),
        [](const EditorView *view) { return view->hasUnsavedChanges(); });
}

QList<TabState> EditorArea::tabStates() const
{
    QList<TabState> result;
    result.reserve(m_tabs->count());
    for (int i = 0; i < m_tabs->count(); ++i)
        result.append(editorAt(i)->viewState());
    return result;
}

int EditorArea::currentIndex() const
{
    return m_tabs->currentIndex();
}

void EditorArea::setCurrentIndex(int index)
{
    m_tabs->setCurrentIndex(index);
    focusCurrentEditor();
}

void EditorArea::saveAll()
{
    for (EditorView *view : std::as_const(m_editors))
        view->saveNow();
}

void EditorArea::closeCurrentTab()
{
    if (m_tabs->count() > 0)
        closeTab(m_tabs->currentIndex(), true);
}

void EditorArea::nextTab()
{
    if (m_tabs->count() > 1)
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
}

void EditorArea::previousTab()
{
    const int count = m_tabs->count();
    if (count > 1)
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + count - 1) % count);
}

void EditorArea::focusCurrentEditor()
{
    if (EditorView *view = currentEditor())
        view->setFocus();
}

void EditorArea::closeTab(int index, bool save)
{
    EditorView *view = editorAt(index);
    if (!view)
        return;
    if (save)
        view->saveNow();
    else
        view->discardChanges();

    m_editors.remove(view->pageId());
    m_tabs->removeTab(index);
    delete view;

    if (m_tabs->count() == 0)
        m_stack->setCurrentWidget(m_placeholder);
    emit tabsChanged();
    emit saveStateChanged();
}

void EditorArea::onCurrentChanged(int)
{
    emit currentPageChanged(currentPage());
    emitCursorPosition();
}

void EditorArea::emitCursorPosition()
{
    const EditorView *view = currentEditor();
    if (!view)
        return;
    const QTextCursor cursor = view->textCursor();
    emit cursorPositionChanged(cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
}

EditorView *EditorArea::editorAt(int index) const
{
    return qobject_cast<EditorView *>(m_tabs->widget(index));
}

} // namespace inkwell
