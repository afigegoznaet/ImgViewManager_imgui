#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
class DirectoryWatcher {
public:
	struct Change {
		std::filesystem::path path;
		bool				  removed = false, directory = false;
	};
	struct Changes {
		std::vector<Change> items;
		bool				resync = false;
		bool				viewResync = false;
		std::string			error;
	};
	DirectoryWatcher();
	~DirectoryWatcher();
	void	requestStop();
	void	set(const std::filesystem::path &folder, bool recursive,
				const std::set<std::filesystem::path> &expanded);
	Changes takeChanges();
	bool	ready() const { return applied_ == revision_; }

private:
	void							work();
	std::mutex						mutex_;
	std::filesystem::path			folder_;
	bool							recursive_ = true;
	std::set<std::filesystem::path> expanded_;
	Changes							changes_;
	std::atomic<bool>				stopping_{false};
	std::atomic<std::uint64_t>		revision_{0}, applied_{0};
	std::thread						worker_;
};
