// SPDX-License-Identifier: MIT
#pragma once

#include "core/Types.h"

#include <QList>
#include <QWidget>

class QAction;
class QTreeView;

namespace inkwell {

class PageTreeModel;

/// Sidebar with the tree of folders and pages and the actions to manage them.
class NavigationPane : public QWidget
{
    Q_OBJECT

public:
    explicit NavigationPane(PageTreeModel *model, QWidget *parent = nullptr);

    [[nodiscard]] QTreeView *view() const { return m_view; }
    [[nodiscard]] QAction *newPageAction() const { return m_newPage; }
    [[nodiscard]] QAction *newFolderAction() const { return m_newFolder; }

    /// Selects @p id in the tree, expanding its folders, without emitting pageActivated().
    void revealPage(PageId id);

    [[nodiscard]] QList<FolderId> expandedFolders() const;
    void setExpandedFolders(const QList<FolderId> &ids);

public slots:
    /// Creates a top-level folder and starts renaming it.
    void createFolder();
    /// Creates a folder inside the selected folder and starts renaming it.
    void createSubfolder();
    /// Creates a page next to the selection, opens it and starts renaming it.
    void createPage();
    void renameSelected();
    void deleteSelected();

signals:
    void pageActivated(inkwell::PageId id);
    /// The user finished naming a page created with createPage().
    void newPageNamed(inkwell::PageId id);
    void expansionChanged();

private:
    void showContextMenu(const QPoint &pos);
    void activate(const QModelIndex &index);
    void startRenaming(const QModelIndex &index);
    void updateActions();

    PageTreeModel *m_model;
    QTreeView *m_view;
    QAction *m_newPage;
    QAction *m_newFolder;
    QAction *m_newSubfolder;
    QAction *m_rename;
    QAction *m_delete;
    PageId m_namingPage = 0;
};

} // namespace inkwell
