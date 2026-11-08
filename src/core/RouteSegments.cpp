#include "RouteSegments.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace watt {

QString RouteSegment::displayName(int number) const
{
    if (!name.trimmed().isEmpty())
        return name.trimmed();
    return (kind == Kom ? QStringLiteral("KOM %1") : kind == Gravel ? QStringLiteral("Gravel %1") : QStringLiteral("Sprint %1"))
        .arg(number);
}

namespace segments {

bool validate(QVector<RouteSegment> &list, double totalM, QString *error)
{
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    std::stable_sort(list.begin(), list.end(), [](const RouteSegment &a, const RouteSegment &b) { return a.start < b.start; });
    int numbers[3] = { 0, 0, 0 };   // per kind, for the names
    // per class (timed / gravel): the one before
    int previous[2] = { -1, -1 };
    QString previousName[2];
    for (int i = 0; i < list.size(); ++i) {
        const RouteSegment &s = list.at(i);
        const QString what = s.displayName(++numbers[s.kind]);
        if (!std::isfinite(s.start) || !std::isfinite(s.finish) || s.start < 0 || s.finish > totalM + 0.5)
            return fail(QStringLiteral("%1 lies outside the route.").arg(what));
        if (s.length() < kMinLengthM)
            return fail(QStringLiteral("%1 is shorter than %2 m.").arg(what).arg(int(kMinLengthM)));
        const int cls = s.timed() ? 0 : 1;
        if (previous[cls] >= 0 && s.start < list.at(previous[cls]).finish)
            return fail(QStringLiteral("%1 overlaps %2.").arg(what, previousName[cls]));
        previous[cls] = i;
        previousName[cls] = what;
    }
    return true;
}

namespace {
// XML-escaped and pure ASCII (non-ASCII as character references), so the
// block is right whatever encoding the file declares
QString esc(const QString &s)
{
    const QString e = s.toHtmlEscaped();
    QString out;
    for (const uint c : e.toUcs4())
        out += c < 128 ? QString(QChar(char16_t(c))) : QStringLiteral("&#x%1;").arg(c, 0, 16);
    return out;
}
} // namespace

bool writeToGpx(const QString &path, const QVector<RouteSegment> &list,
                const std::function<std::pair<double, double>(double)> &positionAt, QString *error, double maxGrade)
{
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Cannot read %1").arg(path));
    // byte for byte (Latin-1 maps each byte to one character): UTF-8,
    // ISO-8859-1 and Windows-1252 files pass through unchanged; all markers are ASCII
    QString text = QString::fromLatin1(in.readAll());
    in.close();
    // remove segments written before - exactly as written below (so the file
    // is as it was), then any other formatting of them
    static const QRegularExpression ownExtensions(QStringLiteral(
        R"([ \t]*<extensions>\s*<artilody:segments\b[^>]*>.*?</artilody:segments>\s*</extensions>[ \t]*\r?\n?)"),
                                                  QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression ownBlock(
        QStringLiteral(R"(\r?\n[ \t]*<artilody:segments\b[^>]*>.*?</artilody:segments>[ \t]*(?=\r?\n))"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression anyBlock(QStringLiteral(R"(<artilody:segments\b[^>]*>.*?</artilody:segments>)"),
                                             QRegularExpression::DotMatchesEverythingOption);
    text.remove(ownExtensions);
    text.remove(ownBlock);
    text.remove(anyBlock);
    const int end = text.lastIndexOf(QLatin1String("</gpx>"));
    if (end < 0)
        return fail(QStringLiteral("%1 is not a GPX file (no </gpx>).").arg(path));
    if (!list.isEmpty() || maxGrade > 0) {
        QString block = QStringLiteral("  <artilody:segments xmlns:artilody=\"%1\"").arg(QLatin1String(kNamespace));
        if (maxGrade > 0)
            block += QStringLiteral(" maxGrade=\"%1\"").arg(maxGrade, 0, 'f', 1);
        block += QStringLiteral(">\n");
        for (const RouteSegment &s : list) {
            block += QStringLiteral("    <artilody:segment kind=\"%1\" start=\"%2\" finish=\"%3\"")
                         .arg(s.kindName())
                         .arg(s.start, 0, 'f', 1)
                         .arg(s.finish, 0, 'f', 1);
            if (!s.name.trimmed().isEmpty())
                block += QStringLiteral(" name=\"%1\"").arg(esc(s.name.trimmed()));
            if (positionAt) {
                const auto a = positionAt(s.start), b = positionAt(s.finish);
                block += QStringLiteral(" startLat=\"%1\" startLon=\"%2\" finishLat=\"%3\" finishLon=\"%4\"")
                             .arg(a.first, 0, 'f', 7)
                             .arg(a.second, 0, 'f', 7)
                             .arg(b.first, 0, 'f', 7)
                             .arg(b.second, 0, 'f', 7);
            }
            block += QStringLiteral("/>\n");
        }
        block += QStringLiteral("  </artilody:segments>\n");
        // GPX 1.1: the root's own <extensions> is its last child - add ours to it, or add one
        static const QRegularExpression rootExt(QStringLiteral(R"(</extensions>\s*$)"));
        const QString head = text.left(end);
        const auto m = rootExt.match(head);
        if (m.hasMatch() && head.lastIndexOf(QLatin1String("</trk>")) < m.capturedStart()
            && head.lastIndexOf(QLatin1String("</rte>")) < m.capturedStart()
            && head.lastIndexOf(QLatin1String("</wpt>")) < m.capturedStart())
            text.insert(m.capturedStart(), block);   // (after the line before: removed as written)
        else
            text.insert(end, QStringLiteral("<extensions>\n") + block + QStringLiteral("</extensions>\n"));
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Cannot write %1").arg(path));
    out.write(text.toLatin1());
    if (!out.commit())
        return fail(QStringLiteral("Cannot write %1").arg(path));
    return true;
}

} // namespace segments

} // namespace watt
