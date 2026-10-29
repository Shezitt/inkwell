// SPDX-License-Identifier: MIT
#pragma once

#include "core/Session.h"
#include "core/Types.h"

#include <QMainWindow>

class QAction;
class QLabel;
class QSplitter;
class QTimer;

namespace inkwell {

class BackgroundWriter;
class EditorArea;
class NavigationPane;
class NoteStore;
class PageSearch;
class PageTreeModel;
class QuickSwitcher;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(NoteStore &store, BackgroundWriter &writer, QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Reopens the tabs and restores the layout of a previous session.
    void restoreSession(const SessionState &state);
    [[nodiscard]] SessionState captureSession() const;

    /// Queues all edits and the session, then waits up to @p timeoutMs for
    /// them to be durable. Returns false if they could not all be written.
    bool saveEverything(int timeoutMs);

protected:
    void closeEvent(QCloseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void createActions();
    void openPage(PageId id);
    void updateWindowTitle();
    void updateSaveStatus();
    void scheduleSessionSave();
    void showError(const QString &message);
    void showAbout();

    NoteStore &m_store;
    BackgroundWriter &m_writer;
    PageTreeModel *m_model;
    NavigationPane *m_navigation;
    EditorArea *m_editors;
    QSplitter *m_splitter;
    QuickSwitcher *m_quickSwitcher;
    PageSearch *m_pageSearch;
    QLabel *m_saveStatus;
    QLabel *m_cursorStatus;
    QAction *m_sidebarAction = nullptr;
    QAction *m_markdownAction = nullptr;
    QTimer *m_sessionTimer;
    bool m_saveFailing = false;
};

} // namespace inkwell
