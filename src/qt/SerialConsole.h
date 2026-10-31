/*
 * BeBox emulator for Qt - a serial port's console window.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * A SerialBackend: what the BeBox sends shows in a read only text view (a
 * terminal of sorts: CR, LF, backspace and tab), and what is typed in the
 * window goes back to it. Transmit() and Receive() run on the emulation
 * thread; the window lives on the UI thread.
 */
#ifndef QT_SERIAL_CONSOLE_H
#define QT_SERIAL_CONSOLE_H

#include "core/host/SerialBackend.h"

#include <QPlainTextEdit>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

class SerialConsole : public QPlainTextEdit, public bebox::SerialBackend {
	Q_OBJECT
public:
								SerialConsole(int port,
									QWidget* parent = nullptr);

			void				ShowWindow();
			void				Clear();

	virtual	void				Transmit(bebox::u8 byte) override;
	virtual	int					Receive() override;

protected:
	virtual	void				keyPressEvent(QKeyEvent* event) override;
	virtual	void				insertFromMimeData(const QMimeData* source)
									override;
	virtual	void				closeEvent(QCloseEvent* event) override;

private:
			void				_Flush();

			std::mutex			fLock;
			std::string			fPending;		// output not shown yet
			std::atomic<bool>	fFlushPosted;
			std::deque<bebox::u8> fInput;
			QString				fLine;			// the last line, as shown
			bool				fCarriageReturn;
};

#endif	// QT_SERIAL_CONSOLE_H
