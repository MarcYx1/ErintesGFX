#include "mainwindow.h"

#include <QApplication>
#include <cstdio>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }
int num(const Json& j, const char* k, int def = 0) { return j.contains(k) ? (int)j[k].get<double>() : def; }

QColor colorOf(const Json& w)
{
    Json c = w.value("Color", Json::object());
    return QColor(num(c, "Red"), num(c, "Green"), num(c, "Blue"));
}

// Color as the Designer stores it: zero channels left out, black has no Color key at all.
void setColor(Json& w, const QColor& c)
{
    Json o = Json::object();
    if (c.red()) o["Red"] = c.red();
    if (c.green()) o["Green"] = c.green();
    if (c.blue()) o["Blue"] = c.blue();
    setKey(w, "Color", o, Json::object());
}

// Every string in the tree: widget kinds we don't know may keep text ids under other keys.
void collectStrings(const Json& j, QSet<QString>& out)
{
    if (j.is_string()) out << qs(j.get<std::string>());
    else if (j.is_structured())
        for (const Json& v : j) collectStrings(v, out);
}

// j[k] for reading, an empty object when missing (const operator[] on a missing key is undefined)
Json sub(const Json& j, const char* k) { return j.is_object() ? j.value(k, Json::object()) : Json::object(); }

bool isButtonGoto(const Json& i)
{
    return sub(i, "Trigger").value("Type", "") == "TriggerPhysicalButtonClicked" && sub(i, "Action").value("Type", "") == "ActionGotoScreen";
}

// Eye (eye = true) or lock icon. `on` is the non-default state (hidden / locked): closed, in orange.
QIcon stateIcon(bool eye, bool on)
{
    static QIcon cache[4];
    QIcon& icon = cache[eye * 2 + on];
    if (!icon.isNull()) return icon;
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    const QColor c = on ? QColor(255, 165, 50) : QColor(150, 150, 150);
    g.setPen(QPen(c, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    if (eye && !on) { // open eye: two lids and a pupil
        path.moveTo(3, 16);
        path.quadTo(16, 3, 29, 16);
        path.quadTo(16, 29, 3, 16);
        g.drawPath(path);
        g.setBrush(c);
        g.drawEllipse(QPointF(16, 16), 4.5, 4.5);
    } else if (eye) { // closed eye: lower lid with lashes
        path.moveTo(3, 13);
        path.quadTo(16, 25, 29, 13);
        g.drawPath(path);
        g.drawLine(QPointF(8.5, 17.5), QPointF(6, 22));
        g.drawLine(QPointF(16, 19), QPointF(16, 24.5));
        g.drawLine(QPointF(23.5, 17.5), QPointF(26, 22));
    } else { // lock: shackle closed into the body, or lifted open on the right
        path.moveTo(10, 15);
        path.lineTo(10, 10);
        path.arcTo(QRectF(10, 4, 12, 12), 180, -180);
        path.lineTo(22, on ? 15 : 10);
        if (!on) path.translate(0, -3);
        g.drawPath(path);
        g.setBrush(c);
        g.drawRoundedRect(QRectF(6, 15, 20, 14), 2.5, 2.5);
    }
    return icon = QIcon(pm);
}

// Combo that shrinks with the panel instead of growing to its longest item.
QComboBox* newCombo()
{
    auto* cb = new QComboBox;
    cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cb->setMinimumContentsLength(6);
    return cb;
}

// Label that wraps instead of widening the panel.
QLabel* wrapLabel(const QString& text)
{
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

// "None", "Slide East", ... "Wipe South": transition and direction in one combo
const QStringList kTransitions = [] {
    QStringList l{"None"};
    for (const char* kind : {"Slide", "Cover", "Wipe"})
        for (const char* dir : {"East", "West", "North", "South"}) l << QString(kind) + " " + dir;
    return l;
}();

} // namespace

MainWindow::MainWindow()
{
    canvas = new Canvas(p);
    scroll = new QScrollArea;
    scroll->setWidget(canvas);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setBackgroundRole(QPalette::Dark);
    setCentralWidget(scroll);

    auto dock = [this](const QString& title, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title);
        d->setObjectName(title);
        d->setWidget(w);
        addDockWidget(area, d);
        return d;
    };

    // Screens: list with + / - under it
    auto* screensBox = new QWidget;
    auto* sl = new QVBoxLayout(screensBox);
    sl->setContentsMargins(0, 0, 0, 0);
    screenList = new QListWidget;
    sl->addWidget(screenList);
    auto* sb = new QHBoxLayout;
    auto* addS = new QPushButton("+ Screen");
    auto* delS = new QPushButton("- Screen");
    sb->addWidget(addS);
    sb->addWidget(delS);
    sl->addLayout(sb);
    dock("Screens", screensBox, Qt::LeftDockWidgetArea);
    connect(addS, &QPushButton::clicked, this, &MainWindow::addScreen);
    connect(delS, &QPushButton::clicked, this, &MainWindow::deleteScreen);
    connect(screenList, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r >= 0) showScreen(r);
    });

    tree = new QTreeWidget;
    tree->setColumnCount(4);
    tree->setHeaderLabels({"Name", "Type", "", ""});
    tree->headerItem()->setToolTip(2, "Visible");
    tree->headerItem()->setToolTip(3, "Locked");
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (int c : {2, 3}) {
        tree->header()->setSectionResizeMode(c, QHeaderView::Fixed);
        tree->header()->resizeSection(c, 26);
    }
    tree->setMinimumWidth(320); // room for name, type, eye and lock
    dock("Widgets", tree, Qt::LeftDockWidgetArea);
    // Eye / lock columns toggle Visible / LockPosition.
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it, int col) {
        if (col < 2) return;
        Json& w = p.doc[Json::json_pointer(ss(it->data(0, Qt::UserRole).toString()))];
        if (col == 2)
            setKey(w, "Visible", !w.value("Visible", true), true);
        else
            setKey(w, "LockPosition", !w.value("LockPosition", false), false);
        rebuildTree();
        rebuildProperties();
        changed();
    });
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        auto* it = tree->currentItem();
        canvas->select(it ? it->data(0, Qt::UserRole).toString() : QString());
        rebuildProperties();
    });

    propsArea = new QScrollArea;
    propsArea->setWidgetResizable(true);
    propsArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // fields shrink to fit instead
    propsArea->setMinimumWidth(360);
    dock("Properties", propsArea, Qt::RightDockWidgetArea);

    logView = new QPlainTextEdit;
    logView->setReadOnly(true);
    logView->setMaximumBlockCount(5000);
    logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    dock("Output", logView, Qt::BottomDockWidgetArea);

    connect(canvas, &Canvas::selected, this, &MainWindow::selectPtr);
    connect(canvas, &Canvas::moved, this, [this] {
        syncGeometry();
        dirty = true;
    });
    connect(canvas, &Canvas::editFinished, this, &MainWindow::changed);

    proc.setProcessChannelMode(QProcess::MergedChannels);
    connect(&proc, &QProcess::readyRead, this, [this] { log(QString::fromLocal8Bit(proc.readAll()).trimmed()); });
    connect(&proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        const bool ok = st == QProcess::NormalExit && code == 0;
        log(ok ? "Done." : QString("Failed (exit code %1).").arg(code));
        auto n = std::move(next);
        next = {};
        if (ok && n) n();
    });

    buildActions();
    resize(1500, 900);
    restoreState(QSettings().value("windowState").toByteArray());
}

