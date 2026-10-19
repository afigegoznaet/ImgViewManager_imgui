#include "ThumbnailImage.hpp"
#include "ThumbnailDiskCache.hpp"
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <png.h>
#include <turbojpeg.h>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace {
	void check(bool value, const char *message) {
		if (!value)
			throw std::runtime_error(message);
	}
	struct Fixture {
		std::filesystem::path path =
			std::filesystem::temp_directory_path()
			/ ("ivm-thumb-test-"
			   + std::to_string(std::chrono::steady_clock::now()
									.time_since_epoch()
									.count()));
		Fixture() { std::filesystem::create_directories(path); }
		~Fixture() {
			std::error_code ec;
			std::filesystem::remove_all(path, ec);
		}
	};
	void png(const std::filesystem::path &path, int width, int height) {
		png_image image{};
		image.version = PNG_IMAGE_VERSION;
		image.width = width;
		image.height = height;
		image.format = PNG_FORMAT_RGBA;
		std::vector<unsigned char> pixels(static_cast<std::size_t>(width)
										  * height * 4);
		for (std::size_t i = 0; i < pixels.size(); i += 4) {
			pixels[i] = 255;
			pixels[i + 3] = 128;
		}
		check(png_image_write_to_file(&image, path.string().c_str(), 0,
									  pixels.data(), 0, nullptr),
			  "PNG fixture write");
	}
}
void runThumbnailTests() {
	Fixture fixture;
	png(fixture.path / "alpha.png", 480, 240);
	auto image = decodeThumbnail(fixture.path / "alpha.png");
	check(image.error.empty() && image.width == 256 && image.height == 128,
		  "PNG thumbnail dimensions");
	check(image.originalWidth == 480 && image.originalHeight == 240
			  && image.format == "PNG" && image.alpha && !image.grayscale,
		  "PNG menu metadata survives resizing");
	check(image.pixels[0] == 255 && image.pixels[3] == 128,
		  "PNG alpha preserved");
	png(fixture.path / "small.png", 17, 9);
	image = decodeThumbnail(fixture.path / "small.png");
	check(image.width == 17 && image.height == 9,
		  "small PNG must not be upscaled");
	{
		ThumbnailDiskCache disk;
		check(disk.get(fixture.path / "alpha.png", 160).width == 160,
			  "disk thumbnail resize");
		disk.flush();
		QFile cache(
			QString::fromStdString((fixture.path / ".kthumbnails").string()));
		check(cache.open(QIODevice::ReadOnly), "Qt cache open");
		QDataStream stream(&cache);
		stream.setVersion(QDataStream::Qt_5_7);
		quint32 count;
		QString name;
		QImage	stored;
		stream >> count >> name >> stored;
		check(count == 1 && name == "alpha.png"
				  && stored.size() == QSize(256, 256),
			  "Qt cache format");
	}
	std::filesystem::remove(fixture.path / "alpha.png");
	{
		ThumbnailDiskCache restored;
		auto cached = restored.get(fixture.path / "alpha.png", 256);
		check(cached.error.empty() && cached.width == 256
				  && !cached.pixels.empty(),
			  "restore without source decoding");
	}
	const auto jpeg = tjInitCompress();
	check(jpeg != nullptr, "JPEG fixture compressor");
	std::vector<unsigned char> pixels(2048 * 1024 * 3, 128);
	unsigned char			  *bytes = nullptr;
	unsigned long			   size = 0;
	const int status = tjCompress2(jpeg, pixels.data(), 2048, 0, 1024, TJPF_RGB,
								   &bytes, &size, TJSAMP_420, 90, 0);
	tjDestroy(jpeg);
	check(status == 0, "JPEG fixture write");
	std::ofstream(fixture.path / "wide.jpg", std::ios::binary)
		.write(reinterpret_cast<char *>(bytes), size);
	tjFree(bytes);
	image = decodeThumbnail(fixture.path / "wide.jpg");
	check(image.error.empty() && image.width == 256 && image.height == 128,
		  "scaled JPEG dimensions");
	check(image.originalWidth == 2048 && image.originalHeight == 1024
			  && image.format == "JPEG" && !image.alpha && image.grayscale,
		  "JPEG menu metadata uses original dimensions");
	check(image.pixels[3] == 255 && image.pixels[0] >= 126
			  && image.pixels[0] <= 130,
		  "JPEG pixel data");
	image = decodeThumbnail(fixture.path / "wide.jpg", 1024);
	check(image.error.empty() && image.width == 1024 && image.height == 512,
		  "large preview resolution");
	std::ofstream(fixture.path / "broken.png") << "invalid";
	check(!decodeThumbnail(fixture.path / "broken.png").error.empty(),
		  "invalid image error");
	check(!decodeThumbnail(fixture.path / "missing.jpg").error.empty(),
		  "missing image error");
	check(!decodeThumbnail(fixture.path / "alpha.png", 0).error.empty(),
		  "invalid size error");
}
