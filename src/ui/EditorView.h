// SPDX-License-Identifier: MIT
#pragma once

#include "core/Session.h"
#include "core/Types.h"

#include <QPlainTextEdit>

namespace inkwell {

class AutosaveTimer;
class BackgroundWriter;

/// Plain-text editor for one page, with automatic, non-blocking saving.
///
/// Edits are handed to the BackgroundWriter as text snapshots: when the user
/// pauses (see AutosaveTimer), when the editor loses focus, and when it is
/// closed. The editor itself never touches the disk.
class EditorView : public QPlainTextEdit
{
    Q_OBJECT

public:
    EditorView(PageId id, const QString &body, BackgroundWriter &writer, QWidget *parent = nullptr);
    /// Queues any unsaved change before the editor goes away.
    ~EditorView() override;

    [[nodiscard]] PageId pageId() const { return m_pageId; }

    /// True if some change is not yet durable on disk.
    [[nodiscard]] bool hasUnsavedChanges() const;

    /// Queues the current text if it changed since the last save.
    void saveNow();
    /// Drops unsaved changes, for pages that are being deleted.
    void discardChanges();
    /// Called when the writer reports @p ticket as durable.
    void markDurable(quint64 ticket);

    /// Cursor and scroll position, for session restore.
    [[nodiscard]] TabState viewState() const;
    /// Moves the cursor to @p cursor and scrolls to @p scroll once the editor is shown.
    void restoreViewState(int cursor, int scroll);

    /// Selects the first occurrence of any of @p words, preferring the start
    /// of a word and ignoring case and accents, as search does, and scrolls it
    /// into the middle of the view. Returns false if none occurs.
    bool selectFirstMatch(const QStringList &words);

signals:
    /// hasUnsavedChanges() may have changed.
    void saveStateChanged();

protected:
    void focusOutEvent(QFocusEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateMargins();
    void applyPendingScroll();

    PageId m_pageId;
    BackgroundWriter &m_writer;
    AutosaveTimer *m_autosave;
    quint64 m_queuedTicket = 0; ///< ticket of the last save handed to the writer
    quint64 m_durableTicket = 0; ///< newest ticket the writer reported durable
    int m_pendingScroll = -1; ///< scroll position to apply once laid out
};

} // namespace inkwell
