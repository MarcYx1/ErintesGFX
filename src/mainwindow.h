#pragma once
#include "canvas.h"
#include "project.h"

#include <QMainWindow>
#include <functional>
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
    bool save();
    bool generate();              // save + write generated/ code
    void cleanTexts();            // drop __SingleUse texts nobody uses any more
    Json& current();              // selected widget (or the screen when nothing is selected)
    Json& parentList(const QString& ptr, int* index);

    // UI
    void buildActions();
    void showScreen(int i);
    void rebuildTree();
    void selectPtr(const QString& ptr);
    void rebuildProperties();
    void screenProperties(QFormLayout* f);
    void widgetProperties(QFormLayout* f, Json& w);
    void syncGeometry();          // refresh X/Y/W/H fields while dragging
    void autoSize(Json& w);
    void writeUserFiles(const QString& screen); // gui/<screen>_screen View + Presenter, if missing

    // edits
    void addWidget(const QString& type);
    void deleteWidget();
    void duplicateWidget();
    void moveZ(int delta);
    void addScreen();
    void deleteScreen();

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
};