void MainWindow::buildActions()
{
    auto* tb = addToolBar("Main");
    tb->setObjectName("Main");
    auto act = [this, tb](const QString& text, const QKeySequence& key, auto fn, bool onBar = true) {
        auto* a = new QAction(text, this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, fn);
        if (onBar) tb->addAction(a);
        addAction(a);
        return a;
    };
    act("Open", QKeySequence::Open, [this] {
        QString f = QFileDialog::getOpenFileName(this, "Open TouchGFX project", QString(), "TouchGFX (*.touchgfx)");
        if (!f.isEmpty()) open(f);
    });
    act("Save", QKeySequence::Save, [this] { save(); });
    tb->addSeparator();
    act("Undo", QKeySequence::Undo, [this] {
        if (historyIndex > 0) restore(historyIndex - 1);
    });
    act("Redo", QKeySequence("Ctrl+Shift+Z"), [this] {
        if (historyIndex + 1 < history.size()) restore(historyIndex + 1);
    });
    tb->addSeparator();

    auto* addBtn = new QToolButton;
    addBtn->setText("Add widget");
    addBtn->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(addBtn);
    for (const char* t : {"Box", "Image", "TextArea", "Line", "Shape", "BoxProgress", "LineProgress", "Container"})
        menu->addAction(t, this, [this, t] { addWidget(t); });
    addBtn->setMenu(menu);
    tb->addWidget(addBtn);
    act("Duplicate", QKeySequence("Ctrl+D"), [this] { duplicateWidget(); });
    auto* del = act("Delete", QKeySequence::Delete, [this] { deleteWidget(); });
    // Delete only from the canvas and the tree, so it still works as a key in text fields.
    del->setShortcutContext(Qt::WidgetShortcut);
    canvas->addAction(del);
    tree->addAction(del);
    act("Forward", QKeySequence("Ctrl+PgUp"), [this] { moveZ(1); });
    act("Backward", QKeySequence("Ctrl+PgDown"), [this] { moveZ(-1); });
    tb->addSeparator();
    act("Zoom in", QKeySequence::ZoomIn, [this] { canvas->setZoom(canvas->zoom() * 1.25); }, false);
    act("Zoom out", QKeySequence::ZoomOut, [this] { canvas->setZoom(canvas->zoom() * 0.8); }, false);
    act("Zoom 100%", QKeySequence("Ctrl+0"), [this] { canvas->setZoom(1.0); }, false);
    tb->addSeparator();

    const QString jobs = "-j" + QString::number(QThread::idealThreadCount());
    auto command = [this, jobs](const char* key, const char* def) {
        QString c = qs(p.app().value(key, std::string(def)));
        return c.replace(QRegularExpression("-j\\d+"), jobs);
    };
    act("Generate code (F4)", QKeySequence("F4"), [this] { generate(); });
    act("Run simulator (F5)", QKeySequence("F5"), [this, command] {
        if (!generate()) return;
        // Modern GCC on Linux: PIE by default (ST's libtouchgfx.a is not PIC) and new warnings hit -Werror.
        // ponytail: copies the Linux linker_options of TouchGFX 4.26's simulator Makefile; recheck on a new version
        const QString fixes = " 'WARN=all no-unused-parameter' 'linker_options=-static-libgcc -Xlinker --no-as-needed -no-pie'";
        runStep("Simulator", command("CompileSimulatorCommand", "make -f simulator/gcc/Makefile -j8") + fixes, [this] {
            QProcess::startDetached(p.dir + "/build/bin/simulator.out", {}, p.dir);
        });
    });
    act("Build target (F6)", QKeySequence("F6"), [this, command] {
        if (!generate()) return;
        runStep("Target", command("CompileTargetCommandOverride", "make -f ../gcc/Makefile -j8") + " " + buildFixes().join(' '),
                [this] { log("target.hex: " + p.dir + "/build/bin/target.hex"); });
    });
}

