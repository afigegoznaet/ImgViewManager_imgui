#include "ImageFolderFilter.hpp"
#include "DirectoryScanner.hpp"
#include "TreeUpdates.hpp"
#include <chrono>

namespace fs = std::filesystem;
std::optional<bool>
ImageFolderFilter::step(Scan &scan, const std::function<bool()> &cancelled,
						std::size_t budget) {
	if (cancelled())
		return std::nullopt;
	if (scan.revision != revision_) {
		scan.stack.clear();
		scan.revision = revision_;
	}
	if (const auto found = cache_.find(scan.root); found != cache_.end())
		return found->second;
	if (scan.stack.empty())
		scan.stack.push_back({scan.root, {}, {}});
	const auto deadline =
		std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
	const auto positive = [&] {
		for (const auto &frame : scan.stack)
			cache_[frame.path] = true;
		return std::optional<bool>{true};
	};
	while (budget-- && !scan.stack.empty()) {
		if (cancelled() || std::chrono::steady_clock::now() >= deadline)
			return std::nullopt;
		auto		   &frame = scan.stack.back();
		std::error_code ec;
		if (!frame.entered) {
			frame.iterator = fs::directory_iterator(frame.path, ec);
			frame.entered = true;
		}
		if (frame.iterator != fs::directory_iterator{}) {
			const auto entry = *frame.iterator;
			const auto status = entry.symlink_status(ec);
			if (!ec && fs::is_regular_file(status)
				&& DirectoryScanner::isImage(entry.path()))
				return positive();
			if (!ec && fs::is_directory(status))
				frame.children.push_back(entry.path());
			frame.iterator.increment(ec);
			if (ec)
				frame.iterator = {};
			continue;
		}
		if (frame.next < frame.children.size()) {
			const auto child = frame.children[frame.next++];
			if (const auto found = cache_.find(child); found != cache_.end()) {
				if (found->second)
					return positive();
			} else
				scan.stack.push_back({child, {}, {}});
		} else {
			cache_[frame.path] = false;
			scan.stack.pop_back();
		}
	}
	if (scan.stack.empty())
		return false;
	return std::nullopt;
}

bool ImageFolderFilter::containsImages(const fs::path			   &path,
									   const std::function<bool()> &cancelled) {
	Scan scan(path);
	while (!cancelled())
		if (const auto result = step(scan, cancelled))
			return *result;
	return false;
}

void ImageFolderFilter::invalidate(const fs::path			   &path,
								   const std::function<bool()> &cancelled) {
	if (cancelled && cancelled())
		return;
	++revision_;
	// Cached keys are directory paths. A file change only invalidates its
	// ancestors; a directory change also invalidates the contiguous range below
	// that directory.
	auto it = cache_.lower_bound(path);
	while (it != cache_.end() && pathWithin(it->first, path)) {
		if (cancelled && cancelled())
			return;
		it = cache_.erase(it);
	}
	for (auto parent = path.parent_path(); !parent.empty();) {
		if (cancelled && cancelled())
			return;
		cache_.erase(parent);
		const auto next = parent.parent_path();
		if (next == parent)
			break;
		parent = next;
	}
}
