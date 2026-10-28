// SPDX-License-Identifier: MIT
#include "QuickSwitcher.h"

#include "core/FuzzyMatch.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

enum Role { IdRole = Qt::UserRole + 1, PathRole, MatchRole };

/// Results beyond this are not worth scrolling through; refine the query instead.
constexpr qsizetype kMaxResults = 100;
constexpr int kVisibleRows = 10;

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
    : QFrame(parent, Qt::Popup)
    , m_pages(pages)
    , m_input(new QLineEdit(this))
    , m_results(new QListView(this))
    , m_noResults(new QLabel(tr("No matching pages"), this))
    , m_model(new QStandardItemModel(this))
{
    setFrameShape(QFrame::StyledPanel);

    m_input->setPlaceholderText(tr("Go to page…"));
    m_input->setClearButtonEnabled(true);
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::textChanged, this, &QuickSwitcher::updateResults);

    m_results->setModel(m_model);
    m_results->setItemDelegate(new ResultDelegate(m_results));
    m_results->setFocusPolicy(Qt::NoFocus); // typing always goes to the input
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setUniformItemSizes(true);
    m_results->setFrameShape(QFrame::NoFrame);
    connect(m_results, &QListView::clicked, this, &QuickSwitcher::choose);

    m_noResults->setAlignment(Qt::AlignCenter);
    m_noResults->setEnabled(false); // drawn in the dimmed text colour
    m_noResults->setMargin(12);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(m_input);
    layout->addWidget(m_results);
    layout->addWidget(m_noResults);
}

void QuickSwitcher::popup(QWidget *window)
{
    m_entries = m_pages->pageEntries();
    m_input->clear();
    updateResults(); // also when the text was already empty

    const int width = std::clamp(window->width() * 3 / 5, std::min(400, window->width()), 640);
    setFixedWidth(width);
    adjustSize();
    move(window->mapToGlobal(QPoint((window->width() - width) / 2, 48)));
    show();
    activateWindow();
    m_input->setFocus();
}

bool QuickSwitcher::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            QCoreApplication::sendEvent(m_results, event);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            choose(m_results->currentIndex());
            return true;
        case Qt::Key_Escape:
            close();
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void QuickSwitcher::updateResults()
{
    struct Hit
    {
        const PageTreeModel::PageEntry *entry;
        FuzzyMatch match;
    };
    QList<Hit> hits;
    const QString query = m_input->text();
    for (const PageTreeModel::PageEntry &entry : std::as_const(m_entries)) {
        if (auto match = fuzzyMatch(query, entry.title))
            hits.append({&entry, std::move(*match)});
    }
    // Stable, so equally good matches keep the order of the tree.
    std::stable_sort(hits.begin(), hits.end(),
        [](const Hit &a, const Hit &b) { return a.match.score > b.match.score; });
    if (hits.size() > kMaxResults)
        hits.resize(kMaxResults);

    m_model->clear();
    for (const Hit &hit : std::as_const(hits)) {
        auto *item = new QStandardItem(hit.entry->title);
        item->setData(hit.entry->id, IdRole);
        item->setData(hit.entry->folderPath, PathRole);
        item->setData(QVariant::fromValue(hit.match.positions), MatchRole);
        item->setToolTip(hit.entry->folderPath);
        m_model->appendRow(item);
    }
    m_results->setCurrentIndex(m_model->index(0, 0));

    const bool empty = hits.isEmpty();
    m_results->setVisible(!empty);
    m_noResults->setVisible(empty);
    if (!empty) {
        const auto rows = int(std::min<qsizetype>(hits.size(), kVisibleRows));
        m_results->setFixedHeight(rows * m_results->sizeHintForRow(0));
    }
    adjustSize();
}

void QuickSwitcher::choose(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    const PageId id = index.data(IdRole).toLongLong();
    close();
    emit pageChosen(id);
}

} // namespace inkwell
