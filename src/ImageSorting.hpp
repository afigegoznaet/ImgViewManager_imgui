#pragma once
#include "DirectoryScanner.hpp"
void sortImages(std::vector<std::size_t>				  &order,
				const std::vector<DirectoryScanner::File> &files,
				bool									   fullPath);
