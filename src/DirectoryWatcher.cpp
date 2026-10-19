#include "DirectoryWatcher.hpp"
#include "DirectoryScanner.hpp"
#include <chrono>
#include <map>
#ifdef __linux__
#include <sys/inotify.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif
namespace fs = std::filesystem;
DirectoryWatcher::DirectoryWatcher() : worker_(&DirectoryWatcher::work, this) {}
DirectoryWatcher::~DirectoryWatcher() {
	requestStop();
	worker_.join();
}
void DirectoryWatcher::requestStop() { stopping_ = true; }
void DirectoryWatcher::set(const fs::path &folder, bool recursive,
						   const std::set<fs::path> &expanded) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (folder == folder_ && recursive == recursive_ && expanded == expanded_)
		return;
	folder_ = folder;
	recursive_ = recursive;
	expanded_ = expanded;
	// Events describe filesystem paths and remain valid across navigation/tree
	// changes. Keep queued deletions until the UI consumes them.
	++revision_;
}
DirectoryWatcher::Changes DirectoryWatcher::takeChanges() {
	std::lock_guard<std::mutex> lock(mutex_);
	Changes						result;
	std::swap(result, changes_);
	return result;
}
void DirectoryWatcher::work() {
#ifdef __linux__
	const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0) {
		std::lock_guard<std::mutex> lock(mutex_);
		changes_.error = std::strerror(errno);
		return;
	}
	struct Fd {
		int value;
		~Fd() { close(value); }
	} guard{fd};
	std::map<int, fs::path> watches;
	const auto				add = [&](const fs::path &path) {
		const int wd = inotify_add_watch(
			fd, path.c_str(),
			IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE
				| IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR
				| IN_DONT_FOLLOW);
		if (wd >= 0)
			watches[wd] = path;
		else if (errno != ENOENT && errno != ENOTDIR && errno != EACCES) {
			std::lock_guard<std::mutex> lock(mutex_);
			changes_.error = std::strerror(errno);
		}
	};
	std::uint64_t	   configured = 0;
	fs::path		   folder;
	bool			   recursive = false;
	std::set<fs::path> expanded;
	bool			   rebuild = false;
	while (!stopping_) {
		const auto revision = revision_.load();
		if (configured != revision || rebuild) {
			const bool newConfig = configured != revision;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				folder = folder_;
				recursive = recursive_;
				expanded = expanded_;
				configured = revision_;
			}
			// Add new watches before removing old ones, closing navigation
			// gaps.
			const auto		   old = watches;
			std::set<fs::path> wanted;
			const auto		   include = [&](const fs::path &path) {
				if (wanted.insert(path).second)
					add(path);
			};
			include(folder);
			include(folder.parent_path());
			for (const auto &path : expanded) {
				if (stopping_ || configured != revision_)
					break;
				include(path);
				std::error_code ec;
				for (fs::directory_iterator it(path, ec), end;
					 !ec && it != end && !stopping_ && configured == revision_;
					 it.increment(ec)) {
					std::error_code te;
					if (fs::is_directory(it->symlink_status(te)) && !te)
						include(it->path());
				}
			}
			if (recursive) {
				std::vector<fs::path> pending{folder};
				while (!pending.empty() && !stopping_
					   && configured == revision_) {
					const auto path = std::move(pending.back());
					pending.pop_back();
					include(path);
					std::error_code ec;
					for (fs::directory_iterator it(path, ec), end;
						 !ec && it != end && !stopping_
						 && configured == revision_;
						 it.increment(ec)) {
						std::error_code te;
						if (fs::is_directory(it->symlink_status(te)) && !te)
							pending.push_back(it->path());
					}
				}
			}
			for (const auto &[wd, path] : old)
				if (!wanted.count(path)) {
					inotify_rm_watch(fd, wd);
					watches.erase(wd);
				}
			if (configured == revision_) {
				applied_ = configured;
				if (newConfig || rebuild) {
					std::lock_guard<std::mutex> lock(mutex_);
					// Watch setup may miss file events, but it does not
					// invalidate the tree.
					changes_.viewResync = true;
				}
			}
			rebuild = false;
		}
		pollfd descriptor{fd, POLLIN, 0};
		if (poll(&descriptor, 1, 50) <= 0)
			continue;
		alignas(inotify_event) char buffer[64 * 1024];
		const auto					length = read(fd, buffer, sizeof(buffer));
		if (length <= 0)
			continue;
		Changes batch;
		for (std::size_t offset = 0;
			 offset < static_cast<std::size_t>(length);) {
			const auto *event =
				reinterpret_cast<const inotify_event *>(buffer + offset);
			offset += sizeof(inotify_event) + event->len;
			if (event->mask & IN_Q_OVERFLOW) {
				batch.resync = true;
				rebuild = true;
				continue;
			}
			const auto it = watches.find(event->wd);
			if (it == watches.end())
				continue;
			if (event->mask & IN_IGNORED) {
				watches.erase(it);
				continue;
			}
			const bool directory = (event->mask & IN_ISDIR) || !event->len;
			const auto path =
				event->len ? it->second / event->name : it->second;
			if (directory || DirectoryScanner::isImage(path))
				batch.items.push_back(
					{path,
					 bool(event->mask
						  & (IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF
							 | IN_MOVE_SELF)),
					 directory});
			if (directory)
				rebuild = true;
		}
		if (!batch.items.empty() || batch.resync) {
			std::lock_guard<std::mutex> lock(mutex_);
			{
				// Even events read from the previous watch configuration must
				// reach the tree.
				if (changes_.items.size() + batch.items.size() < 4096)
					for (auto &item : batch.items)
						changes_.items.push_back(std::move(item));
				else
					changes_.resync = true;
				changes_.resync |= batch.resync;
			}
		}
	}
