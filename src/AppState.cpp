#include "AppState.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

AppState loadState(const std::filesystem::path &path) {
	AppState	  state;
	std::ifstream input(path);
	std::string	  line;
	std::string	  key;
	while (input >> key) {
		auto	   &row = input;
		std::string value;
		if (key == "folder" && row >> std::quoted(value))
			state.folder = std::filesystem::u8path(value);
		else if (key == "selected" && row >> std::quoted(value))
			state.selectedImage = std::filesystem::u8path(value);
		else if (key == "expanded" && row >> std::quoted(value))
			state.expanded.insert(std::filesystem::u8path(value));
		else if (key == "filter")
			row >> std::quoted(state.filter);
		else if (key == "treeWidth")
			row >> state.treeWidth;
		else if (key == "thumbnailSize")
			row >> state.thumbnailSize;
		else if (key == "gridScroll")
			row >> state.gridScroll;
		else if (key == "treeScroll")
			row >> state.treeScroll;
		else if (key == "recursive")
			row >> state.recursive;
		else if (key == "preview")
			row >> state.showPreview;
		else if (key == "sortByPath")
			row >> state.sortByPath;
		else if (key == "preload")
			row >> state.preloadPreviews;
		else if (key == "window")
			row >> state.windowWidth >> state.windowHeight;
		else if (key == "position")
			row >> state.windowX >> state.windowY >> state.windowPosition;
		else if (key == "maximized")
			row >> state.maximized;
		else if (key == "fullscreen")
			row >> state.fullscreen;
		else
			std::getline(input, line);
		if (input.fail() && !input.eof()) {
			input.clear();
			std::getline(input, line);
		}
	}
	const auto bound = [](float value, float fallback, float min, float max) {
		return std::isfinite(value) ? std::clamp(value, min, max) : fallback;
	};
	state.treeWidth = bound(state.treeWidth, 280, 120, 4000);
	state.thumbnailSize = bound(state.thumbnailSize, 160, 96, 256);
	state.gridScroll = bound(state.gridScroll, 0, 0, 1e8);
	state.treeScroll = bound(state.treeScroll, 0, 0, 1e8);
	state.windowWidth = std::clamp(state.windowWidth, 400, 16384);
	state.windowHeight = std::clamp(state.windowHeight, 300, 16384);
	return state;
}
std::string serializeState(const AppState &s) {
	std::ostringstream out;
	out << "version 1\nfolder " << std::quoted(s.folder.u8string())
		<< "\nselected " << std::quoted(s.selectedImage.u8string())
		<< "\nfilter " << std::quoted(s.filter) << "\ntreeWidth " << s.treeWidth
		<< "\nthumbnailSize " << s.thumbnailSize << "\ngridScroll "
		<< s.gridScroll << "\ntreeScroll " << s.treeScroll << "\nrecursive "
		<< s.recursive << "\npreview " << s.showPreview << "\nsortByPath "
		<< s.sortByPath << "\npreload " << s.preloadPreviews << "\nwindow "
		<< s.windowWidth << ' ' << s.windowHeight << "\nposition " << s.windowX
		<< ' ' << s.windowY << ' ' << s.windowPosition << "\nmaximized "
		<< s.maximized << "\nfullscreen " << s.fullscreen << '\n';
	for (const auto &path : s.expanded)
		out << "expanded " << std::quoted(path.u8string()) << '\n';
	return out.str();
}
bool saveState(const std::filesystem::path &path, const AppState &state) {
	if (path.empty())
		return false;
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec)
		return false;
	auto temp = path;
	temp += ".tmp";
	std::ofstream output(temp, std::ios::binary | std::ios::trunc);
	output << serializeState(state);
	output.close();
	if (!output)
		return false;
	std::filesystem::rename(temp, path, ec);
	return !ec;
}
