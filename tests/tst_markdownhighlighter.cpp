// SPDX-License-Identifier: MIT
#include "TestNotebook.h"
#include "ui/EditorView.h"
#include "ui/MarkdownHighlighter.h"

#include <QFontDatabase>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

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
