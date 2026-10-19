#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class DirectoryScanner {
public:
	struct File {
		std::filesystem::path path;
		std::string			  name;
		std::string			  relativePath;
	};
	struct Update {
		std::uint64_t	  generation = 0;
		std::vector<File> files;
		std::size_t		  directories = 0;
		std::size_t		  errors = 0;
		std::string		  lastError;
		bool			  first = false;
		bool			  done = false;
	};
	DirectoryScanner();
	~DirectoryScanner();
	void requestStop();
	DirectoryScanner(const DirectoryScanner &) = delete;
	DirectoryScanner  &operator=(const DirectoryScanner &) = delete;
	std::uint64_t	   start(const std::filesystem::path &root, bool recursive,
							 bool imagesOnly);
	std::deque<Update> takeUpdates();
	static bool		   isImage(const std::filesystem::path &path);
	static bool matches(const std::string &name, const std::string &filter);

private:
	struct Job {
		std::filesystem::path root;
		bool				  recursive;
		bool				  imagesOnly;
		std::uint64_t		  generation;
	};
	void					   work();
	void					   scan(const Job &job);
	std::atomic<std::uint64_t> generation_{0};
	std::atomic<bool>		   stopping_{false};
	std::mutex				   mutex_;
	std::condition_variable	   wake_;
	std::optional<Job>		   pending_;
	std::deque<Update>		   updates_;
	std::thread				   worker_;
};
