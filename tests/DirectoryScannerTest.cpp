#include "DirectoryScanner.hpp"
#include "ImageSorting.hpp"
#include "ImageFolderFilter.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool value, const char *message) {
	if (!value)
		throw std::runtime_error(message);
}
std::vector<DirectoryScanner::Update> finish(DirectoryScanner &scanner,
											 std::uint64_t	   generation) {
	std::vector<DirectoryScanner::Update> updates;
	const auto							  deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (std::chrono::steady_clock::now() < deadline) {
		for (auto &update : scanner.takeUpdates()) {
			require(update.generation == generation,
					"stale scan results published");
			const bool done = update.done;
			updates.push_back(std::move(update));
			if (done)
				return updates;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	throw std::runtime_error("scan timed out");
}
std::size_t count(const std::vector<DirectoryScanner::Update> &updates) {
	std::size_t total = 0;
	for (const auto &update : updates)
		total += update.files.size();
	return total;
}
struct Fixture {
	fs::path path =
		fs::temp_directory_path()
		/ ("ivm-scan-test-"
		   + std::to_string(
			   std::chrono::steady_clock::now().time_since_epoch().count()));
	~Fixture() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};
void runThumbnailTests();
void runSessionTests();
void runTreeUpdateTests();
void runContextActionsTests();
int	 main() {
	const std::vector<DirectoryScanner::File> sortingFiles = {
		{"/a/image10.jpg", "image10.jpg", "a/image10.jpg"},
		{"/b/image2.jpg", "image2.jpg", "b/image2.jpg"},
		{"/a/image2.jpg", "image2.jpg", "a/image2.jpg"}};
	std::vector<std::size_t> order{0, 1, 2};
	sortImages(order, sortingFiles, true);
	require(order == std::vector<std::size_t>({2, 0, 1}),
			"numeric full-path sorting");
	sortImages(order, sortingFiles, false);
	require(order == std::vector<std::size_t>({2, 1, 0}),
			"numeric filename sorting with stable ties");
	try {
		runThumbnailTests();
		runSessionTests();
		runTreeUpdateTests();
		runContextActionsTests();
		require(DirectoryScanner::matches("Vacation.JPG", "cat"),
				"substring filter");
		require(DirectoryScanner::matches("Vacation.JPG", "*.jpg"),
				"case insensitive wildcard");
		require(DirectoryScanner::matches("pic1.png", "pic?.png"),
				"single character wildcard");
		require(!DirectoryScanner::matches("pic12.png", "pic?.png"),
				"wildcard length");
		require(DirectoryScanner::matches("a.png", "*a*.p*g"),
				"wildcard backtracking");
		require(DirectoryScanner::matches("anything", ""), "empty filter");
		require(!DirectoryScanner::matches("a.png", "*.jpg"),
				"extension filter");
		Fixture fixture;
		fs::create_directories(fixture.path / "child");
		fs::create_directories(fixture.path / "empty");
		std::ofstream(fixture.path / "root.JPG") << "test";
		std::ofstream(fixture.path / "note.txt") << "test";
		std::ofstream(fixture.path / "child" / "nested.png") << "test";
		std::error_code ec;
		fs::create_directory_symlink(fixture.path,
									 fixture.path / "child" / "cycle", ec);
		ImageFolderFilter folders;
		const auto		  active = [] { return false; };
		require(folders.containsImages(fixture.path, active),
				"folder with direct images");
		require(folders.containsImages(fixture.path / "child", active),
				"image-bearing child folder");
		require(!folders.containsImages(fixture.path / "empty", active),
				"empty subtree must be hidden");
		fs::create_directories(fixture.path / "only-text");
		std::ofstream(fixture.path / "only-text" / "readme.txt") << "test";
		require(!folders.containsImages(fixture.path / "only-text", active),
				"non-image subtree must be hidden");
		fs::create_directories(fixture.path / "nested-only" / "deep");
		std::ofstream(fixture.path / "nested-only" / "deep" / "photo.JPEG")
			<< "test";
		require(folders.containsImages(fixture.path / "nested-only", active),
				"ancestor of images must be retained");
		std::ofstream(fixture.path / "empty" / "added.PNG") << "test";
		folders.clear();
		require(folders.containsImages(fixture.path / "empty", active),
				"refresh discovers new image");
		folders.clear();
		require(!folders.containsImages(fixture.path, [] { return true; }),
				"cancelled check");
		require(folders.containsImages(fixture.path, active),
				"cancellation must not cache a negative");
		fs::remove_all(fixture.path / "only-text");
		fs::remove_all(fixture.path / "nested-only");
		fs::remove(fixture.path / "empty" / "added.PNG");
		DirectoryScanner scanner;
		auto updates = finish(scanner, scanner.start(fixture.path, true, true));
		require(updates.front().first, "initial publication flag");
		require(updates.front().files.size() == 1,
				"initial root files before recursion");
		require(count(updates) == 2,
				"recursive image scan or symlink cycle handling");
		require(updates.back().directories == 3 && updates.back().errors == 0,
				"scan counters");
		bool relative = false;
		for (const auto &update : updates)
			for (const auto &file : update.files)
				relative |= file.relativePath
							== (fs::path("child") / "nested.png").u8string();
		require(relative, "relative path");
		require(count(finish(scanner, scanner.start(fixture.path, false, true)))
					== 1,
				"nonrecursive scan");
		require(count(finish(scanner, scanner.start(fixture.path, true, false)))
					== 3,
				"all file types");
		require(count(finish(scanner,
							 scanner.start(fixture.path / "empty", true, true)))
					== 0,
				"empty folder");
		updates = finish(scanner,
						 scanner.start(fixture.path / "missing", true, true));
		require(updates.back().errors == 1 && !updates.back().lastError.empty(),
				"unreadable folder error");
		for (int i = 0; i < 800; ++i)
			std::ofstream(fixture.path / ("file" + std::to_string(i) + ".png"));
		updates = finish(scanner, scanner.start(fixture.path, false, true));
		require(count(updates) == 801 && updates.size() >= 4,
				"incremental publication");
		for (int i = 0; i < 100; ++i)
			scanner.start(fixture.path, true, false);
		updates =
			finish(scanner, scanner.start(fixture.path / "empty", true, true));
		require(count(updates) == 0, "cancellation contaminated results");
		std::cout << "Directory scan/filter checks passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
