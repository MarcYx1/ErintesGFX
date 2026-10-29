#pragma once
#include "generator.h"

#include <QFont>
#include <QHash>
#include <QPixmap>
#include <QString>
#include <QStringList>

struct TextEntry {
    QString alignment, typography, text;
};

// texts.xml. Edits patch the raw file text so the Designer's formatting stays as it is.
class TextDb {
public:
    bool load(const QString& path);
    bool save() const;

    TextEntry get(const QString& id) const { return entries.value(id); }
    void set(const QString& id, const TextEntry& e);
    QString add(const TextEntry& e); // new __SingleUse_XXXX id
    void remove(const QString& id);
    bool has(const QString& id) const { return entries.contains(id); }
    QStringList ids() const { return entries.keys(); }
    void setRaw(const QString& r) { raw = r; parse(); }

    QString raw, path, language;
    QStringList typographies;
    QHash<QString, QPair<QString, int>> typographyFont; // id -> (font file, size px)

private:
    void parse();
    QHash<QString, TextEntry> entries;
};

struct Project {
    bool load(const QString& file);
    bool save() const;

    Json& app() { return doc["Application"]; }
    Json& screens() { return doc["Application"]["Screens"]; }
    QString assets(const QString& sub) const { return dir + "/assets/" + sub; }

    QPixmap image(QString rel);   // rel as stored in JSON, may use backslashes
    QFont font(const QString& typography);
    QStringList imageFiles() const; // relative to assets/images, JSON style
    QString makeName(const QString& base) const; // unique widget name like box3
    bool hasName(const QString& name) const;     // any widget or screen already called that

    QString file, dir; // .touchgfx file and its folder (the TouchGFX/ folder)
    Json doc;
    TextDb texts;

private:
    QHash<QString, QPixmap> pixmaps;
    QHash<QString, QString> families; // font file -> family
};

// Sets a widget key the way the Designer stores it: default values are left out,
// new geometry keys go to their usual place so diffs stay small.
void setKey(Json& w, const std::string& key, const Json& value, const Json& def);
