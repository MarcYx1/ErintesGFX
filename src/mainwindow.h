#pragma once
#include "canvas.h"
#include "project.h"

#include <QMainWindow>
#include <functional>
#include <optional>
#include <QProcess>

class QFormLayout;
class QListWidget;
class QPlainTextEdit;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool open(const QString& file);
    bool busy() const { return proc.state() != QProcess::NotRunning; }

protected:
    void closeEvent(class QCloseEvent*) override;

private:
    // project state
    void changed();               // after every edit: redraw, mark dirty, undo snapshot
    void restore(int index);      // undo/redo
    void autosave();              // <project>.touchgfx.autosave, the project file itself is untouched
    QString autosavePath() const { return p.file + ".autosave"; }
    bool save();
    bool generate();              // save + write generated/ code
    void cleanTexts();            // drop __SingleUse texts nobody uses any more
    Json& current();              // selected widget (or the screen when nothing is selected)
    Json& parentList(const QString& ptr, int* index);

    // UI
    void buildActions();
    void showScreen(int i);
    void fillScreens();           // screen list from the JSON, start screen bold
    void rebuildTree();
    void selectPtr(const QString& ptr);
    void rebuildProperties();
    void screenProperties(QFormLayout* f);
    void widgetProperties(QFormLayout* f, Json& w);
    void syncGeometry();          // refresh X/Y/W/H fields while dragging
    void autoSize(Json& w);
    void writeUserFiles(const QString& screen); // gui/<screen>_screen View + Presenter, if missing

    // edits
    void addWidget(const QString& type, std::optional<QPoint> at = {}); // at: drop point on the canvas
    Json& insertTarget(QString* prefix, int* at); // list a new widget goes into, its pointer prefix and index
    void deleteWidget();
    void duplicateWidget();
    void copyWidget();
    void pasteWidget();
    void moveZ(int delta);
    QString askScreenName(const QString& title, const QString& def); // empty: cancelled or invalid
    void addScreen();
    void duplicateScreen();
    void deleteScreen();
    void setStartScreen();

    // build
    void runStep(const QString& title, const QString& command, std::function<void()> then = {});
    QStringList buildFixes() const; // Linux fixes for projects made on Windows, as make overrides
    void log(const QString& s);

    Project p;
    Canvas* canvas;
    QScrollArea* scroll;
    QListWidget* screenList;
    QTreeWidget* tree;
    QScrollArea* propsArea;
    QPlainTextEdit* logView;
    QHash<QString, class QSpinBox*> geometry;
    QProcess proc;
    std::function<void()> next; // runs when proc ends with exit code 0
    QList<QPair<std::string, QString>> history; // (json, texts.xml)
    int historyIndex = -1;
    bool dirty = false;
    Json clip;                           // Ctrl+C widget
    QHash<QString, TextEntry> clipTexts; // its __SingleUse texts, kept in case the original goes away
};
