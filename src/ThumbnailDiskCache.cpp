#include "ThumbnailDiskCache.hpp"
#include <QDataStream>
#include <QFile>
#include <QSaveFile>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <cstring>
struct ThumbnailDiskCache::Impl {
	std::filesystem::path directory;
	QMap<QString, QImage> images;
	bool				  dirty = false;
};
ThumbnailDiskCache::ThumbnailDiskCache() : impl_(std::make_unique<Impl>()) {}
ThumbnailDiskCache::~ThumbnailDiskCache() { flush(); }
void ThumbnailDiskCache::flush() {
	auto &c = *impl_;
	if (!c.dirty)
		return;
	QSaveFile file(
		QString::fromStdString((c.directory / ".kthumbnails").u8string()));
	if (!file.open(QIODevice::WriteOnly))
		return;
	QDataStream out(&file);
	out.setVersion(QDataStream::Qt_5_7);
	out << quint32(c.images.size());
	for (auto it = c.images.cbegin(); it != c.images.cend(); ++it)
		out << it.key() << it.value();
	if (out.status() == QDataStream::Ok && file.commit())
		c.dirty = false;
}
ThumbnailImage ThumbnailDiskCache::get(const std::filesystem::path &path,
									   int							edge) {
	auto &c = *impl_;
	if (c.directory != path.parent_path()) {
		flush();
		c.directory = path.parent_path();
		c.images.clear();
		c.dirty = false;
		QFile file(
			QString::fromStdString((c.directory / ".kthumbnails").u8string()));
		if (file.open(QIODevice::ReadOnly)) {
			QDataStream in(&file);
			in.setVersion(QDataStream::Qt_5_7);
			quint32 count = 0;
			in >> count;
			if (count <= 1000000)
				for (quint32 i = 0; i < count; ++i) {
					QString name;
					QImage	image;
					in >> name >> image;
					if (in.status() != QDataStream::Ok) {
						c.images.clear();
						break;
					}
					if (!image.isNull())
						c.images.insert(name, image);
				}
		}
	}
	const auto	   name = QString::fromStdString(path.filename().u8string());
	auto		   image = c.images.value(name);
	ThumbnailImage result;
	if (image.isNull()) {
		result = decodeThumbnail(path, 256);
		if (result.pixels.empty())
			return result;
		QImage decoded(result.pixels.data(), result.width, result.height,
					   result.width * 4, QImage::Format_RGBA8888);
		image = QImage(256, 256, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.drawImage((256 - decoded.width()) / 2,
						  (256 - decoded.height()) / 2, decoded);
		painter.end();
		c.images.insert(name, image);
		c.dirty = true;
	}
	image =
		image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation)
			.convertToFormat(QImage::Format_RGBA8888);
	result.width = image.width();
	result.height = image.height();
	result.pixels.resize(std::size_t(result.width) * result.height * 4);
	for (int y = 0; y < result.height; ++y)
		std::memcpy(result.pixels.data() + std::size_t(y) * result.width * 4,
					image.constScanLine(y), result.width * 4);
	return result;
}
