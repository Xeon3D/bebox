/*
 * BeBox emulator for Qt - sound output through Qt Multimedia.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "AudioOutput.h"

#include "core/Log.h"

#ifdef HAVE_QT_MULTIMEDIA
#	include <QAudioDevice>
#	include <QAudioFormat>
#	include <QAudioSink>
#	include <QMediaDevices>
#endif

#include <algorithm>

using bebox::s16;
using bebox::u32;

namespace {

// The ring's fill level aimed at, in guest frames (about 100 ms at
// 44.1 kHz): enough to ride out the emulation's time slices and the host
// scheduler. The resampling step goes up to 2% faster or slower to keep it.
const double kTargetFill = 4096;
const double kMaxCorrection = 0.02;

}	// namespace


AudioOutput::AudioOutput(QObject* parent)
	:
	QIODevice(parent),
	fRing(kRingFrames * 2, 0),
	fWrite(0),
	fRead(0),
	fGuestRate(44100),
	fVolume(1.0f),
	fOutputRate(44100),
	fSink(nullptr),
	fPosition(0),
	fSmoothedFill(0),
	fPrimed(false)
{
}


AudioOutput::~AudioOutput()
{
	Stop();
}


bool
AudioOutput::Start()
{
#ifdef HAVE_QT_MULTIMEDIA
	if (fSink != nullptr)
		return true;
	QAudioDevice device = QMediaDevices::defaultAudioOutput();
	if (device.isNull()) {
		BB_LOG(kLogHost, kLogWarning, "sound: no audio output device");
		return false;
	}
	QAudioFormat format;
	format.setSampleRate(44100);
	format.setChannelCount(2);
	format.setSampleFormat(QAudioFormat::Int16);
	if (!device.isFormatSupported(format)) {
		format.setSampleRate(device.preferredFormat().sampleRate());
		if (!device.isFormatSupported(format)) {
			BB_LOG(kLogHost, kLogWarning,
				"sound: the output takes no 16-bit stereo");
			return false;
		}
	}
	fOutputRate = format.sampleRate();
	fWrite = fRead = 0;
	fPosition = 0;
	fSmoothedFill = 0;
	fPrimed = false;
	open(QIODevice::ReadOnly);
	fSink = new QAudioSink(device, format, this);
	// 80 ms queued at the sound card
	fSink->setBufferSize(fOutputRate * 4 * 80 / 1000);
	fSink->setVolume(fVolume);
	fSink->start(this);
	return true;
#else
	return false;
#endif
}


void
AudioOutput::Stop()
{
#ifdef HAVE_QT_MULTIMEDIA
	if (fSink == nullptr)
		return;
	fSink->stop();
	delete fSink;
	fSink = nullptr;
	close();
#endif
}


bool
AudioOutput::IsRunning() const
{
	return fSink != nullptr;
}


void
AudioOutput::SetVolume(float volume)
{
	fVolume = volume;
#ifdef HAVE_QT_MULTIMEDIA
	if (fSink != nullptr)
		fSink->setVolume(volume);
#endif
}


// Emulation thread.
void
AudioOutput::Play(const s16* frames, size_t count, u32 sampleRate)
{
	if (sampleRate != 0)
		fGuestRate = sampleRate;
	size_t write = fWrite.load(std::memory_order_relaxed);
	size_t read = fRead.load(std::memory_order_acquire);
	size_t space = kRingFrames - (write - read);
	count = std::min(count, space);		// a full ring drops the rest
	for (size_t i = 0; i < count; i++) {
		size_t slot = (write + i) % kRingFrames;
		fRing[slot * 2] = frames[i * 2];
		fRing[slot * 2 + 1] = frames[i * 2 + 1];
	}
	fWrite.store(write + count, std::memory_order_release);
}


// The sink's side: whole output frames from the ring.
qint64
AudioOutput::readData(char* data, qint64 maxSize)
{
	size_t frames = (size_t)maxSize / 4;
	s16* out = (s16*)data;
	size_t read = fRead.load(std::memory_order_relaxed);
	size_t write = fWrite.load(std::memory_order_acquire);
	double available = (double)(write - read);
	// start once the ring has filled up to the level aimed at
	if (!fPrimed) {
		if (available < kTargetFill) {
			std::fill(out, out + frames * 2, 0);
			return (qint64)frames * 4;
		}
		fPrimed = true;
		fSmoothedFill = available;
	}
	fSmoothedFill += (available - fSmoothedFill) * 0.05;
	double correction = (fSmoothedFill - kTargetFill) / kTargetFill * 0.05;
	correction = std::max(-kMaxCorrection, std::min(kMaxCorrection,
		correction));
	double step = fGuestRate.load() / (double)fOutputRate * (1 + correction);
	for (size_t i = 0; i < frames; i++) {
		size_t whole = (size_t)fPosition;
		if (read + whole + 1 >= write) {
			// ran dry: silence until the ring has filled up again
			std::fill(out + i * 2, out + frames * 2, 0);
			fPrimed = false;
			break;
		}
		double fraction = fPosition - whole;
		size_t a = (read + whole) % kRingFrames;
		size_t b = (read + whole + 1) % kRingFrames;
		for (int channel = 0; channel < 2; channel++) {
			double sample = fRing[a * 2 + channel] * (1 - fraction)
				+ fRing[b * 2 + channel] * fraction;
			out[i * 2 + channel] = (s16)std::max(-32768.0,
				std::min(32767.0, sample));
		}
		fPosition += step;
	}
	size_t consumed = (size_t)fPosition;
	fPosition -= consumed;
	fRead.store(read + std::min(consumed, (size_t)(write - read)),
		std::memory_order_release);
	return (qint64)frames * 4;
}


qint64
AudioOutput::writeData(const char* data, qint64 size)
{
	return -1;
}


// Always some: silence when the ring is empty, so the sink keeps pulling.
qint64
AudioOutput::bytesAvailable() const
{
	return 4096 + QIODevice::bytesAvailable();
}
