#pragma once
#include "ThumbnailImage.hpp"
#include <memory>
class ThumbnailDiskCache {
public:
	ThumbnailDiskCache();
	~ThumbnailDiskCache();
	ThumbnailImage get(const std::filesystem::path &path, int edge);
	void		   flush();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
