// SPDX-License-Identifier: MIT
#include "NavigationPane.h"

#include "PageTreeModel.h"

#include <QAbstractItemDelegate>
#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>
#include <utility>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback, const QWidget *widget)
{
    return QIcon::fromTheme(name, widget->style()->standardIcon(fallback));
}

QToolButton *toolButton(QAction *action, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setAutoRaise(true);
    return button;
}

} // namespace

NavigationPane::NavigationPane(PageTreeModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_view(new QTreeView(this))
{
    m_newPage = new QAction(
        themeIcon(u"document-new"_s, QStyle::SP_FileIcon, this), tr("New &Page"), this);
    m_newPage->setShortcut(QKeySequence::New);
    m_newPage->setToolTip(tr("New page (%1)").arg(m_newPage->shortcut().toString()));
    connect(m_newPage, &QAction::triggered, this, &NavigationPane::createPage);

    m_newFolder = new QAction(
        themeIcon(u"folder-new"_s, QStyle::SP_FileDialogNewFolder, this), tr("New &Folder"), this);
    m_newFolder->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_N);
    m_newFolder->setToolTip(tr("New folder (%1)").arg(m_newFolder->shortcut().toString()));
    connect(m_newFolder, &QAction::triggered, this, &NavigationPane::createFolder);

    m_newSubfolder = new QAction(m_newFolder->icon(), tr("New &Subfolder"), this);
    connect(m_newSubfolder, &QAction::triggered, this, &NavigationPane::createSubfolder);

    // Rename and delete only apply while the tree has focus, so Delete in the
    // editor can never delete a page.
    m_rename = new QAction(QIcon::fromTheme(u"edit-rename"_s), tr("&Rename"), this);
    m_rename->setShortcut(Qt::Key_F2);
    m_rename->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_rename, &QAction::triggered, this, &NavigationPane::renameSelected);

    m_delete = new QAction(QIcon::fromTheme(u"user-trash"_s), tr("Move to &Trash"), this);
    m_delete->setShortcut(QKeySequence::Delete);
    m_delete->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_delete, &QAction::triggered, this, &NavigationPane::trashSelected);
    m_moveUp = new QAction(QIcon::fromTheme(u"go-up"_s), tr("Move &Up"), this);
    m_moveUp->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Up);
    m_moveUp->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_moveUp, &QAction::triggered, this, &NavigationPane::moveSelectedUp);

    m_moveDown = new QAction(QIcon::fromTheme(u"go-down"_s), tr("Move Dow&n"), this);
    m_moveDown->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Down);
    m_moveDown->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_moveDown, &QAction::triggered, this, &NavigationPane::moveSelectedDown);
    m_view->addActions({m_rename, m_delete, m_moveUp, m_moveDown});

    auto *title = new QLabel(tr("Pages"), this);
    title->setStyleSheet(u"font-weight: 600;"_s);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(8, 4, 4, 4);
    header->addWidget(title, 1);
    header->addWidget(toolButton(m_newFolder, this));
    header->addWidget(toolButton(m_newPage, this));

    m_view->header()->hide();
    // Lets the view skip measuring every row. Must be set before the model: the
    // shared row height is only measured while laying out rows with this on,
    // so turning it on afterwards left existing rows 0 px tall.
    m_view->setUniformRowHeights(true);
    m_view->setModel(model);
    m_view->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setAnimated(true);
    // Drag pages and folders to reorder them or to move them into another folder.
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDefaultDropAction(Qt::MoveAction);
    m_view->setAutoExpandDelay(600); // hovering over a folder while dragging opens it

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_view, 1);

    connect(m_view, &QTreeView::clicked, this, &NavigationPane::activate);
    connect(m_view, &QTreeView::activated, this, &NavigationPane::activate);
    connect(m_view, &QWidget::customContextMenuRequested, this, &NavigationPane::showContextMenu);
    connect(m_view, &QTreeView::expanded, this, &NavigationPane::expansionChanged);
    connect(m_view, &QTreeView::collapsed, this, &NavigationPane::expansionChanged);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        &NavigationPane::updateActions);
    // Moving, adding or removing items changes what can move up or down.
    connect(m_model, &QAbstractItemModel::rowsMoved, this, &NavigationPane::updateActions);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &NavigationPane::updateActions);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &NavigationPane::updateActions);
    // The model is rebuilt after restoring from the trash; keep folders open.
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this,
        [this] { m_expandedBeforeReset = expandedFolders(); });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        setExpandedFolders(std::exchange(m_expandedBeforeReset, {}));
        updateActions();
    });
    connect(m_view->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] {
        if (const PageId page = std::exchange(m_namingPage, 0))
            emit newPageNamed(page);
    });
    connect(m_model, &PageTreeModel::errorOccurred, this, [this](const QString &message) {
        QMessageBox::warning(
            this, tr("Inkwell"), tr("The change could not be saved:\n%1").arg(message));
    });
    updateActions();
}

