#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

// Keep this alive until all application resources have been destroyed.
class ShutdownDeadline {
public:
	~ShutdownDeadline() {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			finished_ = true;
		}
		wake_.notify_one();
		if (worker_.joinable())
			worker_.join();
	}

	void arm() {
		const auto deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(2);
		worker_ = std::thread([this, deadline] {
			std::unique_lock<std::mutex> lock(mutex_);
			if (!wake_.wait_until(lock, deadline,
								  [this] { return finished_; })) {
				// Bypass destructors and exit handlers, which may themselves be
				// blocked.
				std::_Exit(EXIT_FAILURE);
			}
		});
	}

private:
	std::mutex				mutex_;
	std::condition_variable wake_;
	bool					finished_ = false;
	std::thread				worker_;
};