bool MainWindow::open(const QString& file)
{
    if (!p.load(file)) {
        QMessageBox::warning(this, "Open", "Cannot read " + file + " or its assets/texts/texts.xml.");
        return false;
    }
    QSettings().setValue("lastProject", file);
    history = {{p.doc.dump(), p.texts.raw}};
    historyIndex = 0;
    dirty = false;
    screenList->blockSignals(true);
    screenList->clear();
    int start = 0;
    for (const Json& s : p.screens()) {
        if (s.value("Name", "") == p.app().value("StartupScreenName", "")) start = screenList->count();
        screenList->addItem(qs(s.value("Name", "")));
    }
    screenList->blockSignals(false);
    canvas->setZoom(1.0);
    showScreen(start);
    setWindowTitle("ErintesGFX - " + qs(p.app().value("Name", "")));
    log("Opened " + file);
    return true;
}

void MainWindow::changed()
{
    canvas->update();
    std::string now = p.doc.dump();
    if (historyIndex >= 0 && history[historyIndex].first == now && history[historyIndex].second == p.texts.raw) return;
    dirty = true;
    setWindowTitle("ErintesGFX - " + qs(p.app().value("Name", "")) + " *");
    while (history.size() > historyIndex + 1) history.removeLast();
    history.append({std::move(now), p.texts.raw});
    if (history.size() > 300) history.removeFirst();
    historyIndex = history.size() - 1;
}

void MainWindow::restore(int index)
{
    historyIndex = index;
    p.doc = Json::parse(history[index].first);
    p.texts.setRaw(history[index].second);
    dirty = true;
    screenList->blockSignals(true);
    screenList->clear();
    for (const Json& s : p.screens()) screenList->addItem(qs(s.value("Name", "")));
    screenList->blockSignals(false);
    showScreen(qMin(canvas->screen(), int(p.screens().size()) - 1));
}

bool MainWindow::save()
{
    cleanTexts();
    if (!p.save()) {
        QMessageBox::warning(this, "Save", "Cannot write " + p.file);
        return false;
    }
    dirty = false;
    setWindowTitle("ErintesGFX - " + qs(p.app().value("Name", "")));
    log("Saved " + p.file);
    return true;
}

bool MainWindow::generate()
{
    if (proc.state() != QProcess::NotRunning) {
        log("A build is still running.");
        return false;
    }
    if (!save()) return false;
    try {
        for (const Json& s : p.screens()) writeUserFiles(qs(s.value("Name", "")));
        GenResult r = ::generate(p.doc);
        for (const auto& w : r.warnings) log("warning: " + qs(w));
        int n = writeFiles(ss(p.dir), r);
        log(QString("Generated code: %1 of %2 files changed.").arg(n).arg(r.files.size()));
        return true;
    } catch (const std::exception& e) {
        log(QString("Code generation failed: ") + e.what());
        return false;
    }
}

void MainWindow::cleanTexts()
{
    QSet<QString> used;
    collectStrings(p.doc, used);
    for (const QString& id : p.texts.ids())
        if (id.startsWith("__SingleUse_") && !used.contains(id)) p.texts.remove(id);
}

Json& MainWindow::current()
{
    const QString sel = canvas->selection();
    return sel.isEmpty() ? p.screens()[canvas->screen()] : p.doc[Json::json_pointer(ss(sel))];
}

Json& MainWindow::parentList(const QString& ptr, int* index)
{
    const int slash = ptr.lastIndexOf('/');
    *index = ptr.mid(slash + 1).toInt();
    return p.doc[Json::json_pointer(ss(ptr.left(slash)))];
}

void MainWindow::showScreen(int i)
{
    if (i < 0) return;
    screenList->blockSignals(true);
    screenList->setCurrentRow(i);
    screenList->blockSignals(false);
    canvas->setScreen(i);
    rebuildTree();
    rebuildProperties();
}

void MainWindow::rebuildTree()
{
    tree->blockSignals(true);
    const int scrollPos = tree->verticalScrollBar()->value(); // icon clicks rebuild the tree, keep the view
    tree->clear();
    std::function<void(const Json&, const QString&, QTreeWidgetItem*)> add = [&](const Json& comps, const QString& ptr,
                                                                                QTreeWidgetItem* parent) {
        for (size_t i = 0; i < comps.size(); ++i) {
            const Json& w = comps[i];
            auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
            const bool visible = w.value("Visible", true), locked = w.value("LockPosition", false);
            it->setText(0, qs(w.value("Name", "")));
            it->setText(1, qs(w.value("Type", "")));
            it->setForeground(1, QColor(140, 140, 140));
            it->setIcon(2, stateIcon(true, !visible));
            it->setToolTip(2, visible ? "Visible, click to hide" : "Hidden, click to show");
            it->setIcon(3, stateIcon(false, locked));
            it->setToolTip(3, locked ? "Locked, click to unlock" : "Unlocked, click to lock");
            const QString me = ptr + "/" + QString::number(i);
            it->setData(0, Qt::UserRole, me);
            if (!visible) it->setForeground(0, QColor(130, 130, 130));
            if (w.contains("Components")) add(w["Components"], me + "/Components", it);
        }
    };
    add(p.screens()[canvas->screen()].value("Components", Json::array()), canvas->base(), nullptr);
    tree->expandAll();
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == canvas->selection()) tree->setCurrentItem(*it);
    tree->verticalScrollBar()->setValue(scrollPos);
    tree->blockSignals(false);
}