#else
	// Portable fallback: compare directory snapshots off the UI thread.
	struct Stamp {
		fs::file_time_type time;
		std::uintmax_t	   size;
		bool			   directory;
		bool			   operator!=(const Stamp &other) const {
			return time != other.time || size != other.size
				   || directory != other.directory;
		}
	};
	std::map<fs::path, Stamp> previous;
	std::uint64_t			  configured = 0;
	while (!stopping_) {
		fs::path		   folder;
		bool			   recursive;
		std::set<fs::path> expanded;
		std::uint64_t	   revision;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			folder = folder_;
			recursive = recursive_;
			expanded = expanded_;
			revision = revision_;
		}
		std::map<fs::path, Stamp>			   current;
		std::vector<std::pair<fs::path, bool>> pending;
		pending.emplace_back(folder, recursive);
		pending.emplace_back(folder.parent_path(), false);
		for (const auto &path : expanded)
			pending.emplace_back(path, false);
		std::set<fs::path> visited;
		while (!pending.empty() && !stopping_ && revision == revision_) {
			const auto [path, descend] = pending.back();
			pending.pop_back();
			if (!visited.insert(path).second)
				continue;
			std::error_code ec;
			for (fs::directory_iterator it(path, ec), end;
				 !ec && it != end && !stopping_ && revision == revision_;
				 it.increment(ec)) {
				std::error_code te;
				const auto		status = it->symlink_status(te);
				if (te)
					continue;
				const bool directory = fs::is_directory(status);
				if (directory
					|| (fs::is_regular_file(status)
						&& DirectoryScanner::isImage(it->path()))) {
					const auto time = it->last_write_time(te);
					const auto size = directory ? 0 : it->file_size(te);
					if (!te)
						current[it->path()] = {time, size, directory};
				}
				if (directory && descend)
					pending.emplace_back(it->path(), true);
			}
		}
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (revision == revision_) {
				if (configured != revision)
					changes_.viewResync = true;
				else {
					for (const auto &[path, stamp] : previous)
						if (!current.count(path))
							changes_.items.push_back(
								{path, true, stamp.directory});
					for (const auto &[path, stamp] : current) {
						const auto it = previous.find(path);
						if (it == previous.end() || stamp != it->second)
							changes_.items.push_back(
								{path, false, stamp.directory});
					}
				}
				previous = std::move(current);
				configured = revision;
				applied_ = revision;
			}
		}
		for (int i = 0; i < 10 && !stopping_; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
#endif
}
