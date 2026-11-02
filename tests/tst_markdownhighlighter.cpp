// SPDX-License-Identifier: MIT
#include "TestNotebook.h"
#include "ui/EditorView.h"
#include "ui/MarkdownHighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolTip>

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

/// How many pixels of @p image within @p rect satisfy @p test.
int countPixels(const QImage &image, const QRect &rect, auto test)
{
    int count = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x)
            count += test(image.pixelColor(x, y)) ? 1 : 0;
    }
    return count;
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

public slots:
    /// Stands in for the browser, so tests can check what would open.
    void recordUrl(const QUrl &url) { m_opened.append(url); }

private:
    std::unique_ptr<TestNotebook> m_notebook;
    QList<QUrl> m_opened;

private slots:
    void init()
    {
        m_notebook = std::make_unique<TestNotebook>();
        QVERIFY(m_notebook->open());
    }

    void cleanup()
    {
        m_notebook.reset();
        m_opened.clear();
    }

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

    void symbolsTakeThePlaceOfTheirSyntax()
    {
        const QString text = u"- item\n"
                             u"- [ ] open\n"
                             u"- [x] done\n"
                             u"> > quote\n"
                             u">\n"
                             u"---\n"
                             u"plain"_s;
        QTextDocument document(text);
        MarkdownHighlighter highlighter(&document);
        highlighter.setRevealedBlock(document.findBlockByNumber(6));
        const auto revealedX = [&](int line, int position) {
            highlighter.setRevealedBlock(document.findBlockByNumber(line));
            const qreal x = xAt(document, line, position);
            highlighter.setRevealedBlock(document.findBlockByNumber(6));
            return x;
        };
        const auto symbols = [&](int line) {
            QStringList parts;
            const QTextBlock block = document.findBlockByNumber(line);
            for (const markdown::Span &symbol : highlighter.symbols(block))
                parts.append(block.text().mid(symbol.start, symbol.length));
            return parts;
        };
        QVERIFY(symbols(0).isEmpty()); // only while hidden
        highlighter.setHideSyntax(true);

        QCOMPARE(symbols(0), QStringList {u"-"_s});
        QCOMPARE(symbols(1), QStringList {u"[ ]"_s});
        QCOMPARE(highlighter.symbols(document.findBlockByNumber(2)).constFirst().style,
            markdown::Style::CheckedBoxSymbol);
        QCOMPARE(symbols(3), (QStringList {u">"_s, u">"_s}));
        QCOMPARE(symbols(5), QStringList {u"---"_s});
        QVERIFY(symbols(6).isEmpty());

        // A bullet keeps its place, so the text does not move when it shows.
        QCOMPARE(xAt(document, 0, 2), revealedX(0, 2));
        QVERIFY(xAt(document, 0, 1) > xAt(document, 0, 0));
        // A checkbox stands for the bullet too, and is as wide checked as not.
        QCOMPARE(xAt(document, 1, 2), xAt(document, 1, 0));
        QVERIFY(xAt(document, 1, 6) < revealedX(1, 6));
        QCOMPARE(xAt(document, 1, 5) - xAt(document, 1, 2), revealedX(1, 5) - revealedX(1, 2));
        QCOMPARE(xAt(document, 2, 6), xAt(document, 1, 6));
        // Quote bars go beside the text.
        QCOMPARE(xAt(document, 3, 4), xAt(document, 3, 0));

        // Lines keep their height, even with nothing left on them.
        const auto height = [&document](int line) {
            const QTextBlock block = document.findBlockByNumber(line);
            std::ignore = document.documentLayout()->blockBoundingRect(block);
            return block.layout()->boundingRect().height();
        };
        for (int line = 0; line < 6; ++line)
            QCOMPARE(height(line), height(6));
        QCOMPARE(xAt(document, 5, 3), xAt(document, 5, 0) + revealedX(5, 3) - revealedX(5, 0));

        // A revealed block shows its syntax, not symbols.
        highlighter.setRevealedBlock(document.findBlockByNumber(0));
        QVERIFY(symbols(0).isEmpty());
        QCOMPARE(symbols(1), QStringList {u"[ ]"_s});

        highlighter.setHideSyntax(false);
        QVERIFY(symbols(1).isEmpty());
        highlighter.setHideSyntax(true);
        QCOMPARE(symbols(1), QStringList {u"[ ]"_s});
        // Not even blocks of a document the highlighter left.
        highlighter.setDocument(nullptr);
        QVERIFY(symbols(1).isEmpty());
        QCOMPARE(document.toPlainText(), text);
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

    void editorDrawsSymbols()
    {
        const QString text = u"start\n"
                             u"- item\n"
                             u"- [ ] open\n"
                             u"- [x] done\n"
                             u"> a quote long enough to wrap onto a second line\n"
                             u"---\n"
                             u"end"_s;
        EditorView editor(m_notebook->newPage(), text, *m_notebook->writer);
        // Bullets and checkboxes take the link colour; bars and rules the
        // text colour, dimmed, which is grey here.
        QPalette palette = editor.palette();
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::Text, Qt::black);
        palette.setColor(QPalette::Link, Qt::red);
        editor.setPalette(palette);
        editor.resize(300, 400);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTRY_VERIFY(isHighlighted(*editor.document()));

        const auto at = [&editor](int line, int position) {
            QTextCursor cursor(editor.document()->findBlockByNumber(line));
            cursor.setPosition(cursor.position() + position);
            return editor.cursorRect(cursor);
        };
        const auto red = [](const QColor &c) { return c.red() - c.green() > 100; };
        const auto grey = [](const QColor &c) {
            return c.red() == c.green() && c.green() == c.blue() && c.red() > 60 && c.red() < 230;
        };
        const auto cell = [&at](int line, int from, int to) {
            return QRect(QPoint(at(line, from).left(), at(line, from).top()),
                QPoint(at(line, to).left() - 1, at(line, from).bottom()));
        };
        const int quoteEnd = editor.document()->findBlockByNumber(4).length() - 1;
        const QRect quote(
            QPoint(0, at(4, 0).top()), QPoint(at(4, 2).left() - 1, at(4, quoteEnd).bottom()));
        QVERIFY(at(4, quoteEnd).top() > at(4, 0).top()); // it wraps
        const auto ruleRow = [&](const QImage &image) {
            int longest = 0;
            for (int y = at(5, 0).top(); y <= at(5, 0).bottom(); ++y) {
                longest
                    = std::max(longest, countPixels(image, QRect(0, y, image.width(), 1), grey));
            }
            return longest;
        };

        QImage image = editor.viewport()->grab().toImage();
        QCOMPARE(countPixels(image, cell(0, 0, 5), red), 0);
        QVERIFY(countPixels(image, cell(1, 0, 1), red) > 0); // a bullet
        const int open = countPixels(image, cell(2, 2, 5), red);
        QVERIFY(open > 0);
        QVERIFY(countPixels(image, cell(3, 2, 5), red) > open * 3 / 2); // filled
        // A bar left of the quote, along both of its lines, with room
        // between it and the text.
        const QRect gap(quote.right() - 5, quote.top(), 6, quote.height());
        QCOMPARE(countPixels(image, gap, grey), 0);
        for (int y = quote.top(); y <= quote.bottom(); ++y)
            QVERIFY(countPixels(image, QRect(0, y, quote.width(), 1), grey) > 0);
        QVERIFY(ruleRow(image) > editor.viewport()->width() * 3 / 4);

        editor.setHideMarkdownSyntax(false);
        image = editor.viewport()->grab().toImage();
        QCOMPARE(countPixels(image, quote, grey), 0);
        QVERIFY(ruleRow(image) < editor.viewport()->width() / 4);
    }

    void editorTogglesATaskWhenItsCheckboxIsClicked()
    {
        EditorView editor(
            m_notebook->newPage(), u"start\n- [ ] open\n* [X] done"_s, *m_notebook->writer);
        editor.resize(400, 300);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTRY_VERIFY(isHighlighted(*editor.document()));
        const auto checkbox = [&editor](int line) {
            QTextCursor cursor(editor.document()->findBlockByNumber(line));
            const QRect left = editor.cursorRect(cursor);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
            return QPoint((left.left() + editor.cursorRect(cursor).left()) / 2, left.center().y());
        };
        const auto line
            = [&editor](int number) { return editor.document()->findBlockByNumber(number).text(); };

        QTest::mouseClick(editor.viewport(), Qt::LeftButton, {}, checkbox(1));
        QCOMPARE(line(1), u"- [x] open"_s);
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, {}, checkbox(2));
        QCOMPARE(line(2), u"* [ ] done"_s);
        // The cursor stays, and with it the line that shows its syntax.
        QCOMPARE(editor.textCursor().position(), 0);
        QVERIFY(!editor.textCursor().hasSelection());
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QCOMPARE(editor.textCursor().position(), 0);

        // Each click toggles, as part of a double-click too.
        const QPoint open = checkbox(1);
        QTest::mousePress(editor.viewport(), Qt::LeftButton, {}, open);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, {}, open);
        QTest::mouseDClick(editor.viewport(), Qt::LeftButton, {}, open);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, {}, open);
        QCOMPARE(line(1), u"- [x] open"_s);
        QVERIFY(!editor.textCursor().hasSelection());

        // Dragging from a checkbox does not select.
        QTest::mousePress(editor.viewport(), Qt::LeftButton, {}, open);
        QMouseEvent move(QEvent::MouseMove, checkbox(2) + QPoint(80, 0),
            editor.viewport()->mapToGlobal(checkbox(2) + QPoint(80, 0)), Qt::NoButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &move);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, {}, checkbox(2));
        QCOMPARE(line(1), u"- [ ] open"_s);
        QVERIFY(!editor.textCursor().hasSelection());

        // Each toggle is an edit, which is saved and can be undone.
        QVERIFY(editor.hasUnsavedChanges());
        editor.undo();
        QCOMPARE(line(1), u"- [x] open"_s);

        // The pointer shows where a click toggles.
        QTest::mouseMove(editor.viewport(), checkbox(2));
        QTRY_COMPARE(editor.viewport()->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseMove(editor.viewport(), checkbox(2) + QPoint(80, 0));
        QTRY_COMPARE(editor.viewport()->cursor().shape(), Qt::IBeamCursor);

        // On the line being edited, the brackets are text like any other.
        QTest::keyClick(&editor, Qt::Key_Home, Qt::ControlModifier);
        QTest::keyClick(&editor, Qt::Key_Down);
        const QPoint revealed = checkbox(1);
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, {}, revealed);
        QCOMPARE(line(1), u"- [x] open"_s);
        QCOMPARE(editor.textCursor().block().blockNumber(), 1);
    }

    void editorOpensALinkWhenItIsCtrlClicked()
    {
        for (const QString &scheme : {u"https"_s, u"mailto"_s})
            QDesktopServices::setUrlHandler(scheme, this, "recordUrl");
        const auto unsetHandlers = qScopeGuard([] {
            for (const QString &scheme : {u"https"_s, u"mailto"_s})
                QDesktopServices::unsetUrlHandler(scheme);
        });
        EditorView editor(m_notebook->newPage(),
            u"start\nsee [the docs](https://example.com/docs) or https://example.org\n"
            "[mail](mailto:me@example.com), [a note](notes.md) [site](www.example.net)\n```\nhttps://in.code\n```"_s,
            *m_notebook->writer);
        editor.resize(600, 300);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTRY_VERIFY(isHighlighted(*editor.document()));
        // The middle of a character of the text.
        const auto at = [&editor](int line, const QString &text) {
            const QTextBlock block = editor.document()->findBlockByNumber(line);
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + int(block.text().indexOf(text)));
            const QRect left = editor.cursorRect(cursor);
            cursor.movePosition(QTextCursor::NextCharacter);
            return QPoint((left.left() + editor.cursorRect(cursor).left()) / 2, left.center().y());
        };
        const QUrl docs(u"https://example.com/docs"_s);

        // Its text opens a link whose URL is hidden, and the cursor stays.
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(1, u"docs"_s));
        QCOMPARE(m_opened, QList<QUrl> {docs});
        QCOMPARE(editor.textCursor().position(), 0);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QCOMPARE(editor.textCursor().position(), 0);

        // Once, when double-clicked.
        const QPoint the = at(1, u"the"_s);
        QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, the);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, the);
        QTest::mouseDClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, the);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, the);
        QCOMPARE(m_opened, (QList<QUrl> {docs, docs}));
        QVERIFY(!editor.textCursor().hasSelection());

        // Without Ctrl, a click places the cursor, as in any text.
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, {}, at(1, u"docs"_s));
        QCOMPARE(m_opened.size(), 2);
        QCOMPARE(editor.textCursor().block().blockNumber(), 1);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);

        // On the line being edited, its URL opens it too, as does a bare one.
        QTest::mouseClick(
            editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(1, u"example.com"_s));
        QTest::mouseClick(
            editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(1, u"example.org"_s));
        QCOMPARE(m_opened, (QList<QUrl> {docs, docs, docs, QUrl(u"https://example.org"_s)}));
        m_opened.clear();

        // Only links open: not the text beside them, not a page that is
        // not on the web, not a URL in code.
        const QPoint pastTheEnd = at(1, u"org"_s) + QPoint(60, 0);
        const QPoint beforeTheStart(
            editor.cursorRect(QTextCursor(editor.document()->findBlockByNumber(2))).left() - 10,
            at(2, u"mail"_s).y());
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(1, u"see"_s));
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, pastTheEnd);
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, beforeTheStart);
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(2, u"note"_s));
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(4, u"in"_s));
        QVERIFY(m_opened.isEmpty());
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(2, u"mail"_s));
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(2, u"site"_s));
        QCOMPARE(m_opened,
            (QList<QUrl> {QUrl(u"mailto:me@example.com"_s), QUrl(u"https://www.example.net"_s)}));
        m_opened.clear();

        // The pointer shows where Ctrl+click opens a link.
        QTest::keyClick(&editor, Qt::Key_Home, Qt::ControlModifier);
        const QPoint link = at(1, u"docs"_s);
        const auto hover = [&editor](const QPoint &point, Qt::KeyboardModifiers modifiers) {
            QMouseEvent move(QEvent::MouseMove, point, editor.viewport()->mapToGlobal(point),
                Qt::NoButton, Qt::NoButton, modifiers);
            QApplication::sendEvent(editor.viewport(), &move);
            return editor.viewport()->cursor().shape();
        };
        QCOMPARE(hover(link, {}), Qt::IBeamCursor);
        QCOMPARE(hover(link, Qt::ControlModifier), Qt::PointingHandCursor);
        QCOMPARE(hover(at(1, u"see"_s), Qt::ControlModifier), Qt::IBeamCursor);
        QCOMPARE(hover(link, Qt::ControlModifier), Qt::PointingHandCursor);
        // Pressing or releasing Ctrl changes it, without moving the mouse.
        QCursor::setPos(editor.viewport()->mapToGlobal(link));
        QTest::keyRelease(&editor, Qt::Key_Control);
        QCOMPARE(editor.viewport()->cursor().shape(), Qt::IBeamCursor);
        QTest::keyPress(&editor, Qt::Key_Control);
        QCOMPARE(editor.viewport()->cursor().shape(), Qt::PointingHandCursor);
        QTest::keyRelease(&editor, Qt::Key_Control);
        QCursor::setPos(editor.viewport()->mapToGlobal(at(1, u"see"_s)));
        QTest::keyPress(&editor, Qt::Key_Control);
        QCOMPARE(editor.viewport()->cursor().shape(), Qt::IBeamCursor);
        QTest::keyRelease(&editor, Qt::Key_Control);

        // Hovering it says where it goes.
        QHelpEvent help(QEvent::ToolTip, link, editor.viewport()->mapToGlobal(link));
        QApplication::sendEvent(editor.viewport(), &help);
        QVERIFY(QToolTip::text().contains(docs.toString()));
        QVERIFY(QToolTip::text().contains(u"Ctrl+click"_s));

        // Links are found as they are highlighted.
        editor.setMarkdownHighlighting(false);
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at(1, u"docs"_s));
        QVERIFY(m_opened.isEmpty());
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
