#include "ContextActions.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
namespace fs = std::filesystem;
namespace {
	void check(bool value, const char *message) {
		if (!value)
			throw std::runtime_error(message);
	}
	struct Fixture {
		fs::path path = fs::temp_directory_path()
						/ ("ivm-context-test-"
						   + std::to_string(std::chrono::steady_clock::now()
												.time_since_epoch()
												.count()));
		Fixture() { fs::create_directories(path / "out"); }
		~Fixture() {
			std::error_code ec;
			fs::remove_all(path, ec);
		}
	};
	std::string text(const fs::path &path) {
		std::ifstream stream(path);
		return std::string(std::istreambuf_iterator<char>(stream), {});
	}
}
void runContextActionsTests() {
	Fixture	   fixture;
	const auto image = fixture.path / "photo.jpg",
			   source = fixture.path / "photo.PSD";
	std::ofstream(image) << "image";
	std::ofstream(source) << "source";
	check(sourceFiles({image, image}) == std::vector<fs::path>{source},
		  "source files are deduplicated and support uppercase extensions");
	const auto active = [] { return false; };
	check(!exportFile(image, fixture.path / "out" / "cancel.jpg",
					  [] { return true; })
			   .empty(),
		  "cancelled export does no work");
	check(fs::exists(image) && !fs::exists(fixture.path / "out" / "cancel.jpg"),
		  "cancellation preserves source");
	std::ofstream(fixture.path / "out" / "photo.jpg") << "existing";
	check(
		!exportFile(image, fixture.path / "out" / "photo.jpg", active).empty(),
		"export reports filename collision");
	check(text(fixture.path / "out" / "photo.jpg") == "existing"
			  && text(image) == "image",
		  "collision preserves both files");
	fs::remove(fixture.path / "out" / "photo.jpg");
	ContextActions actions;
	actions.inspect({image});
	const auto deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(5);
	std::optional<ContextInfo> info;
	while (!(info = actions.info())
		   && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	check(info && info->images == 1 && info->sources == 1
			  && info->imageBytes == 5 && info->sourceBytes == 6,
		  "menu reports image and associated source sizes");
	check(!info->error.empty(), "invalid images report metadata error");
	actions.exportImages({image}, fixture.path / "out");
	while (actions.progress().active
		   && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	const auto progress = actions.progress();
	check(!progress.active && progress.completed == 2
			  && progress.errors.empty(),
		  "background export includes source file");
	check(text(fixture.path / "out" / "photo.jpg") == "image"
			  && text(fixture.path / "out" / "photo.PSD") == "source",
		  "export preserves contents");
#ifdef __linux__
	check(!fs::exists(image) && !fs::exists(source),
		  "same-filesystem export moves images and sources like Qt");
#endif
}
