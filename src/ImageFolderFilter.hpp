#pragma once
#include <functional>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

// Used only by the folder worker. Checks can yield without losing their
// progress.
class ImageFolderFilter {
public:
	struct Scan {
		explicit Scan(std::filesystem::path path) : root(std::move(path)) {}
		struct Frame {
			std::filesystem::path				path;
			std::filesystem::directory_iterator iterator;
			std::vector<std::filesystem::path>	children;
			std::size_t							next = 0;
			bool								entered = false;
		};
		std::filesystem::path root;
		std::vector<Frame>	  stack;
		std::size_t			  revision = static_cast<std::size_t>(-1);
	};
	// nullopt means more work remains; true/false are completed results.
	std::optional<bool> step(Scan &scan, const std::function<bool()> &cancelled,
							 std::size_t budget = 128);
	bool				containsImages(const std::filesystem::path &path,
									   const std::function<bool()> &cancelled);
	void				invalidate(const std::filesystem::path &path,
								   const std::function<bool()> &cancelled = {});
	void				clear() {
		cache_.clear();
		++revision_;
	}

private:
	std::map<std::filesystem::path, bool> cache_;
	std::size_t							  revision_ = 0;
};
