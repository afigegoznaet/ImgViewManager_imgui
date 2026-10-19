#include "ContextActions.hpp"
#include "ThumbnailImage.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#ifdef __linux__
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
std::vector<fs::path> sourceFiles(const std::vector<fs::path> &images) {
	std::set<fs::path> result;
	for (const auto &image : images)
		for (const char *extension : {".psd", ".eps", ".ai", ".svg", ".orf",
									  ".PSD", ".EPS", ".AI", ".SVG", ".ORF"}) {
			auto source = image;
			source.replace_extension(extension);
			std::error_code ec;
			if (fs::is_regular_file(source, ec))
				result.insert(std::move(source));
		}
	return {result.begin(), result.end()};
}
std::string exportFile(const fs::path &source, const fs::path &destination,
					   const std::function<bool()> &cancelled) {
	if (cancelled())
		return "Export cancelled";
	std::error_code ec;
	if (!fs::is_regular_file(fs::symlink_status(source, ec)))
		return "Source is not a regular file";
	if (fs::exists(fs::symlink_status(destination, ec)))
		return "Destination already exists";
#ifdef __linux__
	struct stat input{}, output{};
	if (::stat(source.c_str(), &input)
		|| ::stat(destination.parent_path().c_str(), &output))
		return std::strerror(errno);
	if (input.st_dev == output.st_dev) {
		if (::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD,
					  destination.c_str(), RENAME_NOREPLACE)
			== 0)
			return {};
		return std::strerror(errno);
	}
#endif
	// Cross-filesystem export keeps the original, matching the Qt application.
	// Publish only a completed copy so cancellation cannot leave a partial
	// target.
	const auto temporary = fs::path(
		destination.string() + ".ivm-part-"
		+ std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()));
	FILE *in = std::fopen(source.string().c_str(), "rb");
	if (!in)
		return "Cannot read source file";
	FILE *out = std::fopen(temporary.string().c_str(), "wbx");
	if (!out) {
		std::fclose(in);
		return "Cannot create destination file";
	}
	std::vector<char> buffer(1024 * 1024);
	std::string		  error;
	while (!cancelled()) {
		const auto count = std::fread(buffer.data(), 1, buffer.size(), in);
		if (count && std::fwrite(buffer.data(), 1, count, out) != count) {
			error = "Cannot write destination file";
			break;
		}
		if (count < buffer.size()) {
			if (std::ferror(in))
				error = "Cannot read source file";
			break;
		}
	}
	if (cancelled())
		error = "Export cancelled";
	std::fclose(in);
	if (std::fclose(out) && error.empty())
		error = "Cannot finish destination file";
	if (error.empty()) {
#ifdef __linux__
		if (::syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD,
					  destination.c_str(), RENAME_NOREPLACE))
			error = std::strerror(errno);
#else
		if (!fs::copy_file(temporary, destination, fs::copy_options::none, ec))
			error = ec.message();
#endif
	}
	fs::remove(temporary, ec);
	return error;
}
ContextActions::ContextActions() : worker_(&ContextActions::work, this) {}
ContextActions::~ContextActions() {
	requestStop();
	worker_.join();
}
void ContextActions::requestStop() {
	stopping_ = true;
	wake_.notify_one();
}
void ContextActions::inspect(std::vector<fs::path> images) {
	std::lock_guard<std::mutex> lock(mutex_);
	++revision_;
	info_.reset();
	inspect_ = std::move(images);
	wake_.notify_one();
}
std::optional<ContextInfo> ContextActions::info() {
	std::lock_guard<std::mutex> lock(mutex_);
	return info_;
}
ExportProgress ContextActions::progress() {
	std::lock_guard<std::mutex> lock(mutex_);
	return progress_;
}
void ContextActions::exportImages(std::vector<fs::path> images,
								  fs::path				destination) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (progress_.active)
		return;
	progress_ = {};
	progress_.active = true;
	export_ = ExportJob{std::move(images), std::move(destination)};
	wake_.notify_one();
}
void ContextActions::work() {
	while (!stopping_) {
		std::optional<ExportJob> job;
		std::vector<fs::path>	 images;
		std::uint64_t			 revision;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wake_.wait(lock, [&] { return stopping_ || inspect_ || export_; });
			if (stopping_)
				return;
			revision = revision_;
			if (export_) {
				job = std::move(export_);
				export_.reset();
			} else {
				images = std::move(*inspect_);
				inspect_.reset();
			}
		}
		if (job) {
			auto paths = sourceFiles(job->images);
			paths.insert(paths.end(), job->images.begin(), job->images.end());
			std::sort(paths.begin(), paths.end());
			paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
			{
				std::lock_guard<std::mutex> lock(mutex_);
				progress_.total = paths.size();
			}
			for (const auto &path : paths) {
				if (stopping_)
					return;
				const auto error =
					exportFile(path, job->destination / path.filename(),
							   [&] { return stopping_.load(); });
				std::lock_guard<std::mutex> lock(mutex_);
				++progress_.completed;
				if (!error.empty())
					progress_.errors.push_back(path.u8string() + ": " + error);
				else
					progress_.exported.push_back(path);
			}
			{
				std::lock_guard<std::mutex> lock(mutex_);
				progress_.active = false;
			}
		} else {
			ContextInfo result;
			result.images = images.size();
			for (const auto &path : images) {
				if (stopping_ || revision != revision_)
					break;
				std::error_code ec;
				const auto		size = fs::file_size(path, ec);
				if (!ec)
					result.imageBytes += size;
			}
			if (stopping_ || revision != revision_)
				continue;
			const auto sources = sourceFiles(images);
			result.sources = sources.size();
			result.sourcePaths = sources;
			for (const auto &path : sources) {
				std::error_code ec;
				const auto		size = fs::file_size(path, ec);
				if (!ec)
					result.sourceBytes += size;
			}
			if (images.size() == 1 && !stopping_ && revision == revision_) {
				const auto image = decodeThumbnail(images.front());
				result.width = image.originalWidth;
				result.height = image.originalHeight;
				result.depth = 32;
				result.format = image.format;
				result.alpha = image.alpha;
				result.grayscale = image.grayscale;
				result.error = image.error;
			}
			std::lock_guard<std::mutex> lock(mutex_);
			if (!stopping_ && revision == revision_)
				info_ = std::move(result);
		}
	}
}
