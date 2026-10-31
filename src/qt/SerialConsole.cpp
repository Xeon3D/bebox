/*
 * BeBox emulator for Qt - a serial port's console window.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "SerialConsole.h"

#include <QCloseEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMimeData>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

namespace {

// The view keeps the last 5000 lines.
const int kMaxLines = 5000;

}	// namespace


SerialConsole::SerialConsole(int port, QWidget* parent)
	:
	QPlainTextEdit(parent),
	fFlushPosted(false),
	fCarriageReturn(false)
{
	setWindowFlag(Qt::Window);
	setWindowTitle(tr("BeBox serial port %1").arg(port + 1));
	setReadOnly(true);
	setTextInteractionFlags(Qt::TextSelectableByMouse
		| Qt::TextSelectableByKeyboard);
	setMaximumBlockCount(kMaxLines);
	setLineWrapMode(QPlainTextEdit::NoWrap);
	setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	resize(760, 480);
}


void
SerialConsole::ShowWindow()
{
	show();
	raise();
	activateWindow();
	setFocus();
}


void
SerialConsole::Clear()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fPending.clear();
		fInput.clear();
	}
	fLine.clear();
	fCarriageReturn = false;
	clear();
}


// Emulation thread.
void
SerialConsole::Transmit(bebox::u8 byte)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (fPending.size() < 256 * 1024)
			fPending.push_back((char)byte);
	}
	if (!fFlushPosted.exchange(true))
		QMetaObject::invokeMethod(this, [this]() { _Flush(); },
			Qt::QueuedConnection);
}


// Emulation thread.
int
SerialConsole::Receive()
{
	std::lock_guard<std::mutex> lock(fLock);
	if (fInput.empty())
		return -1;
	bebox::u8 byte = fInput.front();
	fInput.pop_front();
	return byte;
}


// The output so far into the view, as a simple terminal would show it:
// the last line is rewritten as it changes.
void
SerialConsole::_Flush()
{
	std::string pending;
	{
		std::lock_guard<std::mutex> lock(fLock);
		pending.swap(fPending);
	}
	fFlushPosted = false;
	if (pending.empty())
		return;
	QStringList finished;
	for (char c : pending) {
		unsigned char byte = (unsigned char)c;
		if (byte == '\n') {
			finished.append(fLine);
			fLine.clear();
			fCarriageReturn = false;
			continue;
		}
		if (byte == '\r') {
			fCarriageReturn = true;
			continue;
		}
		if (fCarriageReturn) {
			// a carriage return not followed by a line feed: back to the
			// line's start
			fLine.clear();
			fCarriageReturn = false;
		}
		if (byte == '\b') {
			fLine.chop(1);
		} else if (byte == '\t') {
			do {
				fLine.append(' ');
			} while (fLine.size() % 8 != 0);
		} else if (byte >= 0x20 && byte != 0x7f) {
			// Latin-1 like a terminal of the time
			fLine.append(QChar(byte));
		}
	}
	QScrollBar* bar = verticalScrollBar();
	bool atEnd = bar->value() == bar->maximum();
	// the last line as it was shown goes, then the new text
	QTextCursor cursor(document());
	cursor.movePosition(QTextCursor::End);
	cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
	cursor.removeSelectedText();
	QString text = finished.join('\n');
	if (!finished.isEmpty())
		text += '\n';
	cursor.insertText(text + fLine);
	if (atEnd)
		bar->setValue(bar->maximum());
}


// Typed characters go to the BeBox instead of into the view.
void
SerialConsole::keyPressEvent(QKeyEvent* event)
{
	if (event->matches(QKeySequence::Copy)
		&& textCursor().hasSelection()) {
		copy();
		return;
	}
	if (event->matches(QKeySequence::Paste)) {
		paste();
		return;
	}
	QByteArray bytes;
	switch (event->key()) {
		case Qt::Key_Return:
		case Qt::Key_Enter:
			bytes = "\r";
			break;
		case Qt::Key_Backspace:
			bytes = "\b";
			break;
		case Qt::Key_Escape:
			bytes = "\x1b";
			break;
		default:
			bytes = event->text().toLatin1();
			break;
	}
	if (bytes.isEmpty()) {
		QPlainTextEdit::keyPressEvent(event);
		return;
	}
	std::lock_guard<std::mutex> lock(fLock);
	for (char c : bytes)
		fInput.push_back((bebox::u8)c);
}


void
SerialConsole::insertFromMimeData(const QMimeData* source)
{
	QByteArray bytes = source->text().toLatin1();
	std::lock_guard<std::mutex> lock(fLock);
	for (char c : bytes) {
		if (c != '\n')
			fInput.push_back((bebox::u8)c);
	}
}


// Hidden, not destroyed: the port stays connected.
void
SerialConsole::closeEvent(QCloseEvent* event)
{
	event->ignore();
	hide();
}
