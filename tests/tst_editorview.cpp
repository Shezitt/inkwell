// SPDX-License-Identifier: MIT
#include "TestNotebook.h"
#include "ui/EditorView.h"

#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QWheelEvent>

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

    void restoresCursorAndScroll()
    {
        const QString body = u"line\n"_s.repeated(500);
        EditorView view(m_notebook->newPage(body), body, *m_notebook->writer);
        view.restoreViewState(25, 120);
        QCOMPARE(view.viewState().cursor, 25);
        QCOMPARE(view.viewState().scroll, 120); // pending until shown

        view.restoreViewState(1'000'000, 0); // clamped to the end of the text
        QCOMPARE(view.viewState().cursor, int(body.size()));
        QVERIFY(!view.hasUnsavedChanges());
    }

    void wrapsLinesToTheVisibleWidth()
    {
        // Paragraphs of words of varied lengths fill almost every line, so text
        // wrapped for even a slightly wider area than the visible one sticks out.
        const QStringList words = {u"a"_s, u"marker"_s, u"gripper"_s, u"to"_s, u"the"_s,
            u"recordings"_s, u"camera"_s, u"of"_s, u"slowly"_s, u"tray"_s, u"opens"_s};
        QString text;
        quint32 seed = 1;
        for (int line = 0; line < 30; ++line) {
            for (int word = 0; word < 40; ++word) {
                seed = seed * 1664525u + 1013904223u; // fixed sequence, same on every run
                text += words.at(int(seed >> 16) % words.size()) + u' ';
            }
            text += u'\n';
        }

        for (const int width : {560, 600, 640, 680, 720, 760}) {
            QTabWidget tabs;
            tabs.addTab(
                new EditorView(m_notebook->newPage(), u"short"_s, *m_notebook->writer), u"a"_s);
            auto *editor = new EditorView(m_notebook->newPage(), text, *m_notebook->writer);
            tabs.addTab(editor, u"b"_s);
            tabs.resize(width, 400);
            tabs.show();
            QVERIFY(QTest::qWaitForWindowExposed(&tabs));

            // A background tab gets its size only when it is first shown.
            tabs.setCurrentIndex(1);
            QCoreApplication::processEvents();
            QVERIFY2(editor->horizontalScrollBar()->maximum() == 0,
                qPrintable(u"text wider than the editor at a window width of %1"_s.arg(width)));
        }
    }

    void selectsFirstMatchOfSearchedWords()
    {
        EditorView view(m_notebook->newPage(), u"Concatenate.\nLa Caché del procesador. A cat."_s,
            *m_notebook->writer);
        const auto selected = [&view] { return view.textCursor().selectedText(); };

        // Case and accents are ignored, as in search.
        QVERIFY(view.selectFirstMatch({u"CACHE"_s}));
        QCOMPARE(selected(), u"Caché"_s);
        QCOMPARE(view.textCursor().selectionStart(), 16);

        // A match at the start of a word beats an earlier one inside a word.
        QVERIFY(view.selectFirstMatch({u"cat"_s}));
        QCOMPARE(view.textCursor().selectionStart(), 40);

        // The earliest of several words, with punctuation around them ignored.
        QVERIFY(view.selectFirstMatch({u"\"procesador\""_s, u"la"_s}));
        QCOMPARE(selected(), u"La"_s);
        // Prefixes match, as the last word does in search.
        QVERIFY(view.selectFirstMatch({u"proc"_s}));
        QCOMPARE(selected(), u"proc"_s);

        QVERIFY(!view.selectFirstMatch({u"zebra"_s, u"--"_s}));
        QCOMPARE(selected(), u"proc"_s); // unchanged
        QVERIFY(!view.selectFirstMatch({}));
    }

    void zoomsTheText()
    {
        QString text;
        quint32 seed = 7;
        const QStringList words = {u"a"_s, u"marker"_s, u"gripper"_s, u"to"_s, u"the"_s,
            u"recordings"_s, u"camera"_s, u"of"_s, u"slowly"_s, u"tray"_s};
        for (int line = 0; line < 20; ++line) {
            for (int word = 0; word < 40; ++word) {
                seed = seed * 1664525u + 1013904223u;
                text += words.at(int(seed >> 16) % words.size()) + u' ';
            }
            text += u'\n';
        }
        EditorView view(m_notebook->newPage(), text, *m_notebook->writer);
        view.resize(1400, 500);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const qreal normalSize = view.font().pointSizeF();
        const int normalColumn = view.viewport()->width();
        QCOMPARE(view.zoom(), 100);

        view.setZoom(150);
        QCOMPARE(view.zoom(), 150);
        QCOMPARE(view.font().pointSizeF(), normalSize * 1.5);
        // The text column widens along with the text...
        QVERIFY(view.viewport()->width() > normalColumn);
        // ...and the text is wrapped for it, not for the old width.
        QCoreApplication::processEvents();
        QCOMPARE(view.horizontalScrollBar()->maximum(), 0);

        view.setZoom(50);
        QCOMPARE(view.font().pointSizeF(), normalSize * 0.5);
        QCoreApplication::processEvents();
        QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
        view.setZoom(100);
        QCoreApplication::processEvents(); // lets the scroll bar come back
        QCOMPARE(view.font().pointSizeF(), normalSize);
        QCOMPARE(view.viewport()->width(), normalColumn);
        QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
        QVERIFY(!view.hasUnsavedChanges());
    }

    void zoomsWithCtrlAndTheWheel()
    {
        EditorView view(m_notebook->newPage(), u"text"_s, *m_notebook->writer);
        QSignalSpy zoom(&view, &EditorView::zoomRequested);
        const auto wheel = [&view](int delta, Qt::KeyboardModifiers modifiers) {
            QWheelEvent event(QPointF(5, 5), view.mapToGlobal(QPointF(5, 5)), {}, {0, delta},
                Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(view.viewport(), &event);
        };

        wheel(120, Qt::ControlModifier);
        wheel(-240, Qt::ControlModifier);
        QCOMPARE(zoom.size(), 2);
        QCOMPARE(zoom.at(0).at(0).toInt(), 1);
        QCOMPARE(zoom.at(1).at(0).toInt(), -2);

        // A touchpad's small movements add up to one step.
        wheel(60, Qt::ControlModifier);
        QCOMPARE(zoom.size(), 2);
        wheel(60, Qt::ControlModifier);
        QCOMPARE(zoom.size(), 3);
        QCOMPARE(zoom.at(2).at(0).toInt(), 1);

        // Without Ctrl the wheel scrolls, as usual.
        wheel(120, Qt::NoModifier);
        QCOMPARE(zoom.size(), 3);
        QCOMPARE(view.zoom(), 100); // the editor only asks; its owner decides
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
