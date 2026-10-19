#include "TreeUpdates.hpp"
#include "KeyboardNavigation.hpp"
#include "ImageFolderFilter.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
namespace fs = std::filesystem;
namespace {
	struct Entry {
		fs::path	path;
		std::string name;
		bool		directory;
	};
	void check(bool value, const char *message) {
		if (!value)
			throw std::runtime_error(message);
	}
	struct Fixture {
		fs::path path = fs::temp_directory_path()
						/ ("ivm-tree-test-"
						   + std::to_string(std::chrono::steady_clock::now()
												.time_since_epoch()
												.count()));
		~Fixture() {
			std::error_code ec;
			fs::remove_all(path, ec);
		}
	};
}
void runTreeUpdateTests() {
	check(navigateIndex(-1, 12, 4, 2, NavigationKey::Right) == 0,
		  "first keyboard movement establishes current item");
	check(navigateIndex(2, 12, 4, 2, NavigationKey::Down) == 6,
		  "vertical navigation preserves column");
	check(navigateIndex(6, 12, 4, 2, NavigationKey::Up) == 2,
		  "up returns to same column");
	check(navigateIndex(2, 12, 4, 2, NavigationKey::Up) == 2,
		  "top row does not jump columns");
	check(navigateIndex(7, 10, 4, 2, NavigationKey::Down) == 9,
		  "incomplete last row clamps to last image");
	check(navigateIndex(9, 10, 4, 2, NavigationKey::Down) == 9,
		  "last row stays selected");
	check(navigateIndex(1, 12, 4, 2, NavigationKey::PageDown) == 9,
		  "page navigation moves one viewport");
	check(navigateIndex(9, 12, 4, 2, NavigationKey::Home) == 0,
		  "home selects first image");
	check(navigateIndex(0, 12, 4, 2, NavigationKey::End) == 11,
		  "end selects last image");
	check(navigateIndex(0, 0, 4, 2, NavigationKey::Right) == -1,
		  "empty grid cannot select an image");
	const std::vector<fs::path> imageOrder{"a.jpg", "b.jpg", "c.jpg", "d.jpg"};
	std::set<fs::path>			selection{"d.jpg"};
	selectRange(selection, imageOrder, 2, 0, false);
	check(selection == std::set<fs::path>{"a.jpg", "b.jpg", "c.jpg"},
		  "shift selection works backwards from anchor");
	selectRange(selection, imageOrder, 2, 1, false);
	check(selection == std::set<fs::path>{"b.jpg", "c.jpg"},
		  "shrinking shift range removes old endpoints");
	selectRange(selection, imageOrder, 3, 3, true);
	check(selection.count("b.jpg") && selection.count("d.jpg"),
		  "ctrl shift preserves existing selection");
	const Entry a{"/root/a", "a", true}, b{"/root/b", "b", true},
		c{"/root/c", "c", true};
	std::vector<Entry> rows{a, c};
	mergeTreeEntries(rows, std::vector<Entry>{b}, false);
	check(rows.size() == 3 && rows[0].path == a.path && rows[1].path == b.path
			  && rows[2].path == c.path,
		  "partial update inserts one row without discarding siblings");
	mergeTreeEntries(rows, std::vector<Entry>{b, c}, true);
	check(rows.size() == 2 && rows[0].path == b.path && rows[1].path == c.path,
		  "completed update removes only missing row");
	mergeTreeEntries(rows, std::vector<Entry>{b, c}, true);
	check(rows.size() == 2, "duplicate events must not duplicate rows");
	mergeTreeEntries(rows, std::vector<Entry>{{b.path, b.name, false}, c},
					 true);
	check(rows.size() == 2 && rows[0].path == c.path && rows[1].path == b.path
			  && !rows[1].directory,
		  "type change must move the existing path without duplication");
	mergeTreeEntries(rows, std::vector<Entry>{}, false);
	check(rows.size() == 2, "empty partial scan must not blank existing rows");
	mergeTreeEntries(rows, std::vector<Entry>{}, true);
	check(rows.empty(), "completed empty scan removes rows");
	check(pathWithin("/root/a/child", "/root/a")
			  && !pathWithin("/root/ab", "/root/a"),
		  "subtree path boundaries");
	const std::vector<fs::path> order{"/root", "/root/a", "/root/a/child",
									  "/root/b", "/root/c"};
	const auto allExist = [](const fs::path &) { return true; };
	check(folderAfterDeletion(order, "/root/a", {"/root/a"}, allExist)
			  == "/root/b",
		  "skip deleted folder descendants");
	check(folderAfterDeletion(order, "/root/a/child", {"/root/a"}, allExist)
			  == "/root/b",
		  "deleted ancestor selects next surviving row");
	check(
		folderAfterDeletion(order, "/root/a", {"/root/a", "/root/b"}, allExist)
			== "/root/c",
		"skip other deleted siblings");
	check(folderAfterDeletion(order, "/root/c", {"/root/c"}, allExist)
			  == "/root/b",
		  "last folder falls back to previous row");
	check(folderAfterDeletion(
			  order, "/root/a", {"/root/a"},
			  [](const fs::path &path) { return path != "/root/b"; })
			  == "/root/c",
		  "skip already missing rows");
	check(folderAfterDeletion(std::vector<fs::path>{"/root", "/root/a"},
							  "/root/a", {"/root/a"}, allExist)
			  == "/root",
		  "only child falls back to parent");
	Fixture	   fixture;
	const auto child = fixture.path / "parent" / "child";
	fs::create_directories(child);
	ImageFolderFilter filter;
	const auto		  active = [] { return false; };
	check(!filter.containsImages(fixture.path, active),
		  "cache negative subtree");
	std::ofstream(child / "first.png") << "test";
	filter.invalidate(child / "first.png");
	check(filter.containsImages(fixture.path, active),
		  "first image makes ancestors visible");
	fs::remove(child / "first.png");
	filter.invalidate(child / "first.png");
	check(!filter.containsImages(fixture.path, active),
		  "last image hides ancestors");
	// Unrelated cached branches remain cached when one branch changes.
	const auto other = fixture.path / "unrelated";
	fs::create_directories(other);
	check(!filter.containsImages(other, active), "cache unrelated negative");
	std::ofstream(other / "new.jpg") << "test";
	filter.invalidate(child);
	check(!filter.containsImages(other, active),
		  "unrelated cache must stay intact");
	unsigned cancellationChecks = 0;
	filter.invalidate(other, [&] {
		++cancellationChecks;
		return true;
	});
	check(cancellationChecks == 1,
		  "cancelled invalidation stops at the first cached entry");
	check(!filter.containsImages(other, active),
		  "cancelled invalidation leaves unvisited cache intact");
	filter.invalidate(other);
	check(filter.containsImages(other, active), "changed branch is rechecked");
	const auto slow = fixture.path / "large-empty";
	const auto fast = fixture.path / "quick-image";
	fs::create_directories(slow / "nested");
	fs::create_directories(fast);
	for (int i = 0; i < 80; ++i)
		std::ofstream(slow / "nested" / (std::to_string(i) + ".txt")) << "test";
	std::ofstream(fast / "image.jpg") << "test";
	ImageFolderFilter		queued;
	ImageFolderFilter::Scan slowScan(slow), fastScan(fast);
	check(!queued.step(slowScan, active, 1).has_value(),
		  "large child yields instead of blocking its siblings");
	const auto fastResult = queued.step(fastScan, active, 1);
	check(fastResult.has_value() && *fastResult,
		  "queued sibling can finish before large empty child");
	unsigned			turns = 0;
	std::optional<bool> slowResult;
	while (!(slowResult = queued.step(slowScan, active, 1)).has_value()
		   && ++turns < 200) {
	}
	check(slowResult.has_value() && !*slowResult && turns > 80,
		  "yielded scan resumes and eventually proves absence");
	ImageFolderFilter::Scan cachedFast(fast);
	const auto				cachedResult = queued.step(cachedFast, active, 0);
	check(cachedResult.has_value() && *cachedResult,
		  "cached child completes without scanning entries");
	queued.clear();
	ImageFolderFilter::Scan changedScan(slow);
	check(!queued.step(changedScan, active, 1).has_value(),
		  "start resumable scan before filesystem change");
	std::ofstream(slow / "new.png") << "test";
	queued.invalidate(slow / "new.png");
	std::optional<bool> changedResult;
	for (int i = 0;
		 i < 200
		 && !(changedResult = queued.step(changedScan, active, 1)).has_value();
		 ++i) {
	}
	check(changedResult.has_value() && *changedResult,
		  "invalidation restarts stale scan progress");
	ImageFolderFilter::Scan cancelledScan(slow);
	check(!queued.step(cancelledScan, [] { return true; }).has_value(),
		  "cancelled queued checks return immediately");
}
