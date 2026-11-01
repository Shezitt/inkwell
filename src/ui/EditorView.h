// SPDX-License-Identifier: MIT
#pragma once

#include "core/Session.h"
#include "core/Types.h"

#include <QPlainTextEdit>
#include <QTextCursor>

class QTimer;

namespace inkwell {

class AutosaveTimer;
class BackgroundWriter;
class MarkdownHighlighter;

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

    /// Styles the Markdown in the text (see MarkdownHighlighter). On by default.
    void setMarkdownHighlighting(bool enabled);
    [[nodiscard]] bool markdownHighlighting() const;
    /// Hides Markdown syntax such as ** and the URLs of links, except on the
    /// line with the cursor, while highlighting is on. On by default.
    void setHideMarkdownSyntax(bool hide);
    [[nodiscard]] bool hidesMarkdownSyntax() const;

    /// Shows the text at @p percent of its normal size. The text column widens
    /// along, so lines keep about the same number of words.
    void setZoom(int percent);
    [[nodiscard]] int zoom() const { return m_zoom; }

signals:
    /// hasUnsavedChanges() may have changed.
    void saveStateChanged();
    /// Ctrl + mouse wheel: the user asked to zoom by @p steps, positive to zoom in.
    void zoomRequested(int steps);

protected:
    void changeEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateMargins();
    void updateTabStops();
    void applyPendingScroll();
    void revealCursorLine();
    void paintSymbols(const QRect &area);
    [[nodiscard]] QTextCursor taskMarkAt(const QPoint &point) const;
    bool toggleTaskAt(const QPoint &point);

    PageId m_pageId;
    BackgroundWriter &m_writer;
    AutosaveTimer *m_autosave;
    MarkdownHighlighter *m_highlighter;
    QTimer *m_revealTimer; ///< reveals the line of a click, if no double-click follows
    quint64 m_queuedTicket = 0; ///< ticket of the last save handed to the writer
    quint64 m_durableTicket = 0; ///< newest ticket the writer reported durable
    int m_pendingScroll = -1; ///< scroll position to apply once laid out
    QFont m_baseFont; ///< the font at 100%
    int m_zoom = 100;
    int m_wheelDelta = 0; ///< Ctrl+wheel movement not yet turned into zoom steps
    bool m_mouseSelecting = false; ///< the left button is down in the text
};

} // namespace inkwell
