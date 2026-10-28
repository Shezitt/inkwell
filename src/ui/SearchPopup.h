// SPDX-License-Identifier: MIT
#pragma once

#include <QFrame>

class QLabel;
class QLineEdit;
class QListView;
class QStandardItemModel;

namespace inkwell {

/// Base for popups with a search box over a list of results, such as the
/// quick switcher.
///
/// Typing goes to the search box; Up, Down, Page Up and Page Down move
/// through the results without leaving it; Enter or a click picks a result
/// and Escape closes the popup.
class SearchPopup : public QFrame
{
    Q_OBJECT

public:
    /// Shows the popup near the top of @p window, with an empty search box.
    void popup(QWidget *window);

    [[nodiscard]] QLineEdit *input() const { return m_input; }
    [[nodiscard]] QListView *results() const { return m_results; }

protected:
    SearchPopup(const QString &placeholder, const QString &noResultsText, QWidget *parent);

    /// Called by popup() before the first search, for example to take a
    /// snapshot of what can be found.
    virtual void prepare() { }
    /// Refills resultsModel() for the text in input(), then calls resultsChanged().
    virtual void updateResults() = 0;
    /// The user picked @p index. The popup has already closed.
    virtual void activate(const QModelIndex &index) = 0;

    [[nodiscard]] QStandardItemModel *resultsModel() const { return m_model; }
    /// Selects the first result and resizes the popup to fit the results.
    void resultsChanged();

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void choose(const QModelIndex &index);

    QLineEdit *m_input;
    QListView *m_results;
    QLabel *m_noResults;
    QStandardItemModel *m_model;
};

} // namespace inkwell
