// SPDX-License-Identifier: MIT
#include "TestNotebook.h"
#include "ui/EditorView.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestEditorView : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<TestNotebook> m_notebook;

private slots:
    void init()
    {
        m_notebook = std::make_unique<TestNotebook>();
        QVERIFY(m_notebook->open());
    }

    void cleanup() { m_notebook.reset(); }

    void startsCleanWithStoredText()
    {
        EditorView view(m_notebook->newPage(u"hello"_s), u"hello"_s, *m_notebook->writer);
        QCOMPARE(view.toPlainText(), u"hello"_s);
        QVERIFY(!view.hasUnsavedChanges());
    }

    void autosavesAfterTyping()
    {
        const PageId page = m_notebook->newPage();
        EditorView view(page, {}, *m_notebook->writer);
        QSignalSpy state(&view, &EditorView::saveStateChanged);
        connect(m_notebook->writer.get(), &BackgroundWriter::pageSaved, &view,
            [&view](PageId, quint64 ticket) { view.markDurable(ticket); });

        view.insertPlainText(u"typed text"_s);
        QVERIFY(view.hasUnsavedChanges());
        QVERIFY(!state.isEmpty());

        // No explicit save: the autosave timer and the writer do the work.
        QTRY_VERIFY_WITH_TIMEOUT(!view.hasUnsavedChanges(), 5000);
        QCOMPARE(m_notebook->storedBody(page), u"typed text"_s);
    }

    void savesOnDestruction()
    {
        const PageId page = m_notebook->newPage();
        {
            EditorView view(page, {}, *m_notebook->writer);
            view.insertPlainText(u"closing quickly"_s);
        }
        QVERIFY(m_notebook->writer->flush(5000));
        QCOMPARE(m_notebook->storedBody(page), u"closing quickly"_s);
    }

    void discardsChangesOnRequest()
    {
        const PageId page = m_notebook->newPage(u"original"_s);
        {
            EditorView view(page, u"original"_s, *m_notebook->writer);
            view.insertPlainText(u"unwanted "_s);
            view.discardChanges();
            QVERIFY(!view.hasUnsavedChanges());
        }
        QVERIFY(m_notebook->writer->flush(5000));
        QCOMPARE(m_notebook->storedBody(page), u"original"_s);
    }

    void undoToSavedTextIsClean()
    {
        EditorView view(m_notebook->newPage(), {}, *m_notebook->writer);
        view.insertPlainText(u"a"_s);
        view.saveNow();
        view.insertPlainText(u"b"_s);
        QVERIFY(view.document()->isModified());
        view.undo();
        QVERIFY(!view.document()->isModified());
    }
};

QTEST_MAIN(TestEditorView)
#include "tst_editorview.moc"
