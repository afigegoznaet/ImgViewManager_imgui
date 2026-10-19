#include "AppState.hpp"
#include "DirectoryWatcher.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
namespace fs = std::filesystem;
namespace {
	void check(bool value, const char *message) {
		if (!value)
			throw std::runtime_error(message);
	}
	struct Fixture {
		fs::path path = fs::temp_directory_path()
						/ ("ivm-session-test-"
						   + std::to_string(std::chrono::steady_clock::now()
												.time_since_epoch()
												.count()));
		Fixture() { fs::create_directories(path); }
		~Fixture() {
			std::error_code ec;
			fs::remove_all(path, ec);
		}
	};
	template <class Predicate>
	void wait(Predicate predicate) {
		const auto deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline) {
			if (predicate())
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		throw std::runtime_error("watch event timed out");
	}
}
void runSessionTests() {
	Fixture	 fixture;
	AppState state;
	state.folder = fixture.path / "space and \"quotes\"\nnewline";
	state.selectedImage = state.folder / "photo.png";
	state.filter = "test\nwith \"quotes\"";
	state.expanded = {fixture.path, state.folder};
	state.recursive = false;
	state.showPreview = false;
	state.sortByPath = false;
	state.preloadPreviews = false;
	state.thumbnailSize = 192;
	state.treeWidth = 320;
	state.gridScroll = 1234;
	state.treeScroll = 45;
	state.windowWidth = 1100;
	state.windowHeight = 700;
	state.windowX = 200;
	state.windowY = 100;
	state.windowPosition = true;
	state.maximized = true;
	state.fullscreen = true;
	const auto settings = fixture.path / "preferences" / "state.conf";
	check(saveState(settings, state), "initial state save");
	check(serializeState(loadState(settings)) == serializeState(state),
		  "state roundtrip");
	state.thumbnailSize = 224;
	check(saveState(settings, state), "atomic state overwrite");
	check(loadState(settings).thumbnailSize == 224,
		  "state overwrite roundtrip");
	std::ofstream(settings)
		<< "unknown ignored\nwindow -5 999999\nthumbnailSize 99999\ngridScroll -10\n";
	const auto bounded = loadState(settings);
	check(bounded.windowWidth == 400 && bounded.windowHeight == 16384
			  && bounded.thumbnailSize == 256 && bounded.gridScroll == 0,
		  "invalid state bounds");

	const auto root = fixture.path / "images";
	fs::create_directories(root / "child");
	DirectoryWatcher watcher;
	watcher.set(root, true, {});
	wait([&] { return watcher.ready(); });
	auto setup = watcher.takeChanges();
	check(!setup.resync, "watch setup must not invalidate the tree");
	bool	   resyncSeen = false;
	const auto event = [&](const fs::path &path, bool removed) {
		wait([&] {
			auto changes = watcher.takeChanges();
			resyncSeen |= changes.resync || changes.viewResync;
			check(changes.error.empty(), "watch error");
			for (const auto &item : changes.items)
				if (item.path == path && item.removed == removed)
					return true;
			return false;
		});
	};
	const auto image = root / "child" / "created.PNG";
	std::ofstream(image) << "image";
	event(image, false);
	std::ofstream(image, std::ios::app) << "edited";
	event(image, false);
	const auto renamed = root / "child" / "renamed.jpg";
	fs::rename(image, renamed);
	bool oldSeen = false, newSeen = false;
	wait([&] {
		for (const auto &change : watcher.takeChanges().items) {
			oldSeen |= change.path == image && change.removed;
			newSeen |= change.path == renamed && !change.removed;
		}
		return oldSeen && newSeen;
	});
	fs::remove(renamed);
	event(renamed, true);
	resyncSeen = false;
	fs::create_directories(root / "new-child");
	event(root / "new-child", false);
	// Wait for the watcher to register the newly created directory.
	wait([&] {
		auto changes = watcher.takeChanges();
		resyncSeen |= changes.resync || changes.viewResync;
		return resyncSeen;
	});
	std::ofstream(root / "new-child" / "nested.png") << "new image";
	event(root / "new-child" / "nested.png", false);
	const auto other = fixture.path / "other";
	fs::create_directories(other);
	watcher.set(other, false, {});
	wait([&] { return watcher.ready(); });
	setup = watcher.takeChanges();
	check(!setup.resync, "folder navigation must not invalidate the tree");
	std::ofstream(other / "selected.jpg") << "new image";
	event(other / "selected.jpg", false);

	const auto first = other / "first";
	const auto second = other / "second";
	fs::create_directories(first);
	fs::create_directories(second);
	watcher.set(other, false, {other, first, second});
	wait([&] { return watcher.ready(); });
	watcher.takeChanges();
	fs::remove_all(first);
	fs::remove_all(second);
	// Let deletions queue, then simulate the expansion/selection change caused
	// by deletion.
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	watcher.set(other, false, {other});
	bool firstRemoved = false, secondRemoved = false;
	wait([&] {
		for (const auto &change : watcher.takeChanges().items) {
			firstRemoved |=
				change.path == first && change.removed && change.directory;
			secondRemoved |=
				change.path == second && change.removed && change.directory;
		}
		return firstRemoved && secondRemoved;
	});
}
