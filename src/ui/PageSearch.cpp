// SPDX-License-Identifier: MIT
#include "PageSearch.h"

#include "PageTreeModel.h"
#include "core/NoteStore.h"

#include <QApplication>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTextLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

enum Role { IdRole = Qt::UserRole + 1, PathRole, SnippetRole };

constexpr int kMaxResults = 50;

/// Draws a result on two lines: the title with its folder path on the right,
/// then the snippet with the matched words in bold.
class ResultDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString title = opt.text;
        opt.text.clear();
        const QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const QRect area = opt.rect.adjusted(kPadding, kPadding, -kPadding, -kPadding);
        const QFontMetrics metrics(opt.font);
        const int lineHeight = metrics.height();
        const QColor text = opt.palette.color(
            opt.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text);
        QColor dimmed = text;
        dimmed.setAlphaF(0.6f);
        painter->save();

        QString path = index.data(PathRole).toString();
        const int maxPathWidth = area.width() / 2;
        if (metrics.horizontalAdvance(path) > maxPathWidth)
            path = metrics.elidedText(path, Qt::ElideLeft, maxPathWidth);
        const int pathWidth = metrics.horizontalAdvance(path);
        const QRect firstLine(area.left(), area.top(), area.width(), lineHeight);
        painter->setPen(dimmed);
        painter->drawText(firstLine.adjusted(firstLine.width() - pathWidth, 0, 1, 0),
            Qt::AlignRight | Qt::AlignVCenter, path);
        QFont titleFont = opt.font;
        titleFont.setWeight(QFont::DemiBold);
        painter->setFont(titleFont);
        painter->setPen(text);
        painter->drawText(firstLine.adjusted(0, 0, -(pathWidth + kPadding), 0),
            Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(titleFont).elidedText(
                title, Qt::ElideRight, firstLine.width() - pathWidth - kPadding));

        // The snippet comes with markers around the matched words.
        const QString marked = index.data(SnippetRole).toString();
        QString plain;
        plain.reserve(marked.size());
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        bold.setForeground(text);
        QList<QTextLayout::FormatRange> formats;
        for (const QChar c : marked) {
            if (c == SearchHit::kMatchStart)
                formats.append({int(plain.size()), 0, bold});
            else if (c == SearchHit::kMatchEnd && !formats.isEmpty())
                formats.last().length = int(plain.size()) - formats.last().start;
            else
                plain.append(c);
        }
        QTextLayout layout(plain, opt.font);
        layout.setFormats(formats);
        layout.beginLayout();
        QTextLine line = layout.createLine();
        line.setLineWidth(1e6); // single line; clipped below
        layout.endLayout();
        const QRect secondLine(area.left(), area.top() + lineHeight, area.width(), lineHeight);
        painter->setFont(opt.font);
        painter->setClipRect(secondLine);
        painter->setPen(dimmed);
        layout.draw(painter, secondLine.topLeft());
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(2 * option.fontMetrics.height() + 2 * kPadding);
        return size;
    }

private:
    static constexpr int kPadding = 6;
};

} // namespace

PageSearch::PageSearch(NoteStore &store, PageTreeModel *pages, QWidget *parent)
    : SearchPopup(tr("Search in all pages…"), tr("No pages contain these words"), parent)
    , m_store(store)
    , m_pages(pages)
{
    results()->setItemDelegate(new ResultDelegate(results()));
}

void PageSearch::prepare()
{
    m_folderPaths.clear();
    for (const PageTreeModel::PageEntry &entry : m_pages->pageEntries())
        m_folderPaths.insert(entry.id, entry.folderPath);
}

void PageSearch::updateResults()
{
    QStandardItemModel *model = resultsModel();
    model->clear();
    for (const SearchHit &hit : m_store.search(input()->text(), kMaxResults)) {
        auto *item = new QStandardItem(hit.title);
        item->setData(hit.id, IdRole);
        item->setData(m_folderPaths.value(hit.id), PathRole);
        item->setData(hit.snippet, SnippetRole);
        item->setToolTip(m_folderPaths.value(hit.id));
        model->appendRow(item);
    }
    resultsChanged();
}

void PageSearch::activate(const QModelIndex &index)
{
    emit pageChosen(
        index.data(IdRole).toLongLong(), input()->text().split(u' ', Qt::SkipEmptyParts));
}

} // namespace inkwell
