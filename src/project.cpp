#include "project.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <fstream>
#include <sstream>

namespace {

QString xmlEscape(QString s)
{
    s.remove('\r');
    return s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('\n', "&#10;");
}

// Span of the <Text Id="id"> element in raw, or {-1, 0}.
QPair<int, int> textSpan(const QString& raw, const QString& id)
{
    QRegularExpression re("<Text Id=\"" + QRegularExpression::escape(id) + "\"[^>]*?(/>|>[\\s\\S]*?</Text>)");
    auto m = re.match(raw);
    return m.hasMatch() ? qMakePair(int(m.capturedStart()), int(m.capturedLength())) : qMakePair(-1, 0);
}

} // namespace

bool TextDb::load(const QString& p)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return false;
    path = p;
    raw = QString::fromUtf8(f.readAll());
    parse();
    return true;
}

bool TextDb::save() const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(raw.toUtf8());
    return true;
}

void TextDb::parse()
{
    entries.clear();
    typographies.clear();
    typographyFont.clear();
    language.clear();
    QXmlStreamReader x(raw);
    QString id;
    while (!x.atEnd()) {
        if (x.readNext() != QXmlStreamReader::StartElement) continue;
        const auto a = x.attributes();
        const QStringView n = x.name();
        if (n == u"Language" && language.isEmpty()) {
            language = a.value("Id").toString();
        } else if (n == u"Text") {
            id = a.value("Id").toString();
            entries[id] = {a.value("Alignment").toString(), a.value("TypographyId").toString(), {}};
        } else if (n == u"Translation" && a.value("Language") == language) {
            entries[id].text = x.readElementText();
        } else if (n == u"Typography") {
            const QString t = a.value("Id").toString();
            typographies << t;
            typographyFont[t] = {a.value("Font").toString(), a.value("Size").toInt()};
        }
    }
}

void TextDb::set(const QString& id, const TextEntry& e)
{
    auto [start, len] = textSpan(raw, id);
    if (start < 0) return;
    static const QRegularExpression align("Alignment=\"[^\"]*\""), typo("TypographyId=\"[^\"]*\"");
    const QString el = raw.mid(start, len);
    const int tagEnd = el.indexOf('>');
    QString tag = el.left(tagEnd), body = el.mid(tagEnd); // attributes are edited in start tags only
    tag.replace(align, "Alignment=\"" + e.alignment + "\"");
    tag.replace(typo, "TypographyId=\"" + e.typography + "\"");
    QRegularExpression tr("<Translation Language=\"" + QRegularExpression::escape(language) + "\"([^>]*)>([\\s\\S]*?)</Translation>");
    auto m = tr.match(body);
    if (m.hasMatch()) {
        QString attrs = m.captured(1);
        attrs.replace(align, "Alignment=\"" + e.alignment + "\""); // per-language override, if any
        // splice, never a regex replacement string: user text may contain backslashes
        body.replace(m.capturedStart(1), m.capturedEnd(2) - m.capturedStart(1), attrs + ">" + xmlEscape(e.text));
    }
    raw.replace(start, len, tag + body);
    parse();
}

QString TextDb::add(const TextEntry& e)
{
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString id;
    do {
        id = "__SingleUse_";
        for (int i = 0; i < 4; ++i) id += chars[QRandomGenerator::global()->bounded(36)];
    } while (entries.contains(id));

    int group = raw.indexOf("<TextGroup Id=\"Unsorted\">");
    int end = raw.indexOf("</TextGroup>", group < 0 ? 0 : group);
    if (end < 0) return {};
    int lineStart = raw.lastIndexOf('\n', end) + 1;
    const QString nl = raw.contains("\r\n") ? "\r\n" : "\n"; // Windows Designer files are CRLF
    raw.insert(lineStart, "      <Text Id=\"" + id + "\" Alignment=\"" + e.alignment + "\" TypographyId=\"" + e.typography +
                              "\">" + nl + "        <Translation Language=\"" + language + "\">" + xmlEscape(e.text) +
                              "</Translation>" + nl + "      </Text>" + nl);
    parse();
    return id;
}