void MainWindow::selectPtr(const QString& ptr)
{
    canvas->select(ptr);
    tree->blockSignals(true);
    tree->clearSelection();
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == ptr) {
            tree->setCurrentItem(*it);
            tree->scrollToItem(*it);
        }
    tree->blockSignals(false);
    rebuildProperties();
}

void MainWindow::rebuildProperties()
{
    geometry.clear();
    auto* w = new QWidget;
    auto* f = new QFormLayout(w);
    if (canvas->selection().isEmpty())
        screenProperties(f);
    else
        widgetProperties(f, current());
    propsArea->setWidget(w); // deletes the old panel
}

void MainWindow::screenProperties(QFormLayout* f)
{
    Json& s = current();
    f->addRow("Screen", new QLabel("<b>" + qs(s.value("Name", "")) + "</b>"));
    auto* cbs = new QSpinBox;
    cbs->setRange(0, 1000000);
    cbs->setValue(num(s, "CanvasBufferSize"));
    f->addRow("Canvas buffer", cbs);
    connect(cbs, &QSpinBox::valueChanged, this, [this](int v) {
        setKey(current(), "CanvasBufferSize", v, 0);
        changed();
    });

    // Interactions: hardware button -> go to screen. Other kinds stay in the JSON untouched.
    auto* table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({"Name", "Button", "Go to", "Transition"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); // fit the panel width
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->verticalHeader()->hide();
    table->setMinimumHeight(220);
    QStringList screens;
    for (const Json& sc : p.screens()) screens << qs(sc.value("Name", ""));

    auto writeBack = [this, table] {
        auto row = [table](int r) {
            Json trig = {{"Type", "TriggerPhysicalButtonClicked"}};
            int key = static_cast<QSpinBox*>(table->cellWidget(r, 1))->value();
            if (key) trig["ButtonKey"] = key;
            const QStringList tr = static_cast<QComboBox*>(table->cellWidget(r, 3))->currentText().split(' '); // kind [dir]
            Json act = {{"Type", "ActionGotoScreen"}, {"ScreenTransitionType", ss("ScreenTransition" + tr[0])}};
            if (tr.size() > 1) act["ScreenTransitionDirection"] = ss(tr[1]);
            act["ActionComponent"] = ss(static_cast<QComboBox*>(table->cellWidget(r, 2))->currentText());
            return Json{{"InteractionName", ss(table->item(r, 0)->text())}, {"Trigger", trig}, {"Action", act}};
        };
        for (int r = 0; r < table->rowCount(); ++r)
            if (!table->cellWidget(r, 3)) return; // row still being filled
        // Table rows take the places of the button interactions, other kinds stay where they are.
        Json out = Json::array();
        int r = 0;
        for (const Json& i : current().value("Interactions", Json::array())) {
            if (!isButtonGoto(i)) out.push_back(i);
            else if (r < table->rowCount()) out.push_back(row(r++));
        }
        while (r < table->rowCount()) out.push_back(row(r++));
        current()["Interactions"] = out;
        changed();
    };
    auto addRow = [table, screens, writeBack](const Json& i) {
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(qs(i.value("InteractionName", ""))));
        auto* key = new QSpinBox;
        key->setRange(0, 255);
        const Json act = sub(i, "Action");
        key->setValue(num(sub(i, "Trigger"), "ButtonKey"));
        auto* target = newCombo();
        target->addItems(screens);
        const QString to = qs(act.value("ActionComponent", ""));
        if (!screens.contains(to)) target->addItem(to); // keep a target we can't find instead of retargeting
        target->setCurrentText(to);
        auto* tr = newCombo();
        tr->addItems(kTransitions);
        QString kind = qs(act.value("ScreenTransitionType", "ScreenTransitionNone")).mid(16);
        if (kind != "None") kind += " " + qs(act.value("ScreenTransitionDirection", "East"));
        if (!kTransitions.contains(kind)) tr->addItem(kind); // keep kinds we don't list
        tr->setCurrentText(kind);
        table->setCellWidget(r, 1, key);
        table->setCellWidget(r, 2, target);
        table->setCellWidget(r, 3, tr);
        QObject::connect(key, &QSpinBox::valueChanged, table, writeBack);
        QObject::connect(target, &QComboBox::currentIndexChanged, table, writeBack);
        QObject::connect(tr, &QComboBox::currentIndexChanged, table, writeBack);
    };
    for (const Json& i : current().value("Interactions", Json::array()))
        if (isButtonGoto(i)) addRow(i);
    connect(table, &QTableWidget::itemChanged, this, writeBack);
    f->addRow(wrapLabel("Interactions (hardware button -> screen)"));
    f->addRow(table);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton("+ Interaction");
    auto* del = new QPushButton("- Interaction");
    buttons->addWidget(add);
    buttons->addWidget(del);
    f->addRow(buttons);
    connect(add, &QPushButton::clicked, this, [table, addRow, writeBack, screens] {
        addRow({{"InteractionName", ss("Interaction" + QString::number(table->rowCount() + 1))},
                {"Trigger", {{"Type", "TriggerPhysicalButtonClicked"}}},
                {"Action", {{"Type", "ActionGotoScreen"}, {"ActionComponent", ss(screens.value(0))}}}});
        writeBack();
    });
    connect(del, &QPushButton::clicked, this, [table, writeBack] {
        if (table->currentRow() < 0) return;
        table->removeRow(table->currentRow());
        writeBack();
    });
}

