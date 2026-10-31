/*
 * BeBox emulator - jobs run one after the other on a thread of their own.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 *
 * For the shared folders' copies, which take seconds with a big folder:
 * a folder goes back to the host before it is copied again.
 */
#ifndef BEBOX_FRONTEND_JOB_QUEUE_H
#define BEBOX_FRONTEND_JOB_QUEUE_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class JobQueue {
public:
								JobQueue();
								~JobQueue();

			void				Add(std::function<void()> job);
			// Waits until all jobs ran.
			void				Drain();

private:
			void				_Run();

			std::mutex			fLock;
			std::condition_variable fCondition;
			std::deque<std::function<void()> > fJobs;
			bool				fBusy;
			bool				fQuit;
			std::thread			fThread;
};

#endif	// BEBOX_FRONTEND_JOB_QUEUE_H
