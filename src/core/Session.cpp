// SPDX-License-Identifier: MIT
#include "Session.h"

#include "Database.h"
#include "Log.h"

#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace inkwell::session {

namespace {

const QString kCurrentTab = u"current_tab"_s;
const QString kExpandedFolders = u"expanded_folders"_s;
const QString kSidebarVisible = u"sidebar_visible"_s;
const QString kMarkdownHighlighting = u"markdown_highlighting"_s;
const QString kWindowGeometry = u"window_geometry"_s;
const QString kWindowState = u"window_state"_s;
const QString kSplitterState = u"splitter_state"_s;

QString joinIds(const QList<FolderId> &ids)
{
    QStringList parts;
    parts.reserve(ids.size());
    for (const FolderId id : ids)
        parts.append(QString::number(id));
    return parts.join(u',');
}

QList<FolderId> splitIds(const QString &text)
{
    QList<FolderId> ids;
    const auto parts = QStringView(text).split(u',', Qt::SkipEmptyParts);
    for (const QStringView part : parts) {
        bool ok = false;
        const FolderId id = part.toLongLong(&ok);
        if (ok)
            ids.append(id);
    }
    return ids;
}

} // namespace

SessionState load(Database &db)
{
    SessionState state;

    QSqlQuery query(db.connection());
    query.setForwardOnly(true);
    if (query.exec(u"SELECT page_id, cursor, scroll FROM session_tabs ORDER BY position"_s)) {
        while (query.next()) {
            state.tabs.append(
                {query.value(0).toLongLong(), query.value(1).toInt(), query.value(2).toInt()});
        }
    } else {
        qCWarning(lcStorage) << "cannot load tabs:" << query.lastError().text();
    }

    QHash<QString, QVariant> values;
    if (query.exec(u"SELECT key, value FROM session_values"_s)) {
        while (query.next())
            values.insert(query.value(0).toString(), query.value(1));
    } else {
        qCWarning(lcStorage) << "cannot load session:" << query.lastError().text();
    }

    // Tabs of deleted pages are gone, so clamp the current index.
    const int current = values.value(kCurrentTab, -1).toInt();
    state.currentTab = state.tabs.isEmpty() ? -1 : qBound(0, current, int(state.tabs.size()) - 1);
    state.expandedFolders = splitIds(values.value(kExpandedFolders).toString());
    state.sidebarVisible = values.value(kSidebarVisible, true).toBool();
    state.markdownHighlighting = values.value(kMarkdownHighlighting, true).toBool();
    state.windowGeometry = values.value(kWindowGeometry).toByteArray();
    state.windowState = values.value(kWindowState).toByteArray();
    state.splitterState = values.value(kSplitterState).toByteArray();
    return state;
}

bool save(Database &db, const SessionState &state)
{
    Transaction tx(db);
    if (!tx.isActive() || !db.exec(u"DELETE FROM session_tabs"_s))
        return false;

    QSqlQuery query(db.connection());
    query.prepare(
        u"INSERT INTO session_tabs (position, page_id, cursor, scroll) VALUES (?, ?, ?, ?)"_s);
    for (qsizetype i = 0; i < state.tabs.size(); ++i) {
        const TabState &tab = state.tabs.at(i);
        query.addBindValue(i);
        query.addBindValue(tab.page);
        query.addBindValue(tab.cursor);
        query.addBindValue(tab.scroll);
        // A page deleted a moment ago fails the foreign key; just skip its tab.
        if (!query.exec())
            qCDebug(lcStorage) << "skipping tab of page" << tab.page << query.lastError().text();
    }

    query.prepare(u"INSERT OR REPLACE INTO session_values (key, value) VALUES (?, ?)"_s);
    const auto put = [&query](const QString &key, const QVariant &value) {
        query.addBindValue(key);
        query.addBindValue(value);
        if (!query.exec()) {
            qCWarning(lcStorage) << "cannot save" << key << query.lastError().text();
            return false;
        }
        return true;
    };
    const bool ok = put(kCurrentTab, state.currentTab)
        && put(kExpandedFolders, joinIds(state.expandedFolders))
        && put(kSidebarVisible, state.sidebarVisible)
        && put(kMarkdownHighlighting, state.markdownHighlighting)
        && put(kWindowGeometry, state.windowGeometry) && put(kWindowState, state.windowState)
        && put(kSplitterState, state.splitterState);
    return ok && tx.commit();
}

} // namespace inkwell::session
