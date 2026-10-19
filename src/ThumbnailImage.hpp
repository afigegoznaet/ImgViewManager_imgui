#pragma once
#include <filesystem>
#include <string>
#include <vector>
struct ThumbnailImage {
	int						   width = 0, height = 0;
	int						   originalWidth = 0, originalHeight = 0;
	bool					   alpha = false, grayscale = false;
	std::string				   format;
	std::vector<unsigned char> pixels;
	std::string				   error;
};
ThumbnailImage decodeThumbnail(const std::filesystem::path &path,
							   int							edge = 256);
