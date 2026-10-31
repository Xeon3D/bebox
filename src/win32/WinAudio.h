/*
 * BeBox emulator for Windows - sound output through waveOut.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * The emulation thread writes the codec's frames into a single producer,
 * single consumer ring; a player thread refills the waveOut buffers from
 * it, resampling linearly from the guest's rate to 44.1 kHz, a little
 * faster or slower to keep the ring's fill level steady while the
 * emulated and the sound card's clocks drift apart. An empty ring plays
 * silence (the emulation is behind), a full one drops frames.
 */
#ifndef WIN32_AUDIO_H
#define WIN32_AUDIO_H

#include "core/host/AudioSink.h"

#include <atomic>
#include <thread>
#include <vector>

class WinAudio : public bebox::AudioSink {
public:
								WinAudio();
	virtual						~WinAudio();

			bool				Start();
			void				Stop();
			bool				IsRunning() const { return fThread.joinable(); }
			void				SetVolume(float volume) { fVolume = volume; }
			void				SetMuted(bool muted) { fMuted = muted; }

	virtual	void				Play(const bebox::s16* frames, size_t count,
									bebox::u32 sampleRate);

private:
			void				_Run();
			void				_Fill(bebox::s16* out, size_t frames);

	static	const size_t		kRingFrames = 32768;

			std::vector<bebox::s16> fRing;		// stereo frames
			std::atomic<size_t>	fWrite;			// frames written, ever
			std::atomic<size_t>	fRead;			// frames read, ever
			std::atomic<bebox::u32> fGuestRate;
			std::atomic<float>	fVolume;
			std::atomic<bool>	fMuted;
			std::atomic<bool>	fQuit;
			std::thread			fThread;

			// player thread only
			double				fPosition;		// between ring frames
			double				fSmoothedFill;
			bool				fPrimed;
};

#endif	// WIN32_AUDIO_H