void TextDb::remove(const QString& id)
{
    auto [start, len] = textSpan(raw, id);
    if (start < 0) return;
    int from = raw.lastIndexOf('\n', start) + 1; // take the indentation and the line break too
    int to = start + len;
    if (to < raw.size() && raw[to] == '\r') ++to;
    if (to < raw.size() && raw[to] == '\n') ++to;
    raw.remove(from, to - from);
    parse();
}

bool Project::load(const QString& f)
{
    std::ifstream in(f.toStdString(), std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    Json d = Json::parse(ss.str(), nullptr, false);
    if (d.is_discarded() || !d.contains("Application") || !d["Application"].contains("Screens") ||
        !d["Application"]["Screens"].is_array() || d["Application"]["Screens"].empty())
        return false;
    TextDb t;
    if (!t.load(QFileInfo(f).absolutePath() + "/assets/texts/texts.xml")) return false;
    // Only now replace the open project, so a failed open leaves it untouched.
    doc = std::move(d);
    texts = std::move(t);
    file = f;
    dir = QFileInfo(f).absolutePath();
    pixmaps.clear();
    return true;
}

bool Project::save() const
{
    std::ofstream out(file.toStdString(), std::ios::binary);
    out << doc.dump(2); // same layout as the Designer writes: 2 spaces, no trailing newline
    return out.good() && texts.save();
}

QPixmap Project::image(QString rel)
{
    rel.replace('\\', '/');
    auto it = pixmaps.find(rel);
    if (it == pixmaps.end()) it = pixmaps.insert(rel, QPixmap(assets("images/" + rel)));
    return *it;
}

QFont Project::font(const QString& typography)
{
    auto [file, size] = texts.typographyFont.value(typography, {QString(), 20});
    if (!families.contains(file)) {
        int id = QFontDatabase::addApplicationFont(assets("fonts/" + file));
        families[file] = id < 0 ? QString() : QFontDatabase::applicationFontFamilies(id).value(0);
    }
    QFont f(families[file]);
    f.setPixelSize(qMax(1, size));
    return f;
}

QStringList Project::imageFiles() const
{
    QStringList out;
    const QString root = assets("images/");
    QDirIterator it(root, {"*.png", "*.jpg", "*.jpeg", "*.bmp"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next().mid(root.size()).replace('/', '\\');
    out.sort();
    return out;
}

static void collectNames(const Json& comps, QSet<QString>& names)
{
    for (const Json& c : comps) {
        names << QString::fromStdString(c.value("Name", ""));
        if (c.contains("Components")) collectNames(c["Components"], names);
    }
}

bool Project::hasName(const QString& name) const
{
    QSet<QString> names;
    for (const Json& s : doc["Application"]["Screens"]) {
        names << QString::fromStdString(s.value("Name", ""));
        collectNames(s.value("Components", Json::array()), names);
    }
    return names.contains(name);
}

QString Project::makeName(const QString& base) const
{
    QSet<QString> names;
    for (const Json& s : doc["Application"]["Screens"]) collectNames(s.value("Components", Json::array()), names);
    for (int i = 1;; ++i)
        if (!names.contains(base + QString::number(i))) return base + QString::number(i);
}

void setKey(Json& w, const std::string& key, const Json& value, const Json& def)
{
    if (value == def) {
        w.erase(key);
        return;
    }
    if (w.contains(key)) {
        w[key] = value;
        return;
    }
    static const std::vector<std::string> canon{"Type", "Name", "X", "Y", "Width", "Height", "Visible", "LockPosition"};
    auto rank = [](const std::string& k) {
        auto it = std::find(canon.begin(), canon.end(), k);
        return it == canon.end() ? (int)canon.size() : int(it - canon.begin());
    };
    const int r = rank(key);
    Json out = Json::object();
    bool done = false;
    for (auto& [k, v] : w.items()) {
        if (!done && rank(k) > r) {
            out[key] = value;
            done = true;
        }
        out[k] = v;
    }
    if (!done) out[key] = value;
    w = std::move(out);
}