void MainWindow::widgetProperties(QFormLayout* f, Json& widget)
{
    const std::string ptr = ss(canvas->selection());
    const QString type = qs(widget.value("Type", ""));
    auto W = [this, ptr]() -> Json& { return p.doc[Json::json_pointer(ptr)]; };
    auto touched = [this] {
        canvas->update();
        changed();
    };

    auto intField = [&](const char* key, const Json& def, int lo = -10000, int hi = 10000) {
        auto* sp = new QSpinBox;
        sp->setRange(lo, hi);
        sp->setValue(num(W(), key, def.is_null() ? 0 : def.get<int>()));
        f->addRow(key, sp);
        connect(sp, &QSpinBox::valueChanged, this, [=](int v) {
            setKey(W(), key, v, def);
            touched();
        });
        return sp;
    };
    auto dblField = [&](const char* key, const Json& def) {
        auto* sp = new QDoubleSpinBox;
        sp->setRange(-10000, 10000);
        sp->setDecimals(2);
        sp->setValue(W().value(key, def.is_null() ? 0.0 : def.get<double>()));
        f->addRow(key, sp);
        connect(sp, &QDoubleSpinBox::valueChanged, this, [=](double v) {
            setKey(W(), key, v, def);
            touched();
        });
    };
    auto boolField = [&](const char* key, bool def) {
        auto* cb = new QCheckBox;
        cb->setChecked(W().value(key, def));
        f->addRow(key, cb);
        connect(cb, &QCheckBox::toggled, this, [=](bool v) {
            setKey(W(), key, v, def);
            if (std::string(key) == "AutoSize") {
                if (v) autoSize(W());
                syncGeometry();
                geometry["Width"]->setEnabled(!v);
                geometry["Height"]->setEnabled(!v);
            }
            if (std::string(key) == "Visible") rebuildTree();
            touched();
        });
    };
    auto enumField = [&](const char* key, const QStringList& options) {
        auto* cb = newCombo();
        cb->addItems(options);
        cb->setCurrentText(qs(W().value(key, ss(options[0]))));
        f->addRow(key, cb);
        connect(cb, &QComboBox::currentTextChanged, this, [=](const QString& v) {
            setKey(W(), key, ss(v), Json());
            touched();
        });
    };
    auto imageField = [&](const char* key, bool optional) {
        auto* cb = newCombo();
        if (optional) cb->addItem("(none)");
        cb->addItems(p.imageFiles());
        cb->view()->setMinimumWidth(cb->view()->sizeHintForColumn(0) + 24); // popup shows full file names
        cb->setCurrentText(qs(W().value(key, std::string("(none)"))));
        cb->setToolTip(cb->currentText());
        f->addRow(key, cb);
        connect(cb, &QComboBox::currentTextChanged, this, [=](const QString& v) {
            if (v == "(none)") {
                W().erase(key);
            } else {
                setKey(W(), key, ss(v), Json());
                if (std::string(key) == "RelativeFilename") { // Image size follows the bitmap
                    QPixmap pm = p.image(v);
                    setKey(W(), "Width", pm.width(), 0);
                    setKey(W(), "Height", pm.height(), 0);
                    syncGeometry();
                }
            }
            touched();
        });
    };
    auto colorField = [&] {
        auto* b = new QPushButton;
        auto paint = [b](const QColor& c) { b->setStyleSheet("background:" + c.name() + "; min-height: 20px;"); };
        paint(colorOf(W()));
        f->addRow("Color", b);
        connect(b, &QPushButton::clicked, this, [=] {
            QColor c = QColorDialog::getColor(colorOf(W()), this);
            if (!c.isValid()) return;
            setColor(W(), c);
            paint(c);
            touched();
        });
    };

    auto* name = new QLineEdit(qs(widget.value("Name", "")));
    f->addRow("<b>" + type + "</b>", new QLabel);
    f->addRow("Name", name);
    connect(name, &QLineEdit::editingFinished, this, [=] {
        static const QRegularExpression ident("^[A-Za-z_][A-Za-z0-9_]*$");
        if (name->text() == qs(W().value("Name", ""))) return;
        if (!ident.match(name->text()).hasMatch() || p.hasName(name->text())) {
            log("Name " + name->text() + " is not a C++ identifier or is already used.");
            name->setText(qs(W().value("Name", "")));
            return;
        }
        W()["Name"] = ss(name->text());
        rebuildTree(); // not the panel: this field is the sender
        changed();
    });
    for (const char* k : {"X", "Y", "Width", "Height"}) geometry[k] = intField(k, 0);
    if (type == "Image" || (type == "TextArea" && widget.value("AutoSize", false))) {
        geometry["Width"]->setEnabled(false);
        geometry["Height"]->setEnabled(false);
    }
    boolField("Visible", true);
    boolField("LockPosition", false);

    if (type == "TextArea") {
        const QString id = qs(widget.value("TextId", ""));
        TextEntry e = p.texts.get(id);
        auto* text = new QPlainTextEdit(e.text);
        text->setMaximumHeight(70);
        f->addRow("Text", text);
        f->addRow("Text id", wrapLabel(id));
        auto* typo = newCombo();
        typo->addItems(p.texts.typographies);
        typo->setCurrentText(e.typography);
        f->addRow("Typography", typo);
        auto* align = newCombo();
        align->addItems({"Left", "Center", "Right"});
        align->setCurrentText(e.alignment);
        f->addRow("Alignment", align);
        auto apply = [=] {
            p.texts.set(id, {align->currentText(), typo->currentText(), text->toPlainText()});
            if (W().value("AutoSize", false)) {
                autoSize(W());
                syncGeometry();
            }
            touched();
        };
        connect(text, &QPlainTextEdit::textChanged, this, apply);
        connect(typo, &QComboBox::currentTextChanged, this, apply);
        connect(align, &QComboBox::currentTextChanged, this, apply);
        if (widget.contains("Wildcard1") && widget["Wildcard1"].contains("TextId")) {
            const QString wid = qs(widget["Wildcard1"]["TextId"]);
            auto* wc = new QLineEdit(p.texts.get(wid).text);
            f->addRow("Wildcard text", wc);
            connect(wc, &QLineEdit::textChanged, this, [=](const QString& v) {
                TextEntry we = p.texts.get(wid);
                we.text = v;
                p.texts.set(wid, we);
                touched();
            });
        }
        colorField();
        boolField("AutoSize", false);
        intField("LineSpacing", 0);
    } else if (type == "Box") {
        colorField();
    } else if (type == "Image") {
        imageField("RelativeFilename", false);
    } else if (type == "Line" || type == "LineProgress") {
        colorField();
        for (const char* k : {"StartX", "StartY", "EndX", "EndY"}) dblField(k, 0.0);
        dblField("LineWidth", Json());
        enumField("LineEndingStyle", {"Butt", "Round", "Square"});
    } else if (type == "Shape") {
        colorField();
        dblField("XOrigin", 0.0);
        dblField("YOrigin", 0.0);
        dblField("Angle", 0.0);
        dblField("XScale", Json());
        dblField("YScale", Json());
        f->addRow("Points", wrapLabel(QString::number(widget["Points"].size()) + " (edit in the .touchgfx file)"));
    } else if (type == "BoxProgress") {
        colorField();
        enumField("Direction", {"Right", "Left", "Up", "Down"});
        for (const char* k : {"IndicatorPositionX", "IndicatorPositionY"}) intField(k, 0);
        for (const char* k : {"IndicatorPositionWidth", "IndicatorPositionHeight"}) intField(k, Json());
    }
    if (type == "BoxProgress" || type == "LineProgress") {
        imageField("FileNameBackground", true);
        intField("ProgressRangeMin", 0);
        intField("ProgressRangeMax", Json());
        intField("ProgressInitialValue", 0);
    }
    if (type != "Container") intField("Alpha", 255, 0, 255);
}

