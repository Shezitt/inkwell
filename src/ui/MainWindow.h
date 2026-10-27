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
class PageTreeModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(NoteStore &store, BackgroundWriter &writer, QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Reopens the tabs and restores the layout of a previous session.
    void restoreSession(const SessionState &state);
    [[nodiscard]] SessionState captureSession() const;

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

    BackgroundWriter &m_writer;
    PageTreeModel *m_model;
    NavigationPane *m_navigation;
    EditorArea *m_editors;
    QSplitter *m_splitter;
    QLabel *m_saveStatus;
    QLabel *m_cursorStatus;
    QAction *m_sidebarAction = nullptr;
    QTimer *m_sessionTimer;
    bool m_saveFailing = false;
};

} // namespace inkwell
