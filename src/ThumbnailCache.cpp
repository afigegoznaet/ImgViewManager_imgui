#include "ThumbnailCache.hpp"
#include "ThumbnailDiskCache.hpp"
#include <SDL3/SDL_opengl.h>
#include <algorithm>

ThumbnailCache::ThumbnailCache(int edge, std::size_t capacity,
							   std::size_t budgetBytes,
							   std::size_t resultCapacity, bool retainJobs,
							   bool diskCache)
	: edge_(edge), capacity_(capacity), budgetBytes_(budgetBytes),
	  resultCapacity_(resultCapacity), retainJobs_(retainJobs),
	  diskCache_(diskCache), worker_(&ThumbnailCache::work, this) {}
ThumbnailCache::~ThumbnailCache() {
	requestStop();
	worker_.join();
	clearTextures();
}
void ThumbnailCache::requestStop() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = true;
		jobs_.clear();
	}
	wake_.notify_one();
}
void ThumbnailCache::clearTextures() {
	for (auto &[path, texture] : textures_)
		if (texture.id)
			glDeleteTextures(1, &texture.id);
	textures_.clear();
}
void ThumbnailCache::invalidate() {
	std::lock_guard<std::mutex> lock(mutex_);
	++generation_;
	jobs_.clear();
	results_.clear();
	preloaded_.clear();
	preloadFailed_.clear();
	clear_ = true; // Delete on the next frame, after current draw commands have
				   // rendered.
	wake_.notify_one();
}
void ThumbnailCache::beginFrame() {
	++frame_;
	if (clear_) {
		clearTextures();
		clear_ = false;
	}
	for (const auto &path : remove_) {
		const auto it = textures_.find(path);
		if (it != textures_.end()) {
			if (it->second.id)
				glDeleteTextures(1, &it->second.id);
			textures_.erase(it);
		}
	}
	remove_.clear();
	std::deque<Result> ready;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		// Thumbnail jobs follow visible cells; full-folder preview jobs survive
		// scrolling.
		if (!retainJobs_) {
			for (const auto &job : jobs_) {
				const auto it = textures_.find(job.path);
				if (it != textures_.end())
					it->second.pending = false;
			}
			jobs_.clear();
		}
		for (int i = 0; i < (edge_ > 256 ? 2 : 8) && !results_.empty(); ++i) {
			ready.push_back(std::move(results_.front()));
			results_.pop_front();
		}
	}
	wake_.notify_one();
	for (auto &result : ready) {
		if (result.job.generation != generation_)
			continue;
		auto &texture = textures_[result.job.path];
		texture.pending = false;
		preloaded_.insert(result.job.path);
		if (!result.image.error.empty())
			preloadFailed_.insert(result.job.path);
		texture.error = std::move(result.image.error);
		texture.width = result.image.width;
		texture.height = result.image.height;
		if (result.image.pixels.empty())
			continue;
		GLint binding = 0, alignment = 0, rowLength = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
		glGenTextures(1, &texture.id);
		glBindTexture(GL_TEXTURE_2D, texture.id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height,
					 0, GL_RGBA, GL_UNSIGNED_BYTE, result.image.pixels.data());
		glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
		glBindTexture(GL_TEXTURE_2D, binding);
	}
	std::size_t bytes = 0;
	for (const auto &[path, texture] : textures_)
		if (texture.id)
			bytes +=
				static_cast<std::size_t>(texture.width) * texture.height * 4;
	while (textures_.size() > capacity_ || bytes > budgetBytes_) {
		auto oldest = textures_.end();
		for (auto it = textures_.begin(); it != textures_.end(); ++it)
			if (!it->second.pending
				&& (oldest == textures_.end()
					|| it->second.used < oldest->second.used))
				oldest = it;
		if (oldest == textures_.end())
			break;
		if (oldest->second.id) {
			bytes -= static_cast<std::size_t>(oldest->second.width)
					 * oldest->second.height * 4;
			glDeleteTextures(1, &oldest->second.id);
		}
		textures_.erase(oldest);
	}
}
const ThumbnailCache::Texture &
ThumbnailCache::get(const std::filesystem::path &path, bool priority) {
	return request(path, priority);
}
const ThumbnailCache::Texture &
ThumbnailCache::request(const std::filesystem::path &path, bool priority) {
	auto &texture = textures_[path];
	texture.used =
		std::max(texture.used, priority ? frame_ : (frame_ ? frame_ - 1 : 0));
	if (priority && texture.pending) {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto					it =
			std::find_if(jobs_.begin(), jobs_.end(),
						 [&](const auto &job) { return job.path == path; });
		if (it != jobs_.end())
			std::rotate(jobs_.begin(), it, std::next(it));
	}
	if (!texture.id && texture.error.empty() && !texture.pending && !clear_) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (priority || jobs_.size() < 128) {
			texture.pending = true;
			if (priority)
				jobs_.push_front({path, generation_, edge_});
			else
				jobs_.push_back({path, generation_, edge_});
			wake_.notify_one();
		}
	}
	return texture;
}
void ThumbnailCache::work() {
	ThumbnailDiskCache disk;
	for (;;) {
		Job job;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wake_.wait(lock, [this] {
				return stopping_
					   || (!jobs_.empty() && results_.size() < resultCapacity_);
			});
			if (stopping_)
				return;
			job = std::move(jobs_.front());
			jobs_.pop_front();
		}
		auto image = diskCache_ ? disk.get(job.path, job.edge)
								: decodeThumbnail(job.path, job.edge);
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (job.generation == generation_)
				results_.push_back({std::move(job), std::move(image)});
		}
		bool idle;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			idle = jobs_.empty();
		}
		if (idle)
			disk.flush();
	}
}

void ThumbnailCache::remove(const std::filesystem::path &path) {
	remove_.push_back(path);
	preloaded_.erase(path);
	preloadFailed_.erase(path);
	std::lock_guard<std::mutex> lock(mutex_);
	++generation_;
	jobs_.clear();
	results_.clear();
	for (auto &[key, texture] : textures_)
		texture.pending = false;
	wake_.notify_one();
}

void ThumbnailCache::setEdge(int edge) {
	edge = std::clamp(edge, 96, 2048);
	if (edge == edge_)
		return;
	edge_ = edge;
	invalidate();
}
std::size_t ThumbnailCache::preloadSlots() const {
	const auto worstCaseBytes = static_cast<std::size_t>(edge_) * edge_ * 4;
	const auto total = std::min(capacity_, budgetBytes_ / worstCaseBytes);
	// Reserve one slot for a hover outside the preload window.
	return total > 1 ? total - 1 : 0;
}
ThumbnailCache::PreloadProgress
ThumbnailCache::preload(const std::vector<std::filesystem::path> &paths) {
	PreloadProgress progress{paths.size()};
	for (const auto &path : paths) {
		if (preloaded_.count(path)) {
			if (preloadFailed_.count(path))
				++progress.failed;
			else
				++progress.ready;
		} else
			request(path, false);
	}
	return progress;
}