void MainWindow::syncGeometry()
{
    if (canvas->selection().isEmpty()) return;
    const Json& w = current();
    for (auto it = geometry.begin(); it != geometry.end(); ++it) {
        it.value()->blockSignals(true);
        it.value()->setValue(num(w, ss(it.key()).c_str()));
        it.value()->blockSignals(false);
    }
}

void MainWindow::autoSize(Json& w)
{
    TextEntry e = p.texts.get(qs(w.value("TextId", "")));
    QString text = e.text;
    if (w.contains("Wildcard1") && w["Wildcard1"].contains("TextId"))
        text.replace(QRegularExpression("<[^>]*>"), p.texts.get(qs(w["Wildcard1"]["TextId"])).text);
    // ponytail: Qt font metrics, TouchGFX's own glyph metrics can differ by a pixel or two
    QRect r = QFontMetrics(p.font(e.typography)).boundingRect(QRect(0, 0, 100000, 100000), Qt::AlignLeft | Qt::AlignTop, text);
    setKey(w, "Width", qMax(1, r.width()), 0);
    setKey(w, "Height", qMax(1, r.height()), 0);
}

void MainWindow::writeUserFiles(const QString& n)
{
    const QString l = n.toLower() + "_screen", g = p.dir + "/gui/";
    if (QFile::exists(g + "include/gui/" + l + "/" + n + "View.hpp")) return;
    QDir().mkpath(g + "include/gui/" + l);
    QDir().mkpath(g + "src/" + l);
    const QString up = n.toUpper();
    auto write = [](const QString& path, const QString& text) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
    };
    write(g + "include/gui/" + l + "/" + n + "View.hpp",
          "#ifndef " + up + "VIEW_HPP\n#define " + up + "VIEW_HPP\n\n#include <gui_generated/" + l + "/" + n +
              "ViewBase.hpp>\n#include <gui/" + l + "/" + n + "Presenter.hpp>\n\nclass " + n + "View : public " + n +
              "ViewBase\n{\npublic:\n    " + n + "View();\n    virtual ~" + n +
              "View() {}\n    virtual void setupScreen();\n    virtual void tearDownScreen();\nprotected:\n};\n\n#endif // " + up +
              "VIEW_HPP\n");
    write(g + "src/" + l + "/" + n + "View.cpp",
          "#include <gui/" + l + "/" + n + "View.hpp>\n\n" + n + "View::" + n + "View()\n{\n\n}\n\nvoid " + n +
              "View::setupScreen()\n{\n    " + n + "ViewBase::setupScreen();\n}\n\nvoid " + n + "View::tearDownScreen()\n{\n    " +
              n + "ViewBase::tearDownScreen();\n}\n");
    write(g + "include/gui/" + l + "/" + n + "Presenter.hpp",
          "#ifndef " + up + "PRESENTER_HPP\n#define " + up +
              "PRESENTER_HPP\n\n#include <gui/model/ModelListener.hpp>\n#include <mvp/Presenter.hpp>\n\nusing namespace "
              "touchgfx;\n\nclass " + n + "View;\n\nclass " + n + "Presenter : public touchgfx::Presenter, public ModelListener\n{\npublic:\n    " +
              n + "Presenter(" + n + "View& v);\n\n    virtual void activate();\n\n    virtual void deactivate();\n\n    virtual ~" + n +
              "Presenter() {}\n\nprivate:\n    " + n + "Presenter();\n\n    " + n + "View& view;\n};\n\n#endif // " + up +
              "PRESENTER_HPP\n");
    write(g + "src/" + l + "/" + n + "Presenter.cpp",
          "#include <gui/" + l + "/" + n + "View.hpp>\n#include <gui/" + l + "/" + n + "Presenter.hpp>\n\n" + n +
              "Presenter::" + n + "Presenter(" + n + "View& v)\n    : view(v)\n{\n\n}\n\nvoid " + n +
              "Presenter::activate()\n{\n\n}\n\nvoid " + n + "Presenter::deactivate()\n{\n\n}\n");
    log("Created gui/" + l + " (View + Presenter).");
}

