#include "DirectoryScanner.hpp"
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;
namespace {
	std::string lower(std::string value) {
		// ASCII fold keeps UTF-8 bytes intact.
		for (char &c : value)
			if (c >= 'A' && c <= 'Z')
				c += 'a' - 'A';
		return value;
	}
}
bool DirectoryScanner::isImage(const fs::path &path) {
	const auto ext = lower(path.extension().u8string());
	return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

DirectoryScanner::DirectoryScanner() : worker_(&DirectoryScanner::work, this) {}
DirectoryScanner::~DirectoryScanner() {
	requestStop();
	worker_.join();
}
void DirectoryScanner::requestStop() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = true;
		++generation_;
	}
	wake_.notify_one();
}
std::uint64_t DirectoryScanner::start(const fs::path &root, bool recursive,
									  bool imagesOnly) {
	std::lock_guard<std::mutex> lock(mutex_);
	const auto					generation = ++generation_;
	pending_ = Job{root, recursive, imagesOnly, generation};
	updates_.clear();
	wake_.notify_one();
	return generation;
}
std::deque<DirectoryScanner::Update> DirectoryScanner::takeUpdates() {
	std::lock_guard<std::mutex> lock(mutex_);
	std::deque<Update>			result;
	result.swap(updates_);
	return result;
}
bool DirectoryScanner::matches(const std::string &name,
							   const std::string &filter) {
	const auto text = lower(name);
	const auto query = lower(filter);
	if (query.find_first_of("*?") == std::string::npos)
		return text.find(query) != std::string::npos;
	// Wildcards match the complete filename; plain text matches a substring.
	std::size_t t = 0, p = 0, star = std::string::npos, retry = 0;
	while (t < text.size()) {
		if (p < query.size() && (query[p] == '?' || query[p] == text[t])) {
			++p;
			++t;
		} else if (p < query.size() && query[p] == '*') {
			star = p++;
			retry = t;
		} else if (star != std::string::npos) {
			p = star + 1;
			t = ++retry;
		} else
			return false;
	}
	while (p < query.size() && query[p] == '*')
		++p;
	return p == query.size();
}
void DirectoryScanner::work() {
	for (;;) {
		Job job;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wake_.wait(lock,
					   [this] { return stopping_ || pending_.has_value(); });
			if (stopping_)
				return;
			job = std::move(*pending_);
			pending_.reset();
		}
		scan(job);
	}
}
void DirectoryScanner::scan(const Job &job) {
	const auto cancelled = [&] {
		return stopping_ || generation_ != job.generation;
	};
	Update update;
	update.generation = job.generation;
	update.first = true;
	auto	   published = std::chrono::steady_clock::now();
	const auto publish = [&](bool done) {
		update.done = done;
		std::lock_guard<std::mutex> lock(mutex_);
		if (cancelled())
			return;
		updates_.push_back(std::move(update));
		const auto directories = updates_.back().directories;
		const auto errors = updates_.back().errors;
		const auto error = updates_.back().lastError;
		update = Update{};
		update.generation = job.generation;
		update.directories = directories;
		update.errors = errors;
		update.lastError = error;
		published = std::chrono::steady_clock::now();
	};
	const auto report = [&](const fs::path		  &path,
							const std::error_code &error) {
		++update.errors;
		update.lastError = path.u8string() + ": " + error.message();
	};
	std::vector<fs::path> stack{job.root};
	while (!stack.empty() && !cancelled()) {
		const auto directory = std::move(stack.back());
		stack.pop_back();
		++update.directories;
		std::vector<fs::path>  children;
		std::error_code		   ec;
		fs::directory_iterator it(directory, ec), end;
		if (ec)
			report(directory, ec);
		while (!ec && it != end) {
			if (cancelled())
				return;
			std::error_code typeError;
			const auto		status = it->symlink_status(typeError);
			if (typeError)
				report(it->path(), typeError);
			else if (fs::is_directory(status)) {
				if (job.recursive)
					children.push_back(it->path());
			} else if (fs::is_regular_file(status)
					   && (!job.imagesOnly || isImage(it->path()))) {
				update.files.push_back(
					{it->path(), it->path().filename().u8string(),
					 it->path().lexically_relative(job.root).u8string()});
			}
			if (update.files.size() >= 256
				|| std::chrono::steady_clock::now() - published
					   > std::chrono::milliseconds(50))
				publish(false);
			it.increment(ec);
			if (ec)
				report(directory, ec);
		}
		// Publish the selected directory before descending into its children.
		if (directory == job.root)
			publish(false);
		std::sort(children.rbegin(), children.rend());
		for (auto &child : children)
			stack.push_back(std::move(child));
	}
	if (!cancelled())
		publish(true);
}
