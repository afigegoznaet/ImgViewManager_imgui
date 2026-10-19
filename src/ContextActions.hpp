#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

std::vector<std::filesystem::path>
sourceFiles(const std::vector<std::filesystem::path> &images);
// Returns an error instead of replacing an existing destination.
std::string exportFile(const std::filesystem::path &source,
					   const std::filesystem::path &destination,
					   const std::function<bool()> &cancelled);
struct ContextInfo {
	std::size_t						   images = 0, sources = 0;
	std::uintmax_t					   imageBytes = 0, sourceBytes = 0;
	int								   width = 0, height = 0, depth = 0;
	bool							   grayscale = false, alpha = false;
	std::string						   format, error;
	std::vector<std::filesystem::path> sourcePaths;
};
struct ExportProgress {
	bool							   active = false;
	std::size_t						   completed = 0, total = 0;
	std::vector<std::string>		   errors;
	std::vector<std::filesystem::path> exported;
};
class ContextActions {
public:
	ContextActions();
	~ContextActions();
	void requestStop();
	void inspect(std::vector<std::filesystem::path> images);
	std::optional<ContextInfo> info();
	void		   exportImages(std::vector<std::filesystem::path> images,
								std::filesystem::path			   destination);
	ExportProgress progress();

private:
	struct ExportJob {
		std::vector<std::filesystem::path> images;
		std::filesystem::path			   destination;
	};
	void											  work();
	std::mutex										  mutex_;
	std::condition_variable							  wake_;
	std::atomic<bool>								  stopping_{false};
	std::atomic<std::uint64_t>						  revision_{0};
	std::optional<std::vector<std::filesystem::path>> inspect_;
	std::optional<ExportJob>						  export_;
	std::optional<ContextInfo>						  info_;
	ExportProgress									  progress_;
	std::thread										  worker_;
};