void MainWindow::addWidget(const QString& type)
{
    const Json white = {{"Red", 255}, {"Green", 255}, {"Blue", 255}};
    QString base = type;
    base[0] = base[0].toLower();
    Json w = {{"Type", ss(type)}, {"Name", ss(p.makeName(base))}, {"X", 20}, {"Y", 20}};
    if (type == "Box") {
        w.update({{"Width", 100}, {"Height", 50}, {"Color", white}});
    } else if (type == "Image") {
        bool ok = false;
        QString img = QInputDialog::getItem(this, "Image", "Bitmap from assets/images:", p.imageFiles(), 0, false, &ok);
        if (!ok) return;
        QPixmap pm = p.image(img);
        w.update({{"Width", pm.width()}, {"Height", pm.height()}, {"RelativeFilename", ss(img)}});
    } else if (type == "TextArea") {
        const QString id = p.texts.add({"Left", p.texts.typographies.value(0, "Default"), "New Text"});
        w.update({{"Width", 1}, {"Height", 1}, {"TextId", ss(id)}, {"TextRotation", "0"}, {"Color", white}, {"AutoSize", true}});
        autoSize(w);
    } else if (type == "Line") {
        w.update({{"Width", 100}, {"Height", 10}, {"Color", white}, {"StartY", 5.0}, {"EndX", 100.0}, {"EndY", 5.0},
                  {"LineWidth", 10.0}, {"LineEndingStyle", "Round"}});
    } else if (type == "Shape") {
        w.update({{"Width", 80}, {"Height", 80}, {"Color", white},
                  {"Points", Json::array({Json::object(), {{"X", 80.0}}, {{"X", 80.0}, {"Y", 80.0}}, {{"Y", 80.0}}})},
                  {"XScale", 1.0}, {"YScale", 1.0}});
    } else if (type == "BoxProgress") {
        w.update({{"Width", 200}, {"Height", 20}, {"Color", {{"Green", 255}}}, {"Direction", "Right"},
                  {"IndicatorPositionWidth", 200}, {"IndicatorPositionHeight", 20}, {"ProgressRangeMax", 100},
                  {"ProgressInitialValue", 60}});
    } else if (type == "LineProgress") {
        w.update({{"Width", 200}, {"Height", 20}, {"Color", {{"Green", 255}}}, {"StartY", 10.0}, {"EndX", 200.0},
                  {"EndY", 10.0}, {"LineWidth", 20.0}, {"LineEndingStyle", "Butt"}, {"ProgressRangeMax", 100},
                  {"ProgressInitialValue", 60}});
    } else if (type == "Container") {
        w.update({{"Width", 200}, {"Height", 100}, {"Components", Json::array()}});
    }

    // Into the selected container, after the selected widget, or at the end of the screen.
    const QString sel = canvas->selection();
    QString ptr;
    if (!sel.isEmpty() && current().value("Type", "") == "Container") {
        Json& list = current()["Components"];
        list.push_back(w);
        ptr = sel + "/Components/" + QString::number(list.size() - 1);
    } else if (!sel.isEmpty()) {
        int i;
        Json& list = parentList(sel, &i);
        list.insert(list.begin() + i + 1, w);
        ptr = sel.left(sel.lastIndexOf('/') + 1) + QString::number(i + 1);
    } else {
        Json& s = p.screens()[canvas->screen()];
        if (!s.contains("Components")) s["Components"] = Json::array();
        s["Components"].push_back(w);
        ptr = canvas->base() + "/" + QString::number(s["Components"].size() - 1);
    }
    rebuildTree();
    selectPtr(ptr);
    changed();
}

void MainWindow::deleteWidget()
{
    const QString sel = canvas->selection();
    if (sel.isEmpty()) return;
    int i;
    Json& list = parentList(sel, &i);
    list.erase(list.begin() + i);
    canvas->select({});
    rebuildTree();
    rebuildProperties();
    changed();
}

void MainWindow::duplicateWidget()
{
    const QString sel = canvas->selection();
    if (sel.isEmpty()) return;
    Json copy = current();
    std::function<void(Json&)> fresh = [&](Json& w) {
        QString base = qs(w.value("Type", "widget"));
        base[0] = base[0].toLower();
        w["Name"] = ss(p.makeName(base));
        const QString id = qs(w.value("TextId", ""));
        if (id.startsWith("__SingleUse_")) w["TextId"] = ss(p.texts.add(p.texts.get(id)));
        if (w.contains("Components"))
            for (Json& c : w["Components"]) fresh(c);
    };
    // Names must be unique before the copy is in the tree, so pick them one by one.
    fresh(copy);
    setKey(copy, "X", num(copy, "X") + 10, 0);
    setKey(copy, "Y", num(copy, "Y") + 10, 0);
    int i;
    Json& list = parentList(sel, &i);
    list.insert(list.begin() + i + 1, copy);
    const QString ptr = sel.left(sel.lastIndexOf('/') + 1) + QString::number(i + 1);
    rebuildTree();
    selectPtr(ptr);
    changed();
}

