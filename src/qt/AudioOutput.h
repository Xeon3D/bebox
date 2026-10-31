/*
 * BeBox emulator for Qt - sound output through Qt Multimedia.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The emulation thread writes the codec's frames into a single producer,
 * single consumer ring; QAudioSink pulls them (readData()), resampled
 * linearly from the guest's rate to the output's, a little faster or
 * slower to keep the ring's fill level steady while the emulated and the
 * sound card's clocks drift apart. An empty ring plays silence (the
 * emulation is behind), a full one drops frames.
 *
 * Built without Qt Multimedia, it is silent.
 */
#ifndef QT_AUDIO_OUTPUT_H
#define QT_AUDIO_OUTPUT_H

#include "core/host/AudioSink.h"

#include <QIODevice>

#include <atomic>
#include <memory>
#include <vector>

class QAudioSink;

class AudioOutput : public QIODevice, public bebox::AudioSink {
	Q_OBJECT
public:
								AudioOutput(QObject* parent = nullptr);
	virtual						~AudioOutput();

			bool				Start();
			void				Stop();
			bool				IsRunning() const;
			void				SetVolume(float volume);

	virtual	void				Play(const bebox::s16* frames, size_t count,
									bebox::u32 sampleRate) override;

protected:
	virtual	qint64				readData(char* data, qint64 maxSize) override;
	virtual	qint64				writeData(const char* data, qint64 size)
									override;
	virtual	qint64				bytesAvailable() const override;
	virtual	bool				isSequential() const override { return true; }

private:
	static	const size_t		kRingFrames = 32768;

			std::vector<bebox::s16> fRing;		// stereo frames
			std::atomic<size_t>	fWrite;			// frames written, ever
			std::atomic<size_t>	fRead;			// frames read, ever
			std::atomic<bebox::u32> fGuestRate;
			std::atomic<float>	fVolume;
			bebox::u32			fOutputRate;
			QAudioSink*			fSink;

			// the pulling side only
			double				fPosition;
			double				fSmoothedFill;
			bool				fPrimed;
};

#endif	// QT_AUDIO_OUTPUT_H
