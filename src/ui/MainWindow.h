// SPDX-License-Identifier: MIT
#pragma once

#include <QMainWindow>

class QSplitter;

namespace inkwell {

class NavigationPane;
class NoteStore;
class PageTreeModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(NoteStore &store, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void createActions();
    void showAbout();

    PageTreeModel *m_model;
    NavigationPane *m_navigation;
    QSplitter *m_splitter;
};

} // namespace inkwell
