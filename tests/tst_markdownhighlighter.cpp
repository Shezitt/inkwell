// SPDX-License-Identifier: MIT
#include "TestNotebook.h"
#include "ui/EditorView.h"
#include "ui/MarkdownHighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <tuple>

using namespace Qt::StringLiterals;
using namespace inkwell;

namespace {

/// The highlighting format at @p position of line @p line.
QTextCharFormat formatAt(const QTextDocument &document, int line, int position)
{
    const QTextBlock block = document.findBlockByNumber(line);
    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
        if (position >= range.start && position < range.start + range.length)
            return range.format;
    }
    return {};
}

/// Where the character at @p position of line @p line is drawn.
qreal xAt(const QTextDocument &document, int line, int position)
{
    const QTextBlock block = document.findBlockByNumber(line);
    // Lays the block out if it is not yet, as painting would.
    std::ignore = document.documentLayout()->blockBoundingRect(block);
    return block.layout()->lineForTextPosition(position).cursorToX(position);
}

bool isHighlighted(const QTextDocument &document)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (!block.layout()->formats().isEmpty())
            return true;
    }
    return false;
}

} // namespace

class TestMarkdownHighlighter : public QObject
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

    void stylesTheMarkdown()
    {
        QTextDocument document(u"# Title\nsome **bold** text\n```\ncode *x*\n```\nafter *it*"_s);
        QFont base = document.defaultFont();
        base.setPointSizeF(10);
        document.setDefaultFont(base);
        MarkdownHighlighter highlighter(&document);
        highlighter.rehighlight();

        // Heading: bold and larger, with a dimmed marker.
        QCOMPARE(formatAt(document, 0, 3).fontWeight(), int(QFont::Bold));
        QVERIFY(formatAt(document, 0, 3).fontPointSize() > 10);
        QVERIFY(formatAt(document, 0, 0).foreground().color().alphaF() < 1);
        QCOMPARE(
            formatAt(document, 0, 0).fontPointSize(), formatAt(document, 0, 3).fontPointSize());

        // "some **bold** text": the word is bold, the asterisks dimmed, the rest plain.
        QCOMPARE(formatAt(document, 1, 7).fontWeight(), int(QFont::Bold));
        QVERIFY(formatAt(document, 1, 5).foreground().color().alphaF() < 1);
        QVERIFY(!formatAt(document, 1, 0).hasProperty(QTextFormat::FontWeight));

        // Inside the fence: fixed-width, and *x* is not italic.
        const QString fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        QCOMPARE(formatAt(document, 3, 0).fontFamilies().toStringList(), QStringList {fixed});
        QVERIFY(!formatAt(document, 3, 6).fontItalic());
        QCOMPARE(
            document.findBlockByNumber(3).userState(), int(markdown::LineState::InBacktickFence));

        // After the closing fence, Markdown again.
        QVERIFY(formatAt(document, 5, 7).fontItalic());
    }

    void hidesSyntaxOutsideTheRevealedBlock()
    {
        const QString text = u"## Title ##\n"
                             u"some **bold** and `code`\n"
                             u"[link](https://example.com/office \"Title 😀\")!\n"
                             u"- [ ] task"_s;
        const qsizetype bang = text.section(u'\n', 2, 2).size() - 1;
        QTextDocument document(text);
        document.setModified(false);
        MarkdownHighlighter highlighter(&document);
        highlighter.setHideSyntax(true);
        QVERIFY(highlighter.hidesSyntax());

        // Hidden syntax takes no space: what follows it is drawn where it begins.
        QCOMPARE(xAt(document, 0, 3), xAt(document, 0, 0));
        QCOMPARE(xAt(document, 0, 11), xAt(document, 0, 8));
        QCOMPARE(xAt(document, 1, 7), xAt(document, 1, 5));
        QCOMPARE(xAt(document, 1, 13), xAt(document, 1, 11));
        QCOMPARE(xAt(document, 1, 19), xAt(document, 1, 18));
        QCOMPARE(xAt(document, 1, 24), xAt(document, 1, 23));
        QCOMPARE(xAt(document, 2, 1), xAt(document, 2, 0));
        QCOMPARE(xAt(document, 2, int(bang)), xAt(document, 2, 5));
        QCOMPARE(xAt(document, 3, 2), xAt(document, 3, 0)); // a checkbox stands for the bullet
        // Only the look changes.
        QCOMPARE(document.toPlainText(), text);
        QVERIFY(!document.isModified());

        // The revealed block shows its syntax; the others still hide theirs.
        const qreal hiddenHeight = document.findBlockByNumber(1).layout()->boundingRect().height();
        highlighter.setRevealedBlock(document.findBlockByNumber(1));
        QVERIFY(xAt(document, 1, 7) > xAt(document, 1, 5));
        QCOMPARE(document.findBlockByNumber(1).layout()->boundingRect().height(), hiddenHeight);
        QCOMPARE(xAt(document, 0, 3), xAt(document, 0, 0));

        highlighter.setRevealedBlock(document.findBlockByNumber(0));
        QVERIFY(xAt(document, 0, 3) > xAt(document, 0, 0));
        QCOMPARE(xAt(document, 1, 7), xAt(document, 1, 5));

        // It stays revealed when a line is added above it.
        QTextCursor(&document).insertText(u"new line\n"_s);
        QVERIFY(xAt(document, 1, 3) > xAt(document, 1, 0));
        QCOMPARE(xAt(document, 2, 7), xAt(document, 2, 5));

        // Turned off, all syntax shows.
        highlighter.setHideSyntax(false);
        QVERIFY(xAt(document, 2, 7) > xAt(document, 2, 5));
    }

    void takesColoursFromThePalette()
    {
        QTextDocument document(u"- item"_s);
        MarkdownHighlighter highlighter(&document);
        QPalette palette;
        palette.setColor(QPalette::Link, Qt::red);
        highlighter.setPalette(palette);
        QCOMPARE(formatAt(document, 0, 0).foreground().color(), QColor(Qt::red));

        palette.setColor(QPalette::Link, Qt::green);
        highlighter.setPalette(palette);
        QCOMPARE(formatAt(document, 0, 0).foreground().color(), QColor(Qt::green));
    }

    void editorHighlightsWithoutSaving()
    {
        // Highlighting tells the document its layout changed, which reaches
        // the autosave timer; it must not lead to a save of unchanged text.
        EditorView editor(m_notebook->newPage(), u"# Notes\n- [x] **done**"_s, *m_notebook->writer);
        QSignalSpy saved(m_notebook->writer.get(), &BackgroundWriter::pageSaved);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTRY_VERIFY(isHighlighted(*editor.document()));

        QTest::qWait(1000); // past the autosave delay
        QCOMPARE(saved.size(), 0);
        QVERIFY(editor.markdownHighlighting());
        QVERIFY(!editor.hasUnsavedChanges());
        QVERIFY(!editor.document()->isModified());
        QCOMPARE(editor.toPlainText(), u"# Notes\n- [x] **done**"_s); // the text is untouched
    }

    void editorCanTurnHighlightingOff()
    {
        EditorView editor(m_notebook->newPage(), u"# Notes\n**bold**"_s, *m_notebook->writer);
        QTRY_VERIFY(isHighlighted(*editor.document()));

        editor.setMarkdownHighlighting(false);
        QVERIFY(!editor.markdownHighlighting());
        QVERIFY(!isHighlighted(*editor.document()));

        editor.setMarkdownHighlighting(true);
        QTRY_VERIFY(isHighlighted(*editor.document()));
        QVERIFY(!editor.hasUnsavedChanges());
    }

    void editorRevealsTheLineWithTheCursor()
    {
        EditorView editor(m_notebook->newPage(), u"**one**\n**two**"_s, *m_notebook->writer);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QVERIFY(editor.hidesMarkdownSyntax());
        const QTextDocument &document = *editor.document();
        const auto shows
            = [&document](int line) { return xAt(document, line, 2) > xAt(document, line, 0); };
        QTRY_VERIFY(isHighlighted(document));
        QVERIFY(shows(0)); // the cursor starts on the first line
        QVERIFY(!shows(1));

        QTest::keyClick(&editor, Qt::Key_Down);
        QVERIFY(!shows(0));
        QVERIFY(shows(1));

        // A new line gets the cursor, and with it the syntax.
        QTest::keyClick(&editor, Qt::Key_End);
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClicks(&editor, u"**three**"_s);
        QVERIFY(!shows(1));
        QVERIFY(shows(2));

        QTest::keyClick(&editor, Qt::Key_Home, Qt::ControlModifier);
        QVERIFY(shows(0));
        QVERIFY(!shows(2));

        editor.selectAll(); // moves the cursor to the end
        QVERIFY(!shows(0));
        QVERIFY(shows(2));

        editor.setHideMarkdownSyntax(false);
        QVERIFY(!editor.hidesMarkdownSyntax());
        QVERIFY(shows(0) && shows(1) && shows(2));
        QCOMPARE(editor.toPlainText(), u"**one**\n**two**\n**three**"_s);
    }

    void editorRevealsAClickedLineOnceNoDoubleClickFollows()
    {
        const QString text = u"start\n**a** *b* ~~c~~ word"_s;
        EditorView editor(m_notebook->newPage(), text, *m_notebook->writer);
        editor.resize(600, 200);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        const QTextDocument &document = *editor.document();
        QTRY_VERIFY(isHighlighted(document));
        const auto shows = [&document] { return xAt(document, 1, 2) > xAt(document, 1, 0); };
        QVERIFY(!shows());

        // Where "word" is drawn while its line hides the syntax before it.
        QTextCursor word(editor.document());
        word.setPosition(int(text.indexOf(u"word"_s)) + 1);
        const QPoint point = editor.cursorRect(word).center();

        // Revealing moves the text, so a double-click still finds its word.
        // A mouse sends press, release, double-click, release; QTest's
        // mouseDClick() only the double-click.
        QTest::mousePress(editor.viewport(), Qt::LeftButton, {}, point);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, {}, point);
        QTest::mouseDClick(editor.viewport(), Qt::LeftButton, {}, point);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, {}, point);
        QCOMPARE(editor.textCursor().selectedText(), u"word"_s);
        QTRY_VERIFY(shows());

        // A single click reveals the line a moment later, without selecting.
        QTest::keyClick(&editor, Qt::Key_Home, Qt::ControlModifier);
        QVERIFY(!shows());
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, {}, point);
        QVERIFY(!shows());
        QTRY_VERIFY(shows());
        QVERIFY(!editor.textCursor().hasSelection());
    }

    void selectedHiddenSyntaxDrawsNothing()
    {
        // A selection draws text in its own colour, so hidden syntax must be
        // invisible by itself: selected, a heading made bold with hidden **
        // looks exactly like the heading without them, bold anyway.
        EditorView editor(
            m_notebook->newPage(), u"# **Title**\n# Title\nend"_s, *m_notebook->writer);
        editor.resize(500, 300);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTRY_VERIFY(isHighlighted(*editor.document()));
        editor.selectAll(); // the cursor goes to the end, the line that shows its syntax

        const auto line = [&editor](int number) {
            const QRect cursor
                = editor.cursorRect(QTextCursor(editor.document()->findBlockByNumber(number)));
            const QRect rect(0, cursor.top(), editor.viewport()->width(), cursor.height());
            return editor.viewport()->grab(rect).toImage();
        };
        QCOMPARE(line(0), line(1));
    }

    void headingsGrowWithTheZoom()
    {
        EditorView editor(m_notebook->newPage(), u"# Title\ntext"_s, *m_notebook->writer);
        QTRY_VERIFY(isHighlighted(*editor.document()));
        const qreal normal = formatAt(*editor.document(), 0, 3).fontPointSize();
        QVERIFY(normal > editor.font().pointSizeF());

        editor.setZoom(200);
        QCOMPARE(formatAt(*editor.document(), 0, 3).fontPointSize(), normal * 2);
    }

    void editorFollowsPaletteChanges()
    {
        EditorView editor(m_notebook->newPage(), u"- item"_s, *m_notebook->writer);
        QTRY_VERIFY(isHighlighted(*editor.document()));
        QPalette palette = editor.palette();
        palette.setColor(QPalette::Link, Qt::magenta);
        editor.setPalette(palette);
        QTRY_COMPARE(formatAt(*editor.document(), 0, 0).foreground().color(), QColor(Qt::magenta));
    }
};

QTEST_MAIN(TestMarkdownHighlighter)
#include "tst_markdownhighlighter.moc"
