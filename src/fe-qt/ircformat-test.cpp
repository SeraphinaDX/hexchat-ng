/*
 * Simple headless Qt/IRC formatting regressions (no external IRC needed).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ircformat.h"
#include <QApplication>
#include <QPalette>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QPalette palette;
    auto verify = [](bool ok, const char *what) {
        if (!ok) std::fprintf(stderr, "FAIL: %s\n", what);
        return ok;
    };
    bool ok = true;
    const QString boldText = QStringLiteral("a") + QChar(0x02) +
                             QStringLiteral("b") + QChar(0x02) + QStringLiteral("c");
    const auto bold = parseIrcText(boldText, palette);
    ok &= verify(bold.size() == 3, "bold boundaries");
    if (bold.size() == 3) {
        ok &= verify(bold[0].text == QStringLiteral("a") &&
                     bold[1].text == QStringLiteral("b") &&
                     bold[2].text == QStringLiteral("c"), "visible content");
        ok &= verify(bold[1].format.fontWeight() == QFont::Bold, "bold toggle");
        ok &= verify(bold[2].format.fontWeight() == QFont::Normal, "bold reset");
    }
    const QString colorText = QChar(0x03) + QStringLiteral("04,01RED") +
                              QChar(0x0F) + QStringLiteral("default");
    const auto colored = parseIrcText(colorText, palette);
    ok &= verify(colored.size() == 2, "mIRC color boundaries");
    if (colored.size() == 2) {
        ok &= verify(colored[0].format.foreground().color() == QColor("#FF0000"),
                     "foreground color 04");
        ok &= verify(colored[0].format.background().color() == QColor("#000000"),
                     "background color 01");
        ok &= verify(colored[1].format.foreground().color() == palette.color(QPalette::Text),
                     "reset color");
    }
    const auto hex = parseIrcText(QChar(0x04) + QStringLiteral("12ABEFtest"), palette);
    ok &= verify(hex.size() == 1 && hex[0].text == QStringLiteral("test") &&
                 hex[0].format.foreground().color() == QColor("#12ABEF"), "hex color");
    const auto bad = parseIrcText(QStringLiteral("<script>alert(1)</script>"), palette);
    ok &= verify(bad.size() == 1 && bad[0].text == QStringLiteral("<script>alert(1)</script>"),
                 "raw text kept literally");
    QTextDocument document;
    document.setMaximumBlockCount(10);
    appendIrcMessage(&document, QStringLiteral("go to https://example.com/test."), 0, palette);
    ok &= verify(document.toPlainText() == QStringLiteral("go to https://example.com/test."),
                 "text roundtrip");
    bool foundLink = false;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto frag = block.begin(); !frag.atEnd(); ++frag) {
            const QTextFragment part = frag.fragment();
            if (part.isValid() && part.charFormat().isAnchor() &&
                part.charFormat().anchorHref() == QStringLiteral("https://example.com/test"))
                foundLink = true;
        }
    }
    ok &= verify(foundLink, "clickable link without trailing period");
    appendIrcMessage(&document, QStringLiteral("second line\n"), 0, palette);
    ok &= verify(document.blockCount() == 2 &&
                 document.toPlainText().endsWith(QStringLiteral("second line")),
                 "newline handling");
    return ok ? 0 : 1;
}
