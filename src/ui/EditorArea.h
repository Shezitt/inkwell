// SPDX-License-Identifier: MIT
#pragma once

#include "core/Session.h"
#include "core/Types.h"

#include <QHash>
#include <QList>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QTabWidget;

namespace inkwell {

class BackgroundWriter;
class EditorView;
class NoteStore;

/// Tabbed area holding one EditorView per open page.
class EditorArea : public QWidget
{
    Q_OBJECT

public:
    EditorArea(NoteStore &store, BackgroundWriter &writer, QWidget *parent = nullptr);
    ~EditorArea() override;

    /// Opens @p id in a new tab, or switches to its tab if already open.
    bool openPage(PageId id, const QString &title);
    void closePage(PageId id);
    /// Closes the tabs of @p ids that are open, saving them first.
    void closePages(const QList<PageId> &ids);
    void renamePage(PageId id, const QString &title);
    /// Turns Markdown highlighting on or off in every editor, open or opened later.
    void setMarkdownHighlighting(bool enabled);
    /// Hides Markdown syntax off the cursor line in every editor, open or opened later.
    void setHideMarkdownSyntax(bool hide);

    /// Text size in every editor, open or opened later, in percent of normal.
    [[nodiscard]] int zoom() const { return m_zoom; }
    /// Sets the zoom, kept between the smallest and largest zoom level.
    void setZoom(int percent);
    /// Moves @p steps zoom levels up (positive) or down, like a web browser.
    void zoomBy(int steps);

    [[nodiscard]] EditorView *currentEditor() const;
    [[nodiscard]] EditorView *editor(PageId id) const { return m_editors.value(id); }
    [[nodiscard]] PageId currentPage() const;
    /// Open pages in tab order.
    [[nodiscard]] QList<PageId> openPages() const;
    [[nodiscard]] bool hasUnsavedChanges() const;

    /// Open tabs with their cursor and scroll positions, in tab order.
    [[nodiscard]] QList<TabState> tabStates() const;
    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int index);

    /// Queues every unsaved change in every tab.
    void saveAll();

public slots:
    void closeCurrentTab();
    void nextTab();
    void previousTab();
    void focusCurrentEditor();
    void zoomIn() { zoomBy(1); }
    void zoomOut() { zoomBy(-1); }
    void resetZoom() { setZoom(100); }

signals:
    /// The current tab changed; 0 when no tab is open.
    void currentPageChanged(inkwell::PageId id);
    void cursorPositionChanged(int line, int column);
    /// The cursor or scroll position of some tab changed.
    void viewStateChanged();
    void saveStateChanged();
    /// Tabs were opened, closed or reordered.
    void tabsChanged();
    void errorOccurred(const QString &message);
    void zoomChanged(int percent);

private:
    void closeTab(int index, bool save);
    void onCurrentChanged(int index);
    void emitCursorPosition();
    [[nodiscard]] EditorView *editorAt(int index) const;

    NoteStore &m_store;
    BackgroundWriter &m_writer;
    QStackedWidget *m_stack;
    QTabWidget *m_tabs;
    QLabel *m_placeholder;
    QHash<PageId, EditorView *> m_editors;
    bool m_markdownHighlighting = true;
    bool m_hideMarkdownSyntax = true;
    int m_zoom = 100;
};

} // namespace inkwell
