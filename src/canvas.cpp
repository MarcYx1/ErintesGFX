#include "canvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QtMath>

namespace {

int num(const Json& j, const char* k, int def = 0) { return j.contains(k) ? (int)j[k].get<double>() : def; }
double flt(const Json& j, const char* k, double def = 0) { return j.contains(k) ? j[k].get<double>() : def; }
QString str(const Json& j, const char* k) { return j.contains(k) ? QString::fromStdString(j[k].get<std::string>()) : QString(); }

QColor color(const Json& w)
{
    Json c = w.value("Color", Json::object());
    return QColor(num(c, "Red"), num(c, "Green"), num(c, "Blue"));
}

QRect rectOfWidget(const Json& w) { return QRect(num(w, "X"), num(w, "Y"), num(w, "Width"), num(w, "Height")); }

Qt::PenCapStyle cap(const QString& s)
{
    return s == "Round" ? Qt::RoundCap : s == "Square" ? Qt::SquareCap : Qt::FlatCap;
}

// Fraction of a progress indicator that is filled.
double progress(const Json& w)
{
    double lo = num(w, "ProgressRangeMin"), hi = num(w, "ProgressRangeMax"), v = num(w, "ProgressInitialValue");
    return hi > lo ? qBound(0.0, (v - lo) / (hi - lo), 1.0) : 0.0;
}

} // namespace

Canvas::Canvas(Project& proj) : p(proj)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setZoom(1.0);
}

void Canvas::setScreen(int i)
{
    screenIndex = i;
    sel.clear();
    update();
}

void Canvas::select(const QString& ptr)
{
    sel = ptr;
    update();
}

void Canvas::setZoom(double z)
{
    scale = qBound(0.25, z, 8.0);
    const Json& r = p.app()["Resolution"];
    setFixedSize(int(num(r, "Width") * scale), int(num(r, "Height") * scale));
    update();
}