void NavigationPane::revealPage(PageId id)
{
    const QModelIndex index = m_model->pageIndex(id);
    if (!index.isValid())
        return;
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        m_view->expand(parent);
    m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    m_view->scrollTo(index);
}

QList<FolderId> NavigationPane::expandedFolders() const
{
    QList<FolderId> result;
    // Only visit expanded folders; collapsed subtrees cannot contain visible expansions.
    std::function<void(const QModelIndex &)> visit = [&](const QModelIndex &parent) {
        const int rows = m_model->rowCount(parent);
        for (int row = 0; row < rows; ++row) {
            const QModelIndex index = m_model->index(row, 0, parent);
            if (!m_model->isFolder(index))
                break; // folders come first
            if (m_view->isExpanded(index)) {
                result.append(PageTreeModel::id(index));
                visit(index);
            }
        }
    };
    visit({});
    return result;
}

void NavigationPane::setExpandedFolders(const QList<FolderId> &ids)
{
    for (const FolderId id : ids) {
        const QModelIndex index = m_model->folderIndex(id);
        if (index.isValid())
            m_view->expand(index);
    }
}

void NavigationPane::createFolder()
{
    const QModelIndex index = m_model->createFolder(kNoFolder, tr("New folder"));
    if (index.isValid())
        startRenaming(index);
}

void NavigationPane::createSubfolder()
{
    const FolderId parent = m_model->containingFolder(m_view->currentIndex());
    if (parent == kNoFolder)
        return createFolder();
    const QModelIndex index = m_model->createFolder(parent, tr("New folder"));
    if (index.isValid()) {
        m_view->expand(index.parent());
        startRenaming(index);
    }
}

void NavigationPane::createPage()
{
    FolderId folder = m_model->containingFolder(m_view->currentIndex());
    if (folder == kNoFolder)
        folder = m_model->firstFolder();
    if (folder == kNoFolder) {
        // Every page needs a folder; make one for first-time users.
        const QModelIndex created = m_model->createFolder(kNoFolder, tr("Notes"));
        if (!created.isValid())
            return;
        folder = PageTreeModel::id(created);
    }

    const QModelIndex index = m_model->createPage(folder, tr("Untitled"));
    if (!index.isValid())
        return;
    const PageId id = PageTreeModel::id(index);
    m_view->expand(index.parent());
    emit pageActivated(id);
    m_namingPage = id;
    startRenaming(index);
}

void NavigationPane::renameSelected()
{
    const QModelIndex index = m_view->currentIndex();
    if (index.isValid())
        m_view->edit(index);
}

void NavigationPane::trashSelected()
{
    const QModelIndex index = m_view->currentIndex();
    if (!index.isValid())
        return;
    const QString name = index.data().toString();
    if (m_model->moveToTrash(index))
        emit movedToTrash(name);
}

void NavigationPane::moveSelectedUp()
{
    moveSelected(-1);
}

void NavigationPane::moveSelectedDown()
{
    moveSelected(1);
}

void NavigationPane::moveSelected(int delta)
{
    const QModelIndex index = m_view->currentIndex();
    if (m_model->shift(index, delta))
        m_view->scrollTo(m_view->currentIndex()); // the current index follows the move
}

void NavigationPane::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_view->indexAt(pos);
    m_view->setCurrentIndex(index);

    QMenu menu(this);
    if (!index.isValid()) {
        menu.addAction(m_newFolder);
    } else {
        menu.addAction(m_newPage);
        if (m_model->isFolder(index))
            menu.addAction(m_newSubfolder);
        menu.addSeparator();
        menu.addAction(m_moveUp);
        menu.addAction(m_moveDown);
        menu.addSeparator();
        menu.addAction(m_rename);
        menu.addAction(m_delete);
    }
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void NavigationPane::activate(const QModelIndex &index)
{
    if (m_model->isPage(index))
        emit pageActivated(PageTreeModel::id(index));
}

void NavigationPane::startRenaming(const QModelIndex &index)
{
    m_view->setFocus();
    m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    m_view->scrollTo(index);
    m_view->edit(index);
}

void NavigationPane::updateActions()
{
    const bool hasSelection = m_view->currentIndex().isValid();
    m_rename->setEnabled(hasSelection);
    m_delete->setEnabled(hasSelection);
    m_moveUp->setEnabled(m_model->canShift(m_view->currentIndex(), -1));
    m_moveDown->setEnabled(m_model->canShift(m_view->currentIndex(), 1));
}

} // namespace inkwell
