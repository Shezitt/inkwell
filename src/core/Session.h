// SPDX-License-Identifier: MIT
#pragma once

#include "Types.h"

#include <QByteArray>
#include <QList>

namespace inkwell {

class Database;

/// One open tab: which page, and where the user was in it.
struct TabState
{
    PageId page = 0;
    int cursor = 0; ///< cursor position in characters
    int scroll = 0; ///< vertical scroll bar value

    friend bool operator==(const TabState &, const TabState &) = default;
};

/// What Inkwell restores on startup: tabs, the sidebar and the window layout.
struct SessionState
{
    QList<TabState> tabs;
    int currentTab = -1;
    QList<FolderId> expandedFolders;
    bool sidebarVisible = true;
    bool markdownHighlighting = true;
    bool hideMarkdownSyntax = true; ///< outside the line being edited
    int zoom = 100; ///< editor text size, in percent
    QByteArray windowGeometry; ///< QWidget::saveGeometry()
    QByteArray windowState; ///< QMainWindow::saveState()
    QByteArray splitterState; ///< QSplitter::saveState()

    friend bool operator==(const SessionState &, const SessionState &) = default;
};

namespace session {

/// Loads the last saved session. Returns a default session if none was saved
/// yet. Tabs of pages that no longer exist are already gone.
[[nodiscard]] SessionState load(Database &db);

/// Replaces the stored session with @p state, atomically.
bool save(Database &db, const SessionState &state);

} // namespace session
} // namespace inkwell