void MainWindow::moveZ(int delta)
{
    const QString sel = canvas->selection();
    if (sel.isEmpty()) return;
    int i;
    Json& list = parentList(sel, &i);
    const int j = i + delta;
    if (j < 0 || j >= int(list.size())) return;
    std::swap(list[i], list[j]);
    const QString ptr = sel.left(sel.lastIndexOf('/') + 1) + QString::number(j);
    rebuildTree();
    selectPtr(ptr);
    changed();
}

void MainWindow::addScreen()
{
    QString n = QInputDialog::getText(this, "New screen", "Name (letters, digits, _):");
    static const QRegularExpression ident("^[A-Za-z_][A-Za-z0-9_]*$");
    if (n.isEmpty()) return;
    for (const Json& s : p.screens())
        if (qs(s.value("Name", "")) == n) n.clear();
    if (!ident.match(n).hasMatch()) {
        QMessageBox::warning(this, "New screen", "Name must be a unique C++ identifier.");
        return;
    }
    p.screens().push_back({{"Name", ss(n)}, {"Components", Json::array()}, {"Interactions", Json::array()}});
    screenList->addItem(n);
    showScreen(screenList->count() - 1);
    changed();
}

void MainWindow::deleteScreen()
{
    const int i = canvas->screen();
    const QString n = qs(p.screens()[i].value("Name", ""));
    if (n == qs(p.app().value("StartupScreenName", ""))) {
        QMessageBox::warning(this, "Delete screen", "The start screen cannot be deleted.");
        return;
    }
    const QString l = n.toLower() + "_screen";
    if (QMessageBox::question(this, "Delete screen",
                              "Delete screen " + n + "?\n\nYour code in gui/include/gui/" + l + " and gui/src/" + l +
                                  " goes to the trash. Button interactions that go to " + n + " are removed.") != QMessageBox::Yes)
        return;
    p.screens().erase(p.screens().begin() + i);
    for (Json& s : p.screens()) {
        if (!s.contains("Interactions")) continue;
        Json& in = s["Interactions"];
        for (int k = int(in.size()) - 1; k >= 0; --k)
            if (sub(in[k], "Action").value("ActionComponent", "") == ss(n)) in.erase(in.begin() + k);
    }
    for (const QString& d : {p.dir + "/gui/include/gui/" + l, p.dir + "/gui/src/" + l})
        if (QFile::exists(d) && !QFile::moveToTrash(d)) log("Could not move " + d + " to the trash.");
    delete screenList->takeItem(i);
    showScreen(qMax(0, i - 1));
    changed();
}

void MainWindow::runStep(const QString& title, const QString& command, std::function<void()> then)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList tc = QDir(QDir::homePath() + "/opt").entryList({"arm-gnu-toolchain-*"}, QDir::Dirs, QDir::Name);
    QString path = env.value("PATH");
    if (!tc.isEmpty()) path = QDir::homePath() + "/opt/" + tc.last() + "/bin:" + path;
    env.insert("PATH", path);
    // TouchGFX textconvert still calls File.exists?, gone since Ruby 3.2
    env.insert("RUBYOPT", "-r" + QCoreApplication::applicationDirPath() + "/ruby_compat.rb");
    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(p.dir);
    next = std::move(then);
    log("== " + title + ": " + command);
    proc.start("sh", {"-c", command});
}

// Projects saved on Windows: tool binaries lose their exec bit in git, and Makefile paths
// may differ in case from the folders on disk (Windows does not care, Linux does).
QStringList MainWindow::buildFixes() const
{
    const QString fw = QDir(p.dir).filePath(qs(p.doc["Application"].value("TouchGfxPath", "../Middlewares/ST/touchgfx")));
    for (const char* t : {"imageconvert", "fontconvert"}) {
        const QString f = fw + "/framework/tools/" + t + "/build/linux/" + t + ".out";
        if (QFile::exists(f))
            QFile::setPermissions(f, QFile::permissions(f) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    }

    QStringList out;
    QRegularExpression mf("-f\\s+(\\S+)");
    QString mk = mf.match(qs(p.doc["Application"].value("CompileTargetCommandOverride", "make -f ../gcc/Makefile"))).captured(1);
    QFile f(QDir(p.dir).filePath(mk));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QDir root(QFileInfo(f).absolutePath() + "/.."); // the Makefile builds from its parent folder
    auto realCase = [&](const QString& rel) {
        QString cur;
        for (const QString& seg : rel.split('/')) {
            const QDir d(root.filePath(cur));
            QString hit = d.exists(seg) ? seg : QString();
            for (const QString& e : d.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
                if (hit.isEmpty() && e.compare(seg, Qt::CaseInsensitive) == 0) hit = e;
            if (hit.isEmpty()) return rel; // not on disk in any case, leave it alone
            cur += (cur.isEmpty() ? "" : "/") + hit;
        }
        return cur;
    };
    QRegularExpression var("^(components|touchgfx_generator_components|cubemx_components)\\s*:=\\s*(.*)$");
    for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
        auto m = var.match(line.trimmed());
        if (!m.hasMatch()) continue;
        QStringList words = m.captured(2).split(' ', Qt::SkipEmptyParts), fixed;
        for (const QString& w : words) fixed << realCase(w);
        if (fixed != words) out << "'" + m.captured(1) + "=" + fixed.join(' ') + "'";
    }
    return out;
}

void MainWindow::log(const QString& s)
{
    if (s.isEmpty()) return;
    fprintf(stdout, "%s\n", qPrintable(s));
    fflush(stdout);
    logView->appendPlainText(s);
    logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (dirty) {
        auto r = QMessageBox::question(this, "ErintesGFX", "Save changes?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel || (r == QMessageBox::Save && !save())) {
            e->ignore();
            return;
        }
    }
    QSettings().setValue("windowState", saveState());
    e->accept();
}
