#pragma once
#include <filesystem>
#include <set>
#include <string>
struct AppState {
	std::filesystem::path			folder;
	std::filesystem::path			selectedImage;
	std::set<std::filesystem::path> expanded;
	std::string						filter;
	float treeWidth = 280, thumbnailSize = 160, gridScroll = 0, treeScroll = 0;
	bool  recursive = true, showPreview = true, preloadPreviews = true,
		  sortByPath = true, maximized = false, fullscreen = false;
	int	  windowWidth = 1280, windowHeight = 800, windowX = 0, windowY = 0;
	bool  windowPosition = false;
};
AppState	loadState(const std::filesystem::path &path);
std::string serializeState(const AppState &state);
bool		saveState(const std::filesystem::path &path, const AppState &state);
