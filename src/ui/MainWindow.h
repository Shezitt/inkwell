// SPDX-License-Identifier: MIT
#pragma once

#include "core/Types.h"

#include <QMainWindow>

class QLabel;
class QSplitter;

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

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void openPage(PageId id);
    void updateWindowTitle();
    void updateSaveStatus();
    void showError(const QString &message);
    void showAbout();

    BackgroundWriter &m_writer;
    PageTreeModel *m_model;
    NavigationPane *m_navigation;
    EditorArea *m_editors;
    QSplitter *m_splitter;
    QLabel *m_saveStatus;
    QLabel *m_cursorStatus;
    bool m_saveFailing = false;
};

} // namespace inkwell
