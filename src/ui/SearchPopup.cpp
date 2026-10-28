// SPDX-License-Identifier: MIT
#include "SearchPopup.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>

namespace inkwell {

namespace {

constexpr int kVisibleRows = 10;

} // namespace

SearchPopup::SearchPopup(const QString &placeholder, const QString &noResultsText, QWidget *parent)
    : QFrame(parent, Qt::Popup)
    , m_input(new QLineEdit(this))
    , m_results(new QListView(this))
    , m_noResults(new QLabel(noResultsText, this))
    , m_model(new QStandardItemModel(this))
{
    setFrameShape(QFrame::StyledPanel);

    m_input->setPlaceholderText(placeholder);
    m_input->setClearButtonEnabled(true);
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::textChanged, this, [this] { updateResults(); });

    m_results->setModel(m_model);
    m_results->setFocusPolicy(Qt::NoFocus); // typing always goes to the input
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setUniformItemSizes(true);
    m_results->setFrameShape(QFrame::NoFrame);
    connect(m_results, &QListView::clicked, this, &SearchPopup::choose);

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

void SearchPopup::popup(QWidget *window)
{
    prepare();
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

void SearchPopup::resultsChanged()
{
    m_results->setCurrentIndex(m_model->index(0, 0));
    const int count = m_model->rowCount();
    m_results->setVisible(count > 0);
    m_noResults->setVisible(count == 0 && !m_input->text().trimmed().isEmpty());
    if (count > 0)
        m_results->setFixedHeight(std::min(count, kVisibleRows) * m_results->sizeHintForRow(0));
    adjustSize();
}

bool SearchPopup::eventFilter(QObject *watched, QEvent *event)
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

void SearchPopup::choose(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    close();
    activate(index);
}

} // namespace inkwell