void Canvas::paintEvent(QPaintEvent*)
{
    QPainter g(this);
    g.scale(scale, scale);
    const Json& r = p.app()["Resolution"];
    g.fillRect(0, 0, num(r, "Width"), num(r, "Height"), Qt::black); // __background
    Json& s = p.screens()[screenIndex];
    paintList(g, s.value("Components", Json::array()), base());

    if (sel.isEmpty()) return;
    const Json& w = p.doc[Json::json_pointer(sel.toStdString())];
    g.resetTransform();
    QRect r2 = rectOf(sel);
    QRectF box(r2.x() * scale, r2.y() * scale, r2.width() * scale, r2.height() * scale);
    const bool locked = w.value("LockPosition", false);
    g.setPen(QPen(locked ? QColor(255, 150, 0) : QColor(0, 200, 255), 1, Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    g.drawRect(box.adjusted(0, 0, -1, -1));
    if (!locked && resizable(w)) g.fillRect(handle(), QColor(0, 200, 255));
}

void Canvas::paintList(QPainter& g, const Json& comps, const QString& ptr)
{
    for (size_t i = 0; i < comps.size(); ++i) {
        const Json& w = comps[i];
        if (!w.value("Visible", true)) continue;
        g.save();
        QRect r = rectOfWidget(w);
        g.translate(r.topLeft());
        g.setClipRect(QRect(QPoint(0, 0), r.size()), Qt::IntersectClip);
        if (w.contains("Alpha")) g.setOpacity(g.opacity() * num(w, "Alpha") / 255.0);
        if (str(w, "Type") == "Container")
            paintList(g, w.value("Components", Json::array()), ptr + "/" + QString::number(i) + "/Components");
        else
            paintWidget(g, w);
        g.restore();
    }
}

// Paints one widget with its top-left at (0, 0).
void Canvas::paintWidget(QPainter& g, const Json& w)
{
    const QString type = str(w, "Type");
    const int W = num(w, "Width"), H = num(w, "Height");
    g.setRenderHint(QPainter::Antialiasing, type == "Line" || type == "Shape" || type == "LineProgress");

    if (type == "Box") {
        g.fillRect(0, 0, W, H, color(w));
    } else if (type == "Image") {
        g.drawPixmap(0, 0, p.image(str(w, "RelativeFilename")));
    } else if (type == "TextArea") {
        TextEntry e = p.texts.get(str(w, "TextId"));
        QString text = e.text;
        if (w.contains("Wildcard1")) {
            const Json& wc = w["Wildcard1"];
            QString value = wc.contains("TextId") ? p.texts.get(str(wc, "TextId")).text : QString();
            text.replace(QRegularExpression("<[^>]*>"), value);
        }
        g.setFont(p.font(e.typography));
        g.setPen(color(w));
        int align = e.alignment == "Center" ? Qt::AlignHCenter : e.alignment == "Right" ? Qt::AlignRight : Qt::AlignLeft;
        g.drawText(QRect(0, 0, W, H), align | Qt::AlignTop, text);
    } else if (type == "Line" || type == "LineProgress") {
        if (type == "LineProgress" && w.contains("FileNameBackground")) g.drawPixmap(0, 0, p.image(str(w, "FileNameBackground")));
        QPointF a(flt(w, "StartX"), flt(w, "StartY")), b(flt(w, "EndX"), flt(w, "EndY"));
        if (type == "LineProgress") b = a + (b - a) * progress(w);
        g.setPen(QPen(color(w), flt(w, "LineWidth", 1), Qt::SolidLine, cap(str(w, "LineEndingStyle"))));
        if (a != b) g.drawLine(a, b);
    } else if (type == "Shape") {
        // TouchGFX: point -> scale -> rotate (degrees) -> move by origin
        QTransform t;
        t.translate(flt(w, "XOrigin"), flt(w, "YOrigin"));
        t.rotate(flt(w, "Angle"));
        t.scale(flt(w, "XScale", 1), flt(w, "YScale", 1));
        QPolygonF poly;
        for (const Json& pt : w["Points"]) poly << t.map(QPointF(flt(pt, "X"), flt(pt, "Y")));
        g.setPen(Qt::NoPen);
        g.setBrush(color(w));
        g.drawPolygon(poly);
    } else if (type == "BoxProgress") {
        if (w.contains("FileNameBackground")) g.drawPixmap(0, 0, p.image(str(w, "FileNameBackground")));
        QRectF ind(num(w, "IndicatorPositionX"), num(w, "IndicatorPositionY"), num(w, "IndicatorPositionWidth", W),
                   num(w, "IndicatorPositionHeight", H));
        const double f = progress(w);
        const QString dir = str(w, "Direction");
        QRectF bar = ind;
        if (dir == "Left") bar.setLeft(ind.right() - ind.width() * f);
        else if (dir == "Up") bar.setTop(ind.bottom() - ind.height() * f);
        else if (dir == "Down") bar.setHeight(ind.height() * f);
        else bar.setWidth(ind.width() * f);
        g.fillRect(bar, color(w));
    } else {
        // Unknown widget: show where it is so it can still be selected and moved.
        g.setPen(QPen(Qt::magenta, 1, Qt::DotLine));
        g.drawRect(0, 0, W - 1, H - 1);
        g.drawText(QRect(0, 0, W, H), Qt::AlignCenter, type);
    }
}

QString Canvas::hit(const Json& comps, const QString& ptr, QPointF pt) const
{
    for (int i = int(comps.size()) - 1; i >= 0; --i) {
        const Json& w = comps[i];
        QRect r = rectOfWidget(w);
        if (!w.value("Visible", true) || !QRectF(r).contains(pt)) continue;
        const QString me = ptr + "/" + QString::number(i);
        if (str(w, "Type") == "Container") {
            QString child = hit(w.value("Components", Json::array()), me + "/Components", pt - r.topLeft());
            if (!child.isEmpty()) return child;
        }
        return me;
    }
    return {};
}

QPoint Canvas::offsetOf(const QString& ptr) const
{
    const QStringList parts = ptr.mid(base().size() + 1).split("/Components/");
    QPoint o;
    const Json* comps = &p.doc[Json::json_pointer(base().toStdString())];
    for (int k = 0; k + 1 < parts.size(); ++k) {
        const Json& c = (*comps)[parts[k].toInt()];
        o += QPoint(num(c, "X"), num(c, "Y"));
        comps = &c["Components"];
    }
    return o;
}

QRect Canvas::rectOf(const QString& ptr) const
{
    return rectOfWidget(p.doc[Json::json_pointer(ptr.toStdString())]).translated(offsetOf(ptr));
}

bool Canvas::resizable(const Json& w) const
{
    const std::string t = w.value("Type", "");
    return t != "Image" && !(t == "TextArea" && w.value("AutoSize", false));
}

QRectF Canvas::handle() const
{
    QRect r = rectOf(sel);
    return QRectF(r.right() * scale - 3, r.bottom() * scale - 3, 8, 8);
}

void Canvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    setFocus();
    const QPointF pt = e->position() / scale;
    mode = None;
    dragged = false;
    if (!sel.isEmpty() && handle().contains(e->position())) {
        const Json& w = p.doc[Json::json_pointer(sel.toStdString())];
        if (!w.value("LockPosition", false) && resizable(w)) mode = Resize;
    }
    if (mode == None) {
        QString h = hit(p.screens()[screenIndex].value("Components", Json::array()), base(), pt);
        if (h != sel) {
            sel = h;
            emit selected(sel);
        }
        if (!sel.isEmpty() && !p.doc[Json::json_pointer(sel.toStdString())].value("LockPosition", false)) mode = Move;
    }
    if (mode != None) {
        dragStart = pt;
        dragOrig = rectOfWidget(p.doc[Json::json_pointer(sel.toStdString())]);
    }
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent* e)
{
    if (mode == None) {
        setCursor(!sel.isEmpty() && handle().contains(e->position()) ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
        return;
    }
    const QPoint d = (e->position() / scale - dragStart).toPoint();
    Json& w = p.doc[Json::json_pointer(sel.toStdString())];
    if (mode == Move) {
        setKey(w, "X", dragOrig.x() + d.x(), 0);
        setKey(w, "Y", dragOrig.y() + d.y(), 0);
    } else {
        setKey(w, "Width", qMax(1, dragOrig.width() + d.x()), 0);
        setKey(w, "Height", qMax(1, dragOrig.height() + d.y()), 0);
    }
    dragged = d != QPoint();
    emit moved();
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent*)
{
    if (dragged) emit editFinished();
    mode = None;
    dragged = false;
}

void Canvas::keyPressEvent(QKeyEvent* e)
{
    QPoint d;
    switch (e->key()) {
    case Qt::Key_Left: d = {-1, 0}; break;
    case Qt::Key_Right: d = {1, 0}; break;
    case Qt::Key_Up: d = {0, -1}; break;
    case Qt::Key_Down: d = {0, 1}; break;
    default: QWidget::keyPressEvent(e); return;
    }
    if (sel.isEmpty()) return;
    Json& w = p.doc[Json::json_pointer(sel.toStdString())];
    if (w.value("LockPosition", false)) return;
    if (e->modifiers() & Qt::ShiftModifier) d *= 10;
    setKey(w, "X", num(w, "X") + d.x(), 0);
    setKey(w, "Y", num(w, "Y") + d.y(), 0);
    emit moved();
    emit editFinished();
    update();
}

void Canvas::wheelEvent(QWheelEvent* e)
{
    if (!(e->modifiers() & Qt::ControlModifier)) {
        e->ignore(); // let the scroll area scroll
        return;
    }
    setZoom(scale * (e->angleDelta().y() > 0 ? 1.25 : 0.8));
}
