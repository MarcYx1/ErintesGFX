#pragma once
#include "project.h"

#include <QWidget>

// Draws one screen like the target would and lets you select, move and resize widgets.
// A widget is identified by its JSON pointer, e.g. /Application/Screens/0/Components/2/Components/1.
class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(Project& p);

    void setScreen(int i);
    int screen() const { return screenIndex; }
    QString base() const { return QString("/Application/Screens/%1/Components").arg(screenIndex); }
    void select(const QString& ptr);
    QString selection() const { return sel; }
    void setZoom(double z);
    double zoom() const { return scale; }

signals:
    void selected(const QString& ptr);
    void moved();        // geometry changed while dragging
    void editFinished(); // drag or nudge done, time for an undo snapshot

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    void paintList(class QPainter& g, const Json& comps, const QString& ptr);
    void paintWidget(class QPainter& g, const Json& w);
    QString hit(const Json& comps, const QString& ptr, QPointF pt) const;
    QPoint offsetOf(const QString& ptr) const; // sum of parent container positions
    QRect rectOf(const QString& ptr) const;    // absolute, in screen pixels
    bool resizable(const Json& w) const;
    QRectF handle() const;                     // resize handle of the selection, widget coords

    Project& p;
    int screenIndex = 0;
    QString sel;
    double scale = 1.0;
    enum { None, Move, Resize } mode = None;
    QPointF dragStart;
    QRect dragOrig;
    bool dragged = false;
};
