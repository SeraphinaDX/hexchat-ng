/*
 * Qt-side mIRC formatting: foreground/background colours, true-colour,
 * bold, italic, underline, strikethrough, reverse and monospace.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ircformat.h"

#include <QDateTime>
#include <QFont>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

namespace {
struct Style {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool monospace = false;
    bool reverse = false;
    QColor fg;
    QColor bg;
};

QColor mircColor(int number)
{
    static const char *const colors[] = {
        "#FFFFFF", "#000000", "#00007F", "#009300",
        "#FF0000", "#7F0000", "#9C009C", "#FC7F00",
        "#FFFF00", "#00FC00", "#009393", "#00FFFF",
        "#0000FC", "#FF00FF", "#7F7F7F", "#D2D2D2"
    };
    if (number < 0 || number >= 16) return QColor();
    return QColor(QString::fromLatin1(colors[number]));
}

QTextCharFormat toFormat(const Style &style, const QPalette &palette)
{
    QTextCharFormat format;
    format.setFontWeight(style.bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(style.italic);
    format.setFontUnderline(style.underline);
    format.setFontStrikeOut(style.strike);
    format.setFontFixedPitch(style.monospace);
    const QColor foreground = style.fg.isValid() ? style.fg : palette.color(QPalette::Text);
    const QColor background = style.bg.isValid() ? style.bg : palette.color(QPalette::Base);
    format.setForeground(style.reverse ? background : foreground);
    if (style.reverse || style.bg.isValid())
        format.setBackground(style.reverse ? foreground : background);
    return format;
}

bool digit(QChar c) { return c.unicode() >= '0' && c.unicode() <= '9'; }

int readDecimal(const QString &text, qsizetype &at)
{
    int value = 0;
    int count = 0;
    while (at < text.size() && count < 2 && digit(text.at(at))) {
        value = value * 10 + text.at(at).unicode() - '0';
        ++at;
        ++count;
    }
    return count ? value : -1;
}

QColor readHex(const QString &text, qsizetype &at)
{
    if (at + 6 > text.size()) return QColor();
    const QString value = text.mid(at, 6);
    for (QChar c : value) {
        const ushort u = c.unicode();
        if (!digit(c) && !(u >= 'a' && u <= 'f') && !(u >= 'A' && u <= 'F'))
            return QColor();
    }
    at += 6;
    return QColor(QLatin1Char('#') + value);
}

void appendLinkedText(QTextCursor &cursor, const IrcRun &run, const QPalette &palette)
{
    // Format URLs as anchors, not HTML, so IRC messages cannot inject markup.
    static const QRegularExpression urlPattern(
        QStringLiteral(R"((?:https?://|ircs?://|www\.)[^\s<>"]+)"),
        QRegularExpression::CaseInsensitiveOption);
    qsizetype pos = 0;
    auto matches = urlPattern.globalMatch(run.text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedStart() > pos)
            cursor.insertText(run.text.mid(pos, match.capturedStart() - pos), run.format);
        QString url = match.captured();
        while (!url.isEmpty() && QStringLiteral(".,;:!?)]").contains(url.back()))
            url.chop(1);
        if (url.isEmpty()) continue;
        QTextCharFormat link = run.format;
        link.setAnchor(true);
        link.setAnchorHref(url.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)
                           ? QStringLiteral("https://") + url : url);
        link.setFontUnderline(true);
        link.setForeground(palette.color(QPalette::Link));
        cursor.insertText(url, link);
        const qsizetype trailing = match.capturedLength() - url.size();
        if (trailing)
            cursor.insertText(match.captured().right(trailing), run.format);
        pos = match.capturedEnd();
    }
    if (pos < run.text.size())
        cursor.insertText(run.text.mid(pos), run.format);
}
} // namespace

QVector<IrcRun> parseIrcText(const QString &text, const QPalette &palette)
{
    Style style;
    QVector<IrcRun> result;
    QString buffer;
    auto flush = [&]() {
        if (!buffer.isEmpty()) {
            result.push_back({buffer, toFormat(style, palette)});
            buffer.clear();
        }
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const ushort c = text.at(i).unicode();
        switch (c) {
        case 0x02: flush(); style.bold = !style.bold; break;
        case 0x1D: flush(); style.italic = !style.italic; break;
        case 0x1F: flush(); style.underline = !style.underline; break;
        case 0x1E: flush(); style.strike = !style.strike; break;
        case 0x11: flush(); style.monospace = !style.monospace; break;
        case 0x16: flush(); style.reverse = !style.reverse; break;
        case 0x0F: flush(); style = Style{}; break;
        case 0x03: {
            flush();
            qsizetype next = i + 1;
            const int fg = readDecimal(text, next);
            style.fg = mircColor(fg);
            style.bg = QColor();
            if (fg >= 0 && next < text.size() && text.at(next) == QLatin1Char(',')) {
                qsizetype after = next + 1;
                const int bg = readDecimal(text, after);
                if (bg >= 0) {
                    style.bg = mircColor(bg);
                    next = after;
                }
            }
            i = next - 1;
            break;
        }
        case 0x04: {
            flush();
            qsizetype next = i + 1;
            style.fg = readHex(text, next);
            style.bg = QColor();
            if (style.fg.isValid() && next < text.size() && text.at(next) == QLatin1Char(',')) {
                qsizetype after = next + 1;
                QColor bg = readHex(text, after);
                if (bg.isValid()) {
                    style.bg = bg;
                    next = after;
                }
            }
            i = next - 1;
            break;
        }
        case 0x00: case 0x07: case 0x08:
            // NUL, bell and hidden-text marker are not displayed.
            break;
        case '\r':
            // IRC uses CRLF; QTextDocument uses '\n'.
            break;
        default:
            buffer += text.at(i);
            break;
        }
    }
    flush();
    return result;
}

void appendIrcMessage(QTextDocument *document, const QString &raw,
                      std::time_t timestamp, const QPalette &palette)
{
    if (!document || raw.isEmpty()) return;
    QString text = raw;
    while (!text.isEmpty() && (text.endsWith(QLatin1Char('\n')) ||
                                text.endsWith(QLatin1Char('\r'))))
        text.chop(1);
    if (text.isEmpty()) return;

    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::End);
    if (!document->isEmpty()) cursor.insertBlock();

    if (timestamp > 0) {
        QTextCharFormat timeFormat;
        timeFormat.setForeground(palette.color(QPalette::Mid));
        const QString time = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(timestamp)).toLocalTime().toString(QStringLiteral("HH:mm"));
        cursor.insertText(QLatin1Char('[') + time + QStringLiteral("] "), timeFormat);
    }
    for (const IrcRun &run : parseIrcText(text, palette))
        appendLinkedText(cursor, run, palette);
}
