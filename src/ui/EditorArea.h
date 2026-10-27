// SPDX-License-Identifier: MIT
#pragma once

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
    /// Closes the tabs of deleted pages without saving them.
    void discardPages(const QList<PageId> &ids);
    void renamePage(PageId id, const QString &title);

    [[nodiscard]] EditorView *currentEditor() const;
    [[nodiscard]] EditorView *editor(PageId id) const { return m_editors.value(id); }
    [[nodiscard]] PageId currentPage() const;
    /// Open pages in tab order.
    [[nodiscard]] QList<PageId> openPages() const;
    [[nodiscard]] bool hasUnsavedChanges() const;

    /// Queues every unsaved change in every tab.
    void saveAll();

public slots:
    void closeCurrentTab();
    void nextTab();
    void previousTab();
    void focusCurrentEditor();

signals:
    /// The current tab changed; 0 when no tab is open.
    void currentPageChanged(inkwell::PageId id);
    void cursorPositionChanged(int line, int column);
    void saveStateChanged();
    /// Tabs were opened, closed or reordered.
    void tabsChanged();
    void errorOccurred(const QString &message);

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
};

} // namespace inkwell
