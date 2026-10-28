// SPDX-License-Identifier: MIT
#include "QuickSwitcher.h"

#include "core/FuzzyMatch.h"

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

enum Role { IdRole = Qt::UserRole + 1, PathRole, MatchRole };

/// Results beyond this are not worth scrolling through; refine the query instead.
constexpr qsizetype kMaxResults = 100;

/// Draws the title with the matched characters in bold and the folder path,
/// dimmed, on the right.
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

        const QRect area = opt.rect.adjusted(kPadding, 0, -kPadding, 0);
        const QColor text = opt.palette.color(
            opt.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text);
        const QFontMetrics metrics(opt.font);
        painter->save();

        // The path gets at most half the row, shortened from the left if needed,
        // so the folder closest to the page stays readable.
        QString path = index.data(PathRole).toString();
        const int maxPathWidth = area.width() / 2;
        if (metrics.horizontalAdvance(path) > maxPathWidth)
            path = metrics.elidedText(path, Qt::ElideLeft, maxPathWidth);
        const int pathWidth = metrics.horizontalAdvance(path);
        QColor dimmed = text;
        dimmed.setAlphaF(0.6f);
        painter->setPen(dimmed);
        painter->drawText(QRect(area.right() - pathWidth, area.top(), pathWidth + 1, area.height()),
            Qt::AlignRight | Qt::AlignVCenter, path);

        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        QList<QTextLayout::FormatRange> formats;
        for (const qsizetype position : index.data(MatchRole).value<QList<qsizetype>>()) {
            if (!formats.isEmpty() && formats.last().start + formats.last().length == position)
                ++formats.last().length;
            else
                formats.append({int(position), 1, bold});
        }
        QTextLayout layout(title, opt.font);
        layout.setFormats(formats);
        layout.beginLayout();
        QTextLine line = layout.createLine();
        line.setLineWidth(1e6); // single line; clipped below
        layout.endLayout();
        const QRect titleArea = area.adjusted(0, 0, -(pathWidth + kPadding), 0);
        painter->setClipRect(titleArea);
        painter->setPen(text);
        layout.draw(painter,
            QPointF(titleArea.left(), titleArea.top() + (titleArea.height() - line.height()) / 2));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), option.fontMetrics.height() + 2 * kPadding));
        return size;
    }

private:
    static constexpr int kPadding = 6;
};

} // namespace

QuickSwitcher::QuickSwitcher(PageTreeModel *pages, QWidget *parent)
    : SearchPopup(tr("Go to page…"), tr("No matching pages"), parent)
    , m_pages(pages)
{
    results()->setItemDelegate(new ResultDelegate(results()));
}

void QuickSwitcher::prepare()
{
    m_entries = m_pages->pageEntries();
}

void QuickSwitcher::updateResults()
{
    struct Hit
    {
        const PageTreeModel::PageEntry *entry;
        FuzzyMatch match;
    };
    QList<Hit> hits;
    const QString query = input()->text();
    for (const PageTreeModel::PageEntry &entry : std::as_const(m_entries)) {
        if (auto match = fuzzyMatch(query, entry.title))
            hits.append({&entry, std::move(*match)});
    }
    // Stable, so equally good matches keep the order of the tree.
    std::stable_sort(hits.begin(), hits.end(),
        [](const Hit &a, const Hit &b) { return a.match.score > b.match.score; });
    if (hits.size() > kMaxResults)
        hits.resize(kMaxResults);

    QStandardItemModel *model = resultsModel();
    model->clear();
    for (const Hit &hit : std::as_const(hits)) {
        auto *item = new QStandardItem(hit.entry->title);
        item->setData(hit.entry->id, IdRole);
        item->setData(hit.entry->folderPath, PathRole);
        item->setData(QVariant::fromValue(hit.match.positions), MatchRole);
        item->setToolTip(hit.entry->folderPath);
        model->appendRow(item);
    }
    resultsChanged();
}

void QuickSwitcher::activate(const QModelIndex &index)
{
    emit pageChosen(index.data(IdRole).toLongLong());
}

} // namespace inkwell
