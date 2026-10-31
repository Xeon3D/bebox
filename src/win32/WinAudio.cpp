/*
 * BeBox emulator for Windows - sound output through waveOut.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "WinAudio.h"

#include "core/Log.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>

using bebox::s16;
using bebox::u32;

namespace {

const u32 kOutputRate = 44100;
// Four buffers of 20 ms: up to 80 ms queued at the sound card.
const size_t kBufferFrames = 882;
const int kBufferCount = 4;
// The ring's fill level aimed at (frames at the guest's rate, so about
// 100 ms at 44.1 kHz): enough to ride out the emulation's time slices
// and the host scheduler, and the latency on top of the buffers.
const double kTargetFill = 4096;
// The resampling step goes up to 2% faster or slower to hold that level.
const double kMaxCorrection = 0.02;

}	// namespace


WinAudio::WinAudio()
	:
	fRing(kRingFrames * 2, 0),
	fWrite(0),
	fRead(0),
	fGuestRate(44100),
	fVolume(1.0f),
	fMuted(false),
	fQuit(false),
	fPosition(0),
	fSmoothedFill(0),
	fPrimed(false)
{
}


WinAudio::~WinAudio()
{
	Stop();
}


bool
WinAudio::Start()
{
	if (fThread.joinable())
		return true;
	fQuit = false;
	fWrite = fRead = 0;
	fPosition = 0;
	fSmoothedFill = 0;
	fPrimed = false;
	fThread = std::thread(&WinAudio::_Run, this);
	return true;
}


void
WinAudio::Stop()
{
	if (!fThread.joinable())
		return;
	fQuit = true;
	fThread.join();
}


// Emulation thread.
void
WinAudio::Play(const s16* frames, size_t count, u32 sampleRate)
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


// Player thread: 'frames' output frames from the ring.
void
WinAudio::_Fill(s16* out, size_t frames)
{
	size_t read = fRead.load(std::memory_order_relaxed);
	size_t write = fWrite.load(std::memory_order_acquire);
	double available = (double)(write - read);
	// start once the ring has filled up to the level aimed at
	if (!fPrimed) {
		if (available < kTargetFill) {
			std::fill(out, out + frames * 2, 0);
			return;
		}
		fPrimed = true;
		fSmoothedFill = available;
	}
	fSmoothedFill += (available - fSmoothedFill) * 0.05;
	double correction = (fSmoothedFill - kTargetFill) / kTargetFill * 0.05;
	correction = std::max(-kMaxCorrection, std::min(kMaxCorrection,
		correction));
	double step = fGuestRate.load() / (double)kOutputRate
		* (1 + correction);
	float volume = fMuted ? 0.0f : fVolume.load();
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
			sample *= volume;
			out[i * 2 + channel] = (s16)std::max(-32768.0,
				std::min(32767.0, sample));
		}
		fPosition += step;
	}
	size_t consumed = (size_t)fPosition;
	fPosition -= consumed;
	fRead.store(read + std::min(consumed, (size_t)(write - read)),
		std::memory_order_release);
}


void
WinAudio::_Run()
{
	WAVEFORMATEX format = {};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 2;
	format.nSamplesPerSec = kOutputRate;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 4;
	format.nAvgBytesPerSec = kOutputRate * 4;
	HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	HWAVEOUT device;
	if (waveOutOpen(&device, WAVE_MAPPER, &format, (DWORD_PTR)event, 0,
			CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		BB_LOG(kLogHost, kLogWarning,
			"sound: no audio output device");
		CloseHandle(event);
		return;
	}
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	std::vector<s16> samples(kBufferFrames * 2 * kBufferCount, 0);
	WAVEHDR headers[kBufferCount] = {};
	for (int i = 0; i < kBufferCount; i++) {
		headers[i].lpData = (LPSTR)&samples[kBufferFrames * 2 * i];
		headers[i].dwBufferLength = kBufferFrames * 4;
		waveOutPrepareHeader(device, &headers[i], sizeof(WAVEHDR));
		_Fill((s16*)headers[i].lpData, kBufferFrames);
		waveOutWrite(device, &headers[i], sizeof(WAVEHDR));
	}
	while (!fQuit) {
		WaitForSingleObject(event, 50);
		for (WAVEHDR& header : headers) {
			if ((header.dwFlags & WHDR_DONE) == 0)
				continue;
			_Fill((s16*)header.lpData, kBufferFrames);
			waveOutWrite(device, &header, sizeof(WAVEHDR));
		}
	}
	waveOutReset(device);
	for (WAVEHDR& header : headers)
		waveOutUnprepareHeader(device, &header, sizeof(WAVEHDR));
	waveOutClose(device);
	CloseHandle(event);
}
