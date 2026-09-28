#pragma once

#include <QRectF>
#include <QWidget>

#include "core/pokewalker/pocketwalker.h"

class QPushButton;
class QResizeEvent;

class AndroidDisplayWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit AndroidDisplayWidget(QWidget* parent = nullptr);
    void setEmulator(PocketWalker* emulator);
    void setControlButtons(QPushButton* left, QPushButton* center, QPushButton* right);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRectF deviceRect() const;
    QRectF screenRect(const QRectF& device) const;
    void placeButton(QPushButton* button, const QPointF& center, int diameter, int font_size);
    void setArrowIcon(QPushButton* button, bool points_left, int diameter);

    PocketWalker* emulator = nullptr;
    QPushButton* left_button = nullptr;
    QPushButton* center_button = nullptr;
    QPushButton* right_button = nullptr;
};
