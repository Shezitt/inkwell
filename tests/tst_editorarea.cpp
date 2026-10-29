// SPDX-License-Identifier: MIT
#include "TestNotebook.h"
#include "ui/EditorArea.h"
#include "ui/EditorView.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestEditorArea : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<TestNotebook> m_notebook;

    PageId newPage(const QString &body = {}) { return m_notebook->newPage(body); }

private slots:
    void init()
    {
        m_notebook = std::make_unique<TestNotebook>();
        QVERIFY(m_notebook->open());
    }

    void cleanup() { m_notebook.reset(); }

    void opensEachPageOnce()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        QSignalSpy current(&area, &EditorArea::currentPageChanged);
        const PageId a = newPage(u"A body"_s);
        const PageId b = newPage(u"B body"_s);

        QVERIFY(area.openPage(a, u"A"_s));
        QVERIFY(area.openPage(b, u"B"_s));
        QVERIFY(area.openPage(a, u"A"_s)); // switches instead of duplicating

        QCOMPARE(area.openPages(), (QList<PageId> {a, b}));
        QCOMPARE(area.currentPage(), a);
        QCOMPARE(area.currentEditor()->toPlainText(), u"A body"_s);
        QCOMPARE(current.constLast().at(0).value<PageId>(), a);
    }

    void reportsMissingPages()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        QSignalSpy errors(&area, &EditorArea::errorOccurred);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no page"_s));
        QVERIFY(!area.openPage(999, u"Ghost"_s));
        QCOMPARE(errors.size(), 1);
        QVERIFY(area.openPages().isEmpty());
    }

    void savesWhenClosingTabs()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        const PageId page = newPage();
        area.openPage(page, u"P"_s);
        area.currentEditor()->insertPlainText(u"keep me"_s);
        area.closeCurrentTab();

        QVERIFY(area.openPages().isEmpty());
        QCOMPARE(area.currentPage(), PageId(0));
        QVERIFY(m_notebook->writer->flush(5000));
        QCOMPARE(m_notebook->storedBody(page), u"keep me"_s);
    }

    void reopensWithLatestText()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        const PageId page = newPage();
        area.openPage(page, u"P"_s);
        area.currentEditor()->insertPlainText(u"fresh"_s);
        area.closePage(page);
        // Reopen immediately; the save may still be queued.
        area.openPage(page, u"P"_s);
        QCOMPARE(area.currentEditor()->toPlainText(), u"fresh"_s);
    }

    void closesPagesKeepingTheirText()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        const PageId a = newPage();
        const PageId b = newPage();
        const PageId closedAlready = newPage();
        area.openPage(a, u"A"_s);
        area.openPage(b, u"B"_s);
        area.editor(a)->insertPlainText(u"kept"_s);

        area.closePages({a, closedAlready});
        QCOMPARE(area.openPages(), QList<PageId> {b});
        QVERIFY(m_notebook->writer->flush(5000));
        QCOMPARE(m_notebook->storedBody(a), u"kept"_s);
    }

    void appliesMarkdownHighlightingToEveryEditor()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        const PageId open = newPage();
        area.openPage(open, u"Open"_s);
        QVERIFY(area.editor(open)->markdownHighlighting());

        area.setMarkdownHighlighting(false);
        QVERIFY(!area.editor(open)->markdownHighlighting());
        const PageId later = newPage();
        area.openPage(later, u"Later"_s);
        QVERIFY(!area.editor(later)->markdownHighlighting());

        area.setMarkdownHighlighting(true);
        QVERIFY(area.editor(open)->markdownHighlighting());
        QVERIFY(area.editor(later)->markdownHighlighting());
    }

    void zoomsEveryEditorThroughFixedLevels()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        QSignalSpy changed(&area, &EditorArea::zoomChanged);
        const PageId open = newPage();
        area.openPage(open, u"Open"_s);
        QCOMPARE(area.zoom(), 100);

        area.zoomIn();
        QCOMPARE(area.zoom(), 110);
        QCOMPARE(area.editor(open)->zoom(), 110);
        area.zoomBy(3);
        QCOMPARE(area.zoom(), 175);
        area.zoomOut();
        QCOMPARE(area.zoom(), 150);

        // Opened later: same zoom.
        const PageId later = newPage();
        area.openPage(later, u"Later"_s);
        QCOMPARE(area.editor(later)->zoom(), 150);

        // The ends hold.
        area.zoomBy(100);
        QCOMPARE(area.zoom(), 300);
        area.zoomBy(-100);
        QCOMPARE(area.zoom(), 50);
        changed.clear();
        area.zoomOut();
        QCOMPARE(changed.size(), 0); // already as small as it gets

        area.resetZoom();
        QCOMPARE(area.zoom(), 100);
        QCOMPARE(area.editor(open)->zoom(), 100);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.constFirst().at(0).toInt(), 100);
    }

    void zoomsFromInBetweenLevels()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        area.setZoom(105); // a stored zoom need not be one of the levels
        QCOMPARE(area.zoom(), 105);
        area.zoomIn();
        QCOMPARE(area.zoom(), 110);
        area.setZoom(105);
        area.zoomOut();
        QCOMPARE(area.zoom(), 100);
        area.setZoom(1000);
        QCOMPARE(area.zoom(), 300);
        area.setZoom(1);
        QCOMPARE(area.zoom(), 50);
    }

    void zoomsWhenAnEditorAsks()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        const PageId a = newPage();
        const PageId b = newPage();
        area.openPage(a, u"A"_s);
        area.openPage(b, u"B"_s);
        emit area.editor(a)->zoomRequested(2); // Ctrl + wheel in one editor
        QCOMPARE(area.zoom(), 125);
        QCOMPARE(area.editor(b)->zoom(), 125); // all of them follow
    }

    void renamesTabs()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        const PageId page = newPage();
        area.openPage(page, u"Old"_s);
        area.renamePage(page, u"New"_s);
        auto *tabs = area.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(tabs->tabText(0), u"New"_s);
    }

    void cyclesTabs()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        const PageId a = newPage();
        const PageId b = newPage();
        const PageId c = newPage();
        area.openPage(a, u"A"_s);
        area.openPage(b, u"B"_s);
        area.openPage(c, u"C"_s);
        area.nextTab();
        QCOMPARE(area.currentPage(), a);
        area.previousTab();
        QCOMPARE(area.currentPage(), c);
    }

    void reportsTabStatesInOrder()
    {
        EditorArea area(*m_notebook->store, *m_notebook->writer);
        const PageId a = newPage(u"aaaa"_s);
        const PageId b = newPage(u"bbbb"_s);
        area.openPage(a, u"A"_s);
        area.openPage(b, u"B"_s);
        area.editor(a)->restoreViewState(2, 0);
        area.setCurrentIndex(0);

        const QList<TabState> states = area.tabStates();
        QCOMPARE(states.size(), 2);
        QCOMPARE(states.at(0).page, a);
        QCOMPARE(states.at(0).cursor, 2);
        QCOMPARE(states.at(1).page, b);
        QCOMPARE(area.currentIndex(), 0);
    }

    void savesEverythingOnDestruction()
    {
        const PageId a = newPage();
        const PageId b = newPage();
        {
            EditorArea area(*m_notebook->store, *m_notebook->writer);
            area.openPage(a, u"A"_s);
            area.openPage(b, u"B"_s);
            area.editor(a)->insertPlainText(u"first"_s);
            area.editor(b)->insertPlainText(u"second"_s);
        }
        QVERIFY(m_notebook->writer->flush(5000));
        QCOMPARE(m_notebook->storedBody(a), u"first"_s);
        QCOMPARE(m_notebook->storedBody(b), u"second"_s);
    }
};

QTEST_MAIN(TestEditorArea)
#include "tst_editorarea.moc"
