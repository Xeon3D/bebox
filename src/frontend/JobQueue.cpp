/*
 * BeBox emulator - jobs run one after the other on a thread of their own.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#include "JobQueue.h"


JobQueue::JobQueue()
	:
	fBusy(false),
	fQuit(false),
	fThread(&JobQueue::_Run, this)
{
}


JobQueue::~JobQueue()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fQuit = true;
	}
	fCondition.notify_all();
	fThread.join();
}


void
JobQueue::Add(std::function<void()> job)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fJobs.push_back(std::move(job));
	}
	fCondition.notify_all();
}


void
JobQueue::Drain()
{
	std::unique_lock<std::mutex> lock(fLock);
	fCondition.wait(lock, [this]() { return fJobs.empty() && !fBusy; });
}


void
JobQueue::_Run()
{
	std::unique_lock<std::mutex> lock(fLock);
	while (true) {
		fCondition.wait(lock, [this]() { return fQuit || !fJobs.empty(); });
		if (fJobs.empty())
			return;		// quitting
		std::function<void()> job = std::move(fJobs.front());
		fJobs.pop_front();
		fBusy = true;
		lock.unlock();
		job();
		lock.lock();
		fBusy = false;
		fCondition.notify_all();
	}
}
