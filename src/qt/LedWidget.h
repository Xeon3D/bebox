/*
 * BeBox emulator for Qt - a front panel light pipe.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * Sixteen LEDs behind a strip of dark acrylic: from the bottom, the power
 * LED (left column) or the disk LED (right column), then fifteen LEDs of
 * the CPU load bar graph. Each LED's light spreads along the pipe and
 * adds up with its neighbours'.
 */
#ifndef QT_LED_WIDGET_H
#define QT_LED_WIDGET_H

#include <QWidget>

class LedWidget : public QWidget {
	Q_OBJECT
public:
								LedWidget(bool withDisk,
									QWidget* parent = nullptr);

			// 'level' 0-15 of the load bar; 'disk' the disk LED's duty
			// cycle (0-1, the right column only).
			void				SetLevels(float level, bool power, float disk);
			void				SetStyle(int style);

	virtual	QSize				sizeHint() const override;

protected:
	virtual	void				paintEvent(QPaintEvent* event) override;

private:
			bool				fWithDisk;
			float				fLevel;
			bool				fPower;
			float				fDisk;
			int					fStyle;
};

#endif	// QT_LED_WIDGET_H
