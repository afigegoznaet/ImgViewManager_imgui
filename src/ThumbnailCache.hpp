#pragma once
#include "ThumbnailImage.hpp"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <set>

class ThumbnailCache {
public:
	struct Texture {
		unsigned	  id = 0;
		int			  width = 0, height = 0;
		std::string	  error;
		std::uint64_t used = 0;
		bool		  pending = false;
	};
	explicit ThumbnailCache(int edge = 256, std::size_t capacity = 512,
							std::size_t budgetBytes = 128 * 1024 * 1024,
							std::size_t resultCapacity = 32,
							bool retainJobs = false, bool diskCache = true);
	~ThumbnailCache();
	void requestStop();
	ThumbnailCache(const ThumbnailCache &) = delete;
	ThumbnailCache &operator=(const ThumbnailCache &) = delete;
	void			beginFrame();
	void			invalidate();
	void			setEdge(int edge);
	std::size_t		preloadSlots() const;
	struct PreloadProgress {
		std::size_t total = 0, ready = 0, failed = 0;
	};
	PreloadProgress preload(const std::vector<std::filesystem::path> &paths);
	void			remove(const std::filesystem::path &path);
	const Texture &get(const std::filesystem::path &path, bool priority = true);

private:
	struct Job {
		std::filesystem::path path;
		std::uint64_t		  generation;
		int					  edge;
	};
	struct Result {
		Job			   job;
		ThumbnailImage image;
	};
	void			  work();
	void			  clearTextures();
	const Texture	 &request(const std::filesystem::path &path, bool priority);
	int				  edge_;
	const std::size_t capacity_;
	const std::size_t budgetBytes_;
	const std::size_t resultCapacity_;
	const bool		  retainJobs_;
	const bool		  diskCache_;
	std::set<std::filesystem::path>			 preloaded_, preloadFailed_;
	std::map<std::filesystem::path, Texture> textures_;
	std::uint64_t							 frame_ = 0;
	bool									 clear_ = false;
	std::vector<std::filesystem::path>		 remove_;
	std::mutex								 mutex_;
	std::condition_variable					 wake_;
	std::deque<Job>							 jobs_;
	std::deque<Result>						 results_;
	std::uint64_t							 generation_ = 0;
	bool									 stopping_ = false;
	std::thread								 worker_;
};
