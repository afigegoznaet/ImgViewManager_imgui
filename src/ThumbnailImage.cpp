#include "ThumbnailImage.hpp"
#include <png.h>
#include <turbojpeg.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
	constexpr std::size_t maxBytes = 256 * 1024 * 1024;
	bool				  valid(int w, int h) {
		return w > 0 && h > 0
			   && static_cast<std::size_t>(w)
					  <= maxBytes / 4 / static_cast<std::size_t>(h);
	}
	ThumbnailImage resize(ThumbnailImage source, int edge) {
		if (std::max(source.width, source.height) <= edge)
			return source;
		const double scale =
			static_cast<double>(edge) / std::max(source.width, source.height);
		ThumbnailImage result;
		result.originalWidth = source.originalWidth;
		result.originalHeight = source.originalHeight;
		result.alpha = source.alpha;
		result.grayscale = source.grayscale;
		result.format = source.format;
		result.width =
			std::max(1, static_cast<int>(std::round(source.width * scale)));
		result.height =
			std::max(1, static_cast<int>(std::round(source.height * scale)));
		result.pixels.resize(static_cast<std::size_t>(result.width)
							 * result.height * 4);
		// Area averaging avoids aliasing when reducing large images.
		for (int y = 0; y < result.height; ++y)
			for (int x = 0; x < result.width; ++x) {
				const int x0 = x * source.width / result.width;
				const int x1 = (x + 1) * source.width / result.width;
				const int y0 = y * source.height / result.height;
				const int y1 = (y + 1) * source.height / result.height;
				double	  sum[4] = {};
				for (int sy = y0; sy < y1; ++sy)
					for (int sx = x0; sx < x1; ++sx) {
						const auto offset =
							(static_cast<std::size_t>(sy) * source.width + sx)
							* 4;
						const double alpha = source.pixels[offset + 3] / 255.0;
						for (int c = 0; c < 3; ++c)
							sum[c] += source.pixels[offset + c] * alpha;
						sum[3] += source.pixels[offset + 3];
					}
				const auto offset =
					(static_cast<std::size_t>(y) * result.width + x) * 4;
				for (int c = 0; c < 3; ++c)
					result.pixels[offset + c] =
						sum[3]
							? static_cast<unsigned char>(sum[c] * 255 / sum[3])
							: 0;
				result.pixels[offset + 3] = static_cast<unsigned char>(
					sum[3] / ((x1 - x0) * (y1 - y0)));
			}
		return result;
	}
}
ThumbnailImage decodeThumbnail(const std::filesystem::path &path, int edge) {
	ThumbnailImage result;
	try {
		if (edge <= 0 || edge > 4096)
			throw std::runtime_error("Invalid thumbnail size");
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
			throw std::runtime_error("Cannot open image");
		const auto size = file.tellg();
		if (size <= 0 || size > static_cast<std::streamoff>(maxBytes))
			throw std::runtime_error("Image file too large or empty");
		std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
		file.seekg(0);
		if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
			throw std::runtime_error("Cannot read image");
		if (bytes.size() >= 8 && png_sig_cmp(bytes.data(), 0, 8) == 0) {
			result.format = "PNG";
			png_image png{};
			png.version = PNG_IMAGE_VERSION;
			struct PngGuard {
				png_image *image;
				~PngGuard() { png_image_free(image); }
			} guard{&png};
			if (!png_image_begin_read_from_memory(&png, bytes.data(),
												  bytes.size()))
				throw std::runtime_error(png.message);
			if (png.width > 32768 || png.height > 32768
				|| !valid(png.width, png.height))
				throw std::runtime_error("Image dimensions too large");
			result.alpha = (png.format & PNG_FORMAT_FLAG_ALPHA) != 0;
			result.originalWidth = png.width;
			result.originalHeight = png.height;
			png.format = PNG_FORMAT_RGBA;
			result.width = png.width;
			result.height = png.height;
			result.pixels.resize(PNG_IMAGE_SIZE(png));
			if (!png_image_finish_read(&png, nullptr, result.pixels.data(), 0,
									   nullptr))
				throw std::runtime_error(png.message);
		} else {
			struct JpegGuard {
				tjhandle handle = tjInitDecompress();
				~JpegGuard() {
					if (handle)
						tjDestroy(handle);
				}
			} jpeg;
			if (!jpeg.handle)
				throw std::runtime_error("JPEG decoder initialization failed");
			int width, height, subsamp, colorspace;
			if (tjDecompressHeader3(jpeg.handle, bytes.data(), bytes.size(),
									&width, &height, &subsamp, &colorspace))
				throw std::runtime_error(tjGetErrorStr2(jpeg.handle));
			if (width > 32768 || height > 32768 || !valid(width, height))
				throw std::runtime_error("Image dimensions too large");
			result.format = "JPEG";
			result.originalWidth = width;
			result.originalHeight = height;
			int				count = 0;
			const auto		factors = tjGetScalingFactors(&count);
			tjscalingfactor chosen{1, 1};
			for (int i = 0; i < count; ++i) {
				const int w = TJSCALED(width, factors[i]);
				const int h = TJSCALED(height, factors[i]);
				if (std::max(w, h) >= edge
					&& factors[i].num * chosen.denom
						   < chosen.num * factors[i].denom)
					chosen = factors[i];
			}
			result.width = TJSCALED(width, chosen);
			result.height = TJSCALED(height, chosen);
			result.pixels.resize(static_cast<std::size_t>(result.width)
								 * result.height * 4);
			if (tjDecompress2(jpeg.handle, bytes.data(), bytes.size(),
							  result.pixels.data(), result.width, 0,
							  result.height, TJPF_RGBA, TJFLAG_FASTDCT))
				throw std::runtime_error(tjGetErrorStr2(jpeg.handle));
		}
		result.grayscale = true;
		for (std::size_t i = 0; i < result.pixels.size(); i += 4)
			if (result.pixels[i] != result.pixels[i + 1]
				|| result.pixels[i] != result.pixels[i + 2]) {
				result.grayscale = false;
				break;
			}
		return resize(std::move(result), edge);
	} catch (const std::exception &error) {
		result = {};
		result.error = error.what();
		return result;
	}
}
