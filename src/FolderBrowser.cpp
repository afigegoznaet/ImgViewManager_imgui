#include <chrono>
#include "FolderBrowser.hpp"
#include "ImageSorting.hpp"
#include "imgui.h"
#include "ImageFolderFilter.hpp"
#include "TreeUpdates.hpp"
#include "KeyboardNavigation.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cfloat>
#include <cstdio>

namespace fs = std::filesystem;

namespace {
	NavigationKey navigationKey() {
		const std::pair<ImGuiKey, NavigationKey> keys[] = {
			{ImGuiKey_LeftArrow, NavigationKey::Left},
			{ImGuiKey_RightArrow, NavigationKey::Right},
			{ImGuiKey_UpArrow, NavigationKey::Up},
			{ImGuiKey_DownArrow, NavigationKey::Down},
			{ImGuiKey_Home, NavigationKey::Home},
			{ImGuiKey_End, NavigationKey::End},
			{ImGuiKey_PageUp, NavigationKey::PageUp},
			{ImGuiKey_PageDown, NavigationKey::PageDown}};
		for (const auto &[key, action] : keys)
			if (ImGui::IsKeyPressed(key))
				return action;
		return NavigationKey::None;
	}
	void openFile(const fs::path &path) {
		// Encode UTF-8 bytes so spaces, percent signs, hashes and non-ASCII
		// paths survive URLs.
		const auto	bytes = path.u8string();
		const char	hex[] = "0123456789ABCDEF";
		std::string url = "file://";
		for (unsigned char c : bytes) {
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_'
				|| c == '.' || c == '~')
				url += c;
			else {
				url += '%';
				url += hex[c >> 4];
				url += hex[c & 15];
			}
		}
		if (!SDL_OpenURL(url.c_str()))
			SDL_Log("Could not open file: %s", SDL_GetError());
	}
}

void FolderBrowser::beginContext(const fs::path &pointed) {
	contextPointed_ = pointed;
	contextSelection_.clear();
	for (auto i : visibleFiles_)
		if (selectedImages_.count(files_[i].path))
			contextSelection_.push_back(files_[i].path);
	contextImages_ = contextSelection_;
	if (contextImages_.size() <= 1 && !pointed.empty())
		contextImages_ = {pointed};
	contextActions_.inspect(contextImages_);
}

void FolderBrowser::drawContextMenu() {
	const auto info = contextActions_.info();
	if (ImGui::MenuItem("Open image", "Ctrl+O", false,
						!contextPointed_.empty()))
		openFile(contextPointed_);
	const bool exporting = contextActions_.progress().active || exportDialog_;
	if (ImGui::MenuItem("Export image", "Ctrl+I", false,
						!contextSelection_.empty() && !exporting))
		requestExport(contextSelection_);
	if (ImGui::MenuItem("Open source file", "Ctrl+S", false,
						info && info->sources != 0))
		for (const auto &source : info->sourcePaths)
			openFile(source);
	ImGui::Separator();
	if (!info) {
		ImGui::TextDisabled("Reading file information...");
		return;
	}
	const auto sizeText = [](std::uintmax_t bytes) {
		char text[64];
		if (bytes > 500000000)
			std::snprintf(text, sizeof(text), "%.1f GB", bytes / 1000000000.0);
		else if (bytes > 500000)
			std::snprintf(text, sizeof(text), "%.1f MB", bytes / 1000000.0);
		else if (bytes > 500)
			std::snprintf(text, sizeof(text), "%.1f KB", bytes / 1000.0);
		else
			std::snprintf(text, sizeof(text), "%ju B", bytes);
		return std::string(text);
	};
	if (info->images > 1) {
		ImGui::TextDisabled("Selected files: %zu", info->images);
		ImGui::TextDisabled("Selected files size: %s",
							sizeText(info->imageBytes).c_str());
		ImGui::TextDisabled("Hidden source files: %zu", info->sources);
		ImGui::TextDisabled("Hidden source files size: %s",
							sizeText(info->sourceBytes).c_str());
		ImGui::TextDisabled("Total files: %zu", info->images + info->sources);
		ImGui::TextDisabled(
			"Total files size: %s",
			sizeText(info->imageBytes + info->sourceBytes).c_str());
	} else {
		ImGui::TextDisabled("File size: %s",
							sizeText(info->imageBytes).c_str());
		ImGui::TextDisabled("File format: %s", info->format.c_str());
		ImGui::TextDisabled("Color depth: %d bpp", info->depth);
		ImGui::TextDisabled("Grayscale: %s",
							info->grayscale ? "true" : "false");
		ImGui::TextDisabled("Image size: %d x %d", info->width, info->height);
		ImGui::TextDisabled("Transparency: %s", info->alpha ? "true" : "false");
		if (!info->error.empty())
			ImGui::TextWrapped("%s", info->error.c_str());
	}
}

void FolderBrowser::requestExport(const std::vector<fs::path> &images) {
	if (images.empty() || exportDialog_ || contextActions_.progress().active)
		return;
	exportDialog_ = std::make_shared<ExportDialog>();
	exportDialog_->images = images;
	exportDialog_->origin = selected_;
	auto	  *callbackState = new std::shared_ptr<ExportDialog>(exportDialog_);
	const auto callback = [](void *userdata, const char *const *paths, int) {
		std::unique_ptr<std::shared_ptr<ExportDialog>> owner(
			static_cast<std::shared_ptr<ExportDialog> *>(userdata));
		const auto					state = *owner;
		std::lock_guard<std::mutex> lock(state->mutex);
		if (!paths)
			state->error = SDL_GetError();
		else if (paths[0])
			state->destination = fs::u8path(paths[0]);
		state->done = true;
	};
	const auto initial = exportDirectory_.empty() ? selected_.u8string()
												  : exportDirectory_.u8string();
	SDL_ShowOpenFolderDialog(callback, callbackState, SDL_GetKeyboardFocus(),
							 initial.c_str(), false);
}

void FolderBrowser::drawExport() {
	if (exportDialog_) {
		std::unique_lock<std::mutex> lock(exportDialog_->mutex);
		if (exportDialog_->done) {
			auto state = exportDialog_;
			if (!state->error.empty())
				exportError_ = state->error;
			else if (!state->destination.empty()) {
				std::error_code ec;
				if (fs::equivalent(state->destination, state->origin, ec))
					exportError_ =
						"Output directory cannot be the current folder.";
				else if (!fs::is_directory(state->destination, ec))
					exportError_ = "Choose an existing output folder.";
				else {
					exportDirectory_ = state->destination;
					contextActions_.exportImages(state->images,
												 state->destination);
					exportProgressOpen_ = true;
					exportApplied_ = false;
				}
			}
			lock.unlock();
			exportDialog_.reset();
		}
	}
	if (!exportError_.empty()) {
		ImGui::OpenPopup("Export error");
	}
	if (ImGui::BeginPopupModal("Export error", nullptr,
							   ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("%s", exportError_.c_str());
		if (ImGui::Button("OK")) {
			exportError_.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (exportProgressOpen_)
		ImGui::OpenPopup("Export images");
	if (ImGui::BeginPopupModal("Export images", nullptr,
							   ImGuiWindowFlags_AlwaysAutoResize)) {
		const auto progress = contextActions_.progress();
		ImGui::Text("%zu of %zu files", progress.completed, progress.total);
		ImGui::ProgressBar(progress.total
							   ? static_cast<float>(progress.completed)
									 / progress.total
							   : 0,
						   ImVec2(450, 0));
		for (const auto &error : progress.errors)
			ImGui::TextWrapped("%s", error.c_str());
		if (!progress.active) {
			if (!exportApplied_) {
				exportedImages_.insert(progress.exported.begin(),
									   progress.exported.end());
				updateFilter();
				exportApplied_ = true;
			}
			ImGui::TextUnformatted(
				progress.errors.empty()
					? "Export complete."
					: "Export finished with errors; failed originals were kept.");
			if (ImGui::Button("Close")) {
				exportProgressOpen_ = false;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}
}

void FolderBrowser::selectImage(int index, bool range, bool additive,
								bool toggle) {
	if (index < 0 || index >= static_cast<int>(visibleFiles_.size()))
		return;
	const auto path = files_[visibleFiles_[index]].path;
	if (range) {
		std::vector<fs::path> order;
		int					  anchor = index;
		for (int i = 0; i < static_cast<int>(visibleFiles_.size()); ++i) {
			order.push_back(files_[visibleFiles_[i]].path);
			if (order.back() == imageAnchor_)
				anchor = i;
		}
		selectRange(selectedImages_, order, anchor, index, additive);
	} else {
		if (!additive)
			selectedImages_.clear();
		if (toggle && selectedImages_.count(path))
			selectedImages_.erase(path);
		else
			selectedImages_.insert(path);
		imageAnchor_ = path;
	}
	selectedImage_ = path;
}

void FolderBrowser::openImages(bool source) {
	std::vector<fs::path> images;
	for (auto index : visibleFiles_)
		if (selectedImages_.count(files_[index].path))
			images.push_back(files_[index].path);
	if (source)
		images = sourceFiles(images);
	for (const auto &path : images)
		openFile(path);
}

int FolderBrowser::typeAhead(const std::vector<fs::path> &order, int current) {
	const auto &io = ImGui::GetIO();
	if (order.empty() || io.KeyCtrl || io.KeyAlt
		|| io.InputQueueCharacters.empty())
		return -1;
	std::string typed;
	for (auto c : io.InputQueueCharacters) {
		if (c < 32 || c == 127)
			continue;
		char utf8[5] = {};
		*SDL_UCS4ToUTF8(c, utf8) = '\0';
		typed += utf8;
	}
	if (typed.empty())
		return -1;
	if (ImGui::GetTime() - searchTime_ > 1)
		searchPrefix_.clear();
	const bool cycling = searchPrefix_ == typed;
	if (!cycling)
		searchPrefix_ += typed;
	searchTime_ = ImGui::GetTime();
	const int count = static_cast<int>(order.size());
	for (int step = cycling ? 1 : 0; step < count + (cycling ? 1 : 0); ++step) {
		const int  i = (std::max(0, current) + step) % count;
		const auto name = order[i].filename().u8string();
		if (SDL_strncasecmp(name.c_str(), searchPrefix_.c_str(),
							searchPrefix_.size())
			== 0)
			return i;
	}
	return -1;
}

void FolderBrowser::treeKeyboard() {
	if (ImGui::GetIO().WantTextInput || focusFilter_ || focusPath_
		|| !ImGui::IsWindowFocused())
		return;
	if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
		if (!tabHandled_) {
			focusGrid_ = true;
			tabHandled_ = true;
			searchPrefix_.clear();
		}
		return;
	}
	const auto key = navigationKey();
	auto found = std::find(treeOrder_.begin(), treeOrder_.end(), selected_);
	if (found == treeOrder_.end())
		return;
	const int current = static_cast<int>(found - treeOrder_.begin());
	fs::path  target;
	if (key == NavigationKey::Left) {
		if (expanded_.count(selected_)) {
			treeToggle_ = selected_;
			treeToggleOpen_ = false;
		} else if (selected_ != root_)
			target = selected_.parent_path();
	} else if (key == NavigationKey::Right) {
		if (!expanded_.count(selected_)) {
			treeToggle_ = selected_;
			treeToggleOpen_ = true;
		} else if (current + 1 < static_cast<int>(treeOrder_.size())
				   && treeOrder_[current + 1].parent_path() == selected_)
			target = treeOrder_[current + 1];
	} else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
			   || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
		treeToggle_ = selected_;
		treeToggleOpen_ = !expanded_.count(selected_);
	} else if (key != NavigationKey::None) {
		const int rows =
			std::max(1, static_cast<int>(ImGui::GetWindowHeight()
										 / ImGui::GetFrameHeight()));
		target = treeOrder_[navigateIndex(
			current, static_cast<int>(treeOrder_.size()), 1, rows, key)];
	}
	if (key == NavigationKey::None) {
		const int match = typeAhead(treeOrder_, current);
		if (match >= 0)
			target = treeOrder_[match];
	}
	if (!target.empty() && target != selected_) {
		select(target);
		reveal_.erase(
			target); // Selection alone doesn't expand the selected row in Qt.
		scrollToSelected_ = true;
	}
}

void FolderBrowser::gridKeyboard(int columns, float rowHeight, int count) {
	if (ImGui::GetIO().WantTextInput || focusFilter_ || focusPath_
		|| !ImGui::IsWindowFocused())
		return;
	const auto &io = ImGui::GetIO();
	if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
		if (!tabHandled_) {
			focusTree_ = true;
			tabHandled_ = true;
			searchPrefix_.clear();
		}
		return;
	}
	int current = -1;
	for (int i = 0; i < count; ++i)
		if (files_[visibleFiles_[i]].path == selectedImage_)
			current = i;
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
		for (auto i : visibleFiles_)
			selectedImages_.insert(files_[i].path);
		if (current < 0 && count) {
			selectedImage_ = files_[visibleFiles_[0]].path;
			imageAnchor_ = selectedImage_;
		}
	}
	const auto			  key = navigationKey();
	std::vector<fs::path> order;
	if (key == NavigationKey::None && !io.InputQueueCharacters.empty())
		for (auto i : visibleFiles_)
			order.push_back(files_[i].path);
	const int match = typeAhead(order, current);
	if ((key != NavigationKey::None || match >= 0) && count) {
		const int rows =
			std::max(1, static_cast<int>(ImGui::GetWindowHeight() / rowHeight));
		const int next =
			match >= 0 ? match
					   : navigateIndex(current, count, columns, rows, key);
		if (io.KeyCtrl && !io.KeyShift)
			selectedImage_ = files_[visibleFiles_[next]].path;
		else
			selectImage(next, io.KeyShift, io.KeyCtrl, false);
		restoreGridScroll_ = false;
		const float top = (next / columns) * rowHeight;
		const float bottom = top + rowHeight;
		const float scroll = ImGui::GetScrollY();
		if (top < scroll)
			ImGui::SetScrollY(top);
		else if (bottom > scroll + ImGui::GetWindowHeight())
			ImGui::SetScrollY(bottom - ImGui::GetWindowHeight());
	}
	if (current >= 0 && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		selectImage(current, io.KeyShift, io.KeyCtrl, io.KeyCtrl);
	if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
		|| ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
		openImages(false);
}

FolderBrowser::FolderBrowser(const fs::path &start, const AppState &state)
	: root_(start.root_path()), selected_(start), address_(4096, '\0'),
	  worker_(&FolderBrowser::work, this) {
	recursive_ = state.recursive;
	showPreview_ = state.showPreview;
	preloadPreviews_ = state.preloadPreviews;
	sortByPath_ = state.sortByPath;
	thumbnailSize_ = state.thumbnailSize;
	treeWidth_ = state.treeWidth;
	// Start with only the active folder and its ancestors open.
	// select() populates reveal_; previously expanded branches stay collapsed.
	std::copy_n(state.filter.data(),
				std::min(state.filter.size(), sizeof(filter_) - 1), filter_);
	select(start);
	selectedImage_ = state.selectedImage;
	if (!selectedImage_.empty())
		selectedImages_.insert(selectedImage_);
	imageAnchor_ = selectedImage_;
	gridScroll_ = state.gridScroll;
	treeScroll_ = state.treeScroll;
	restoreGridScroll_ = gridScroll_ > 0;
}

FolderBrowser::~FolderBrowser() {
	// Cancel every worker before waiting for any one of them.
	stopping_ = true;
	contextActions_.requestStop();
	watcher_.requestStop();
	scanner_.requestStop();
	thumbnails_.requestStop();
	preview_.requestStop();
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = true;
		requests_.clear();
	}
	wake_.notify_one();
	worker_.join();
}

void FolderBrowser::request(const fs::path &path, bool refresh) {
	auto [it, inserted] = listings_.try_emplace(path);
	if (!inserted && !refresh)
		return;
	// Retain the existing listing until a refresh completes.
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (refresh || inserted)
			++listingVersions_[path];
		requests_.erase(
			std::remove_if(requests_.begin(), requests_.end(),
						   [&](const auto &job) { return job.first == path; }),
			requests_.end());
		requests_.emplace_front(path, refresh);
		++requestEpoch_;
	}
	wake_.notify_one();
}

void FolderBrowser::select(const fs::path &path) {
	selected_ = path.lexically_normal();
	root_ = selected_.root_path();
	const auto text = selected_.u8string();
	address_.resize(std::max<std::size_t>(4096, text.size() + 1));
	std::copy(text.begin(), text.end(), address_.begin());
	address_[text.size()] = '\0';
	navigationError_.clear();
	for (auto ancestor = selected_;; ancestor = ancestor.parent_path()) {
		reveal_.insert(ancestor);
		if (ancestor == ancestor.parent_path())
			break;
	}
	gridScroll_ = 0;
	restoreGridScroll_ = true;
	refreshPending_ = false;
	request(selected_);
	watcher_.set(selected_, recursive_, expanded_);
	startScan();
}

void FolderBrowser::work() {
	struct ChildCheck {
		Entry					entry;
		ImageFolderFilter::Scan scan;
	};
	struct PendingListing {
		Listing								  listing;
		std::deque<ChildCheck>				  children;
		std::chrono::steady_clock::time_point published;
	};
	ImageFolderFilter		   imageFolders;
	std::deque<PendingListing> pending;
	std::uint64_t			   cacheGeneration = 0;
	const auto				   publish = [&](PendingListing &job) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_ || job.listing.generation != listingGeneration_
			|| listingVersions_[job.listing.path] != job.listing.version)
			return;
		const auto existing = std::find_if(
			results_.begin(), results_.end(), [&](const Listing &result) {
				return result.path == job.listing.path;
			});
		if (existing != results_.end())
			*existing = job.listing;
		else
			results_.push_back(job.listing);
		job.published = std::chrono::steady_clock::now();
	};
	for (;;) {
		fs::path			  requested;
		std::uint64_t		  generation = 0, version = 0;
		std::vector<fs::path> invalidations;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wake_.wait(lock, [this, &pending] {
				return stopping_ || !requests_.empty() || !pending.empty();
			});
			if (stopping_)
				return;
			generation = listingGeneration_;
			if (!requests_.empty()) {
				requested = std::move(requests_.front().first);
				requests_.pop_front();
				version = listingVersions_[requested];
			}
			invalidations.swap(filterInvalidations_);
		}
		if (cacheGeneration != generation) {
			imageFolders.clear();
			pending.clear();
			cacheGeneration = generation;
		}
		for (const auto &changed : invalidations) {
			if (stopping_)
				return;
			imageFolders.invalidate(changed,
									[this] { return stopping_.load(); });
		}
		if (!requested.empty()) {
			// A refresh supersedes any unfinished check for this listing.
			pending.erase(std::remove_if(pending.begin(), pending.end(),
										 [&](const auto &job) {
											 return job.listing.path
													== requested;
										 }),
						  pending.end());
			PendingListing job;
			job.listing.path = requested;
			job.listing.generation = generation;
			job.listing.version = version;
			std::error_code ec;
			for (fs::directory_iterator it(requested, ec), end;
				 !ec && it != end; it.increment(ec)) {
				if (stopping_)
					return;
				if (generation != listingGeneration_)
					break;
				std::error_code typeError;
				const auto		status = it->symlink_status(typeError);
				if (typeError)
					continue;
				Entry entry{it->path(), it->path().filename().u8string(),
							fs::is_directory(status)};
				if (entry.directory)
					job.children.push_back(
						{entry, ImageFolderFilter::Scan(entry.path)});
				else if (fs::is_regular_file(status)
						 && DirectoryScanner::isImage(entry.path))
					job.listing.entries.push_back(std::move(entry));
			}
			if (ec)
				job.listing.error = ec.message();
			job.listing.ready = job.children.empty();
			publish(job);
			if (!job.listing.ready)
				pending.push_front(std::move(job));
		}
		if (pending.empty())
			continue;
		auto job = std::move(pending.front());
		pending.pop_front();
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (job.listing.generation != listingGeneration_
				|| listingVersions_[job.listing.path] != job.listing.version)
				continue;
		}
		// Each child gets a bounded turn; unfinished subtrees go to the back.
		// New expansion requests are serviced before the next turn.
		auto child = std::move(job.children.front());
		job.children.pop_front();
		const auto result = imageFolders.step(child.scan, [&] {
			return stopping_ || job.listing.generation != listingGeneration_;
		});
		if (stopping_)
			return;
		if (result) {
			if (*result)
				job.listing.entries.push_back(std::move(child.entry));
		} else
			job.children.push_back(std::move(child));
		job.listing.ready = job.children.empty();
		if (job.listing.ready
			|| std::chrono::steady_clock::now() - job.published
				   >= std::chrono::milliseconds(50)) {
			std::sort(job.listing.entries.begin(), job.listing.entries.end(),
					  [](const Entry &a, const Entry &b) {
						  if (a.directory != b.directory)
							  return a.directory;
						  return a.name < b.name;
					  });
			publish(job);
		}
		if (!job.listing.ready)
			pending.push_back(std::move(job));
	}
}

void FolderBrowser::tree(const fs::path &path, unsigned depth) {
	treeOrder_.push_back(path);
	const auto		   id = path.u8string();
	const auto		   name = path == root_ ? id : path.filename().u8string();
	ImGuiTreeNodeFlags flags =
		ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
	if (path == selected_)
		flags |= ImGuiTreeNodeFlags_Selected;
	const bool reveal = reveal_.erase(path) != 0;
	const bool restore = restoreExpanded_.erase(path) != 0;
	if (reveal || restore)
		ImGui::SetNextItemOpen(true);
	if (treeToggle_ == path) {
		ImGui::SetNextItemOpen(treeToggleOpen_);
		treeToggle_.clear();
	}
	const bool open =
		ImGui::TreeNodeEx(id.c_str(), flags, "   %s", name.c_str());
	const auto	 row = ImGui::GetItemRectMin();
	const float	 iconSize = ImGui::GetFontSize() * 0.75f;
	const ImVec2 icon(row.x + ImGui::GetFontSize() + 4,
					  row.y + ImGui::GetStyle().FramePadding.y + 3);
	auto		*draw = ImGui::GetWindowDrawList();
	const auto	 ink = ImGui::GetColorU32(ImGuiCol_Text);
	draw->AddRect(ImVec2(icon.x, icon.y + 2),
				  ImVec2(icon.x + iconSize, icon.y + iconSize), ink);
	draw->AddLine(icon, ImVec2(icon.x + iconSize * 0.45f, icon.y), ink);
	draw->AddLine(icon, ImVec2(icon.x, icon.y + 2), ink);
	draw->AddLine(ImVec2(icon.x + iconSize * 0.45f, icon.y),
				  ImVec2(icon.x + iconSize * 0.6f, icon.y + 2), ink);
	if (scrollToSelected_ && path == selected_) {
		ImGui::SetScrollHereY(0.5f);
		scrollToSelected_ = false;
	}
	if ((ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		|| ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
		select(path);
		reveal_.erase(path);
	}
	if (open)
		expanded_.insert(path);
	else
		expanded_.erase(path);
	if (!open)
		return;
	request(path);
	const auto &listing = listings_.at(path);
	if (!listing.ready)
		ImGui::TextDisabled("Loading...");
	if (!listing.error.empty())
		ImGui::TextWrapped("%s", listing.error.c_str());
	// Bound recursion even when directory symlinks point to ancestors.
	else if (depth >= 64)
		ImGui::TextDisabled("Use the folder view to navigate deeper.");
	else {
		for (const auto &entry : listing.entries)
			if (entry.directory)
				tree(entry.path, depth + 1);
	}
	ImGui::TreePop();
}

void FolderBrowser::startScan(bool preserve) {
	replacing_ = preserve;
	replacement_.clear();
	removed_.clear();
	if (!preserve) {
		thumbnails_.invalidate();
		preview_.invalidate();
		hoveredImage_.clear();
		previewImage_.clear();
		selectedImage_.clear();
		selectedImages_.clear();
		exportedImages_.clear();
		imageAnchor_.clear();
		files_.clear();
		visibleFiles_.clear();
	}
	scanGeneration_ = scanner_.start(selected_, recursive_, true);
	scannedDirectories_ = scanErrors_ = 0;
	scanError_.clear();
	scanning_ = true;
}

void FolderBrowser::updateFilter() {
	visibleFiles_.clear();
	std::set<fs::path> visible;
	for (std::size_t i = 0; i < files_.size(); ++i)
		if (!exportedImages_.count(files_[i].path)
			&& DirectoryScanner::matches(files_[i].name, filter_)) {
			visibleFiles_.push_back(i);
			visible.insert(files_[i].path);
		}
	sortImages(visibleFiles_, files_, sortByPath_);
	for (auto it = selectedImages_.begin(); it != selectedImages_.end();)
		if (!visible.count(*it))
			it = selectedImages_.erase(it);
		else
			++it;
	if (!visible.count(selectedImage_))
		selectedImage_.clear();
}

void FolderBrowser::receiveScan() {
	auto updates = scanner_.takeUpdates();
	bool changed = false;
	for (auto &update : updates) {
		if (update.generation != scanGeneration_)
			continue;
		changed = true;
		update.files.erase(
			std::remove_if(
				update.files.begin(), update.files.end(),
				[&](const auto &file) {
					for (const auto &removed : removed_) {
						const auto relative =
							file.path.lexically_relative(removed.path);
						if (file.path == removed.path
							|| (removed.directory && !relative.empty()
								&& *relative.begin() != ".."))
							return true;
					}
					return false;
				}),
			update.files.end());
		if (replacing_) {
			for (auto &file : update.files)
				replacement_.push_back(std::move(file));
			if (update.done) {
				files_ = std::move(replacement_);
				replacing_ = false;
				updateFilter();
			}
		} else {
			const auto offset = files_.size();
			for (auto &file : update.files)
				files_.push_back(std::move(file));
			for (auto i = offset; i < files_.size(); ++i)
				if (!exportedImages_.count(files_[i].path)
					&& DirectoryScanner::matches(files_[i].name, filter_))
					visibleFiles_.push_back(i);
		}
		scannedDirectories_ = update.directories;
		scanErrors_ = update.errors;
		scanError_ = std::move(update.lastError);
		scanning_ = !update.done;
	}
	if (changed)
		sortImages(visibleFiles_, files_, sortByPath_);
}

void FolderBrowser::contents() {
	ImGui::SetNextItemWidth(-1);
	if (focusFilter_) {
		ImGui::SetKeyboardFocusHere();
		focusFilter_ = false;
	}
	if (ImGui::InputTextWithHint("##Filter",
								 "Filter filenames (text, *.jpg, photo?.png)",
								 filter_, sizeof(filter_)))
		updateFilter();
	ImGui::TextDisabled("%zu of %zu files | %zu folders%s",
						visibleFiles_.size(), files_.size(),
						scannedDirectories_, scanning_ ? " | Scanning..." : "");
	if (scanErrors_)
		ImGui::TextWrapped("%zu scan errors. Last: %s", scanErrors_,
						   scanError_.c_str());
	if (!watchError_.empty())
		ImGui::TextWrapped("Folder watching: %s", watchError_.c_str());
	if (ImGui::BeginChild("Thumbnail grid", ImVec2(0, 0), 0,
						  ImGuiWindowFlags_NoScrollWithMouse
							  | ImGuiWindowFlags_NoNavInputs)) {
		if (focusGrid_) {
			ImGui::SetWindowFocus();
			focusGrid_ = false;
		}
		if (restoreGridScroll_ && (gridScroll_ == 0 || !scanning_)) {
			ImGui::SetScrollY(gridScroll_);
			restoreGridScroll_ = false;
		} else if (!restoreGridScroll_)
			gridScroll_ = ImGui::GetScrollY();
		if (!scanning_ && visibleFiles_.empty())
			ImGui::TextDisabled(files_.empty() ? "No images in this folder."
											   : "No images match the filter.");
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float available = ImGui::GetContentRegionAvail().x;
		const float side = std::max(32.0f, std::min(thumbnailSize_, available));
		const int	columns = std::max(
			1, static_cast<int>((available + spacing) / (side + spacing)));
		const float rowHeight = side + ImGui::GetTextLineHeight() + 16;
		const float wheel = ImGui::GetIO().MouseWheel;
		bool		gridWillScroll = false;
		if (wheel != 0 && ImGui::IsWindowHovered() && !ImGui::GetIO().KeyCtrl) {
			restoreGridScroll_ = false;
			const float steps = std::ceil(std::abs(wheel));
			const float target =
				ImGui::GetScrollY() + (wheel > 0 ? -steps : steps) * rowHeight;
			const float clamped =
				std::clamp(target, 0.0f, ImGui::GetScrollMaxY());
			gridWillScroll = std::abs(clamped - ImGui::GetScrollY()) > 0.5f;
			if (gridWillScroll)
				ImGui::SetScrollY(clamped);
		}
		const int count = static_cast<int>(
			std::min<std::size_t>(visibleFiles_.size(), INT_MAX));
		gridKeyboard(columns, rowHeight, count);
		const int		 rows = count / columns + (count % columns != 0);
		bool			 firstRow = true;
		ImGuiListClipper clipper;
		clipper.Begin(rows, rowHeight);
		while (clipper.Step()) {
			if (firstRow) {
				firstVisible_ = clipper.DisplayStart * columns;
				firstRow = false;
			}
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
				 ++row) {
				const auto origin = ImGui::GetCursorScreenPos();
				for (int column = 0; column < columns; ++column) {
					const int index = row * columns + column;
					if (index >= count)
						break;
					const auto	&file = files_[visibleFiles_[index]];
					const ImVec2 pos(origin.x + column * (side + spacing),
									 origin.y);
					ImGui::SetCursorScreenPos(pos);
					ImGui::PushID(index);
					if (ImGui::InvisibleButton(
							"Image",
							ImVec2(side,
								   rowHeight
									   - ImGui::GetStyle().ItemSpacing.y)))
						selectImage(index, ImGui::GetIO().KeyShift,
									ImGui::GetIO().KeyCtrl,
									ImGui::GetIO().KeyCtrl);
					if (ImGui::IsItemHovered()
						&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						if (!selectedImages_.count(file.path))
							selectImage(index, false, false, false);
						openImages(false);
					}
					if (ImGui::IsItemHovered()
						&& ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
						beginContext(file.path);
						// Use the grid window ID so every thumbnail opens the
						// same popup.
						ImGui::PopID();
						ImGui::OpenPopup("Image actions");
						ImGui::PushID(index);
					}
					const bool hovered = ImGui::IsItemHovered();
					auto	  *draw = ImGui::GetWindowDrawList();
					draw->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side),
										ImGui::GetColorU32(ImGuiCol_FrameBg),
										3);
					// Visible cells get priority over the background thumbnail
					// preload.
					const auto &texture = thumbnails_.get(file.path, true);
					if (texture.id) {
						const float	 scale = std::min(side / texture.width,
													  side / texture.height);
						const ImVec2 size(texture.width * scale,
										  texture.height * scale);
						const ImVec2 imagePos(pos.x + (side - size.x) / 2,
											  pos.y + (side - size.y) / 2);
						draw->AddImage(
							static_cast<ImTextureID>(texture.id), imagePos,
							ImVec2(imagePos.x + size.x, imagePos.y + size.y));
					} else {
						const char *label = texture.error.empty()
												? "Loading..."
												: "Cannot load";
						const auto	size = ImGui::CalcTextSize(label);
						draw->AddText(ImVec2(pos.x + (side - size.x) / 2,
											 pos.y + (side - size.y) / 2),
									  ImGui::GetColorU32(ImGuiCol_TextDisabled),
									  label);
					}
					if (selectedImages_.count(file.path)
						|| selectedImage_ == file.path || hovered)
						draw->AddRect(
							pos, ImVec2(pos.x + side, pos.y + side),
							ImGui::GetColorU32(selectedImages_.count(file.path)
												   ? ImGuiCol_HeaderActive
												   : ImGuiCol_HeaderHovered),
							4.0f, 2.0f);
					draw->PushClipRect(ImVec2(pos.x, pos.y + side),
									   ImVec2(pos.x + side, pos.y + rowHeight),
									   true);
					draw->AddText(ImVec2(pos.x, pos.y + side + 4),
								  ImGui::GetColorU32(ImGuiCol_Text),
								  file.name.c_str());
					draw->PopClipRect();
					if (hovered && !gridWillScroll) {
						hoveredImage_ = file.path;
						hoveredIndex_ = index;
					}
					if (hovered && !showPreview_) {
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(file.relativePath.c_str());
						if (!texture.error.empty())
							ImGui::TextWrapped("%s", texture.error.c_str());
						ImGui::EndTooltip();
					}
					ImGui::PopID();
				}
				ImGui::SetCursorScreenPos(
					ImVec2(origin.x, origin.y + rowHeight
										 - ImGui::GetStyle().ItemSpacing.y));
				ImGui::Dummy(ImVec2(0, 0));
			}
		}
		if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()
			&& ImGui::IsMouseReleased(ImGuiMouseButton_Right)
			&& !selectedImages_.empty()) {
			beginContext({});
			ImGui::OpenPopup("Image actions");
		}
		if (ImGui::BeginPopup("Image actions")) {
			drawContextMenu();
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();
}

void FolderBrowser::draw() {
	tabHandled_ = false;
	watchChanges();
	thumbnails_.beginFrame();
	preview_.beginFrame();
	hoveredImage_.clear();
	hoveredIndex_ = -1;
	std::deque<Listing> completed;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		completed.swap(results_);
	}
	for (auto &listing : completed) {
		if (listing.generation != listingGeneration_)
			continue;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			const auto it = listingVersions_.find(listing.path);
			if (it == listingVersions_.end() || it->second != listing.version)
				continue;
		}
		auto &current = listings_[listing.path];
		mergeTreeEntries(current.entries, listing.entries, listing.ready);
		current.path = listing.path;
		current.error = std::move(listing.error);
		current.ready = listing.ready;
		current.generation = listing.generation;
		current.version = listing.version;
	}
	receiveScan();
	if (startupLoading_) {
		drawStartupSplash();
		return;
	}
	const auto &io = ImGui::GetIO();
	if (!io.WantTextInput
		&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		if (io.KeyCtrl) {
			if (ImGui::IsKeyPressed(ImGuiKey_O, false))
				openImages(false);
			if (ImGui::IsKeyPressed(ImGuiKey_S, false))
				openImages(true);
			if (ImGui::IsKeyPressed(ImGuiKey_I, false)) {
				std::vector<fs::path> images;
				for (auto i : visibleFiles_)
					if (selectedImages_.count(files_[i].path))
						images.push_back(files_[i].path);
				requestExport(images);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_F, false))
				focusFilter_ = true;
			if (ImGui::IsKeyPressed(ImGuiKey_L, false))
				focusPath_ = true;
			if (ImGui::IsKeyPressed(ImGuiKey_Equal)
				|| ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
				thumbnailSize_ = std::min(256.0f, thumbnailSize_ + 16);
			if (ImGui::IsKeyPressed(ImGuiKey_Minus)
				|| ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
				thumbnailSize_ = std::max(96.0f, thumbnailSize_ - 16);
			if (ImGui::IsKeyPressed(ImGuiKey_Slash, false))
				thumbnailSize_ = 160;
			if (io.MouseWheel != 0)
				thumbnailSize_ = std::clamp(thumbnailSize_ + io.MouseWheel * 16,
											96.0f, 256.0f);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
			refreshTree();
			thumbnails_.invalidate();
			preview_.invalidate();
			startScan(true);
		}
	}
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("View")) {
			if (ImGui::MenuItem("Include subfolders", nullptr, &recursive_)) {
				watcher_.set(selected_, recursive_, expanded_);
				startScan();
			}
			if (ImGui::MenuItem("Sort by full path", nullptr, sortByPath_)) {
				sortByPath_ = true;
				updateFilter();
			}
			if (ImGui::MenuItem("Sort by filename", nullptr, !sortByPath_)) {
				sortByPath_ = false;
				updateFilter();
			}
			ImGui::MenuItem("Show preview", nullptr, &showPreview_);
			ImGui::MenuItem("Preload previews", nullptr, &preloadPreviews_);
			ImGui::SliderFloat("Thumbnail size", &thumbnailSize_, 96, 256,
							   "%.0f px");
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
	if (ImGui::Button("Up") && selected_ != root_)
		select(selected_.parent_path());
	ImGui::SameLine();
	if (ImGui::Button("Refresh")) {
		refreshTree();
		thumbnails_.invalidate();
		preview_.invalidate();
		startScan(true);
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	if (focusPath_) {
		ImGui::SetKeyboardFocusHere();
		focusPath_ = false;
	}
	if (ImGui::InputText("##Path", address_.data(), address_.size(),
						 ImGuiInputTextFlags_EnterReturnsTrue)) {
		const auto		path = fs::u8path(address_.data());
		std::error_code ec;
		const auto		absolute = fs::absolute(path, ec);
		if (!ec && fs::is_directory(absolute, ec))
			select(absolute);
		else
			navigationError_ =
				ec ? ec.message() : "Enter an existing folder path.";
	}
	if (!navigationError_.empty())
		ImGui::TextWrapped("%s", navigationError_.c_str());
	ImGui::Separator();
	// A resizable child supplies the splitter without relying on ImGui
	// internals.
	treeWidth_ =
		std::clamp(treeWidth_, 120.0f,
				   std::max(120.0f, ImGui::GetContentRegionAvail().x - 160));
	ImGui::SetNextWindowSizeConstraints(
		ImVec2(120, 0),
		ImVec2(std::max(120.0f, ImGui::GetContentRegionAvail().x - 160),
			   FLT_MAX));
	if (restoreTreeWidth_) {
		ImGui::SetNextWindowSize(ImVec2(treeWidth_, 0));
		restoreTreeWidth_ = false;
	}
	const bool	showPreloadProgress = true;
	const float panesHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
	previewSize_ = ImVec2(0, 0);
	if (ImGui::BeginChild("Folders", ImVec2(treeWidth_, panesHeight),
						  ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX,
						  ImGuiWindowFlags_NoNavInputs)) {
		treeWidth_ = ImGui::GetWindowWidth();
		previewPosition_ = ImGui::GetWindowPos();
		previewSize_ = ImGui::GetWindowSize();
		ImGui::TextUnformatted("Folders");
		ImGui::Separator();
		if (focusTree_) {
			ImGui::SetWindowFocus();
			focusTree_ = false;
		}
		treeKeyboard();
		treeOrder_.clear();
		tree(root_);
		treeScroll_ = ImGui::GetScrollY();
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if (previewSize_.x > 0 && previewSize_.y > 0) {
		// Bucket sizes avoid repeatedly restarting work during small
		// splitter/window resizes.
		const int edge = static_cast<int>(std::ceil(
							 std::max(previewSize_.x, previewSize_.y) / 64))
						 * 64;
		preview_.setEdge(edge);
	}
	if (ImGui::BeginChild("Folder contents", ImVec2(0, panesHeight),
						  ImGuiChildFlags_Borders,
						  ImGuiWindowFlags_NoScrollWithMouse)) {
		const ImVec2 progressOrigin = ImGui::GetCursorScreenPos();
		const ImVec2 progressArea = ImGui::GetContentRegionAvail();
		contents();
		ThumbnailCache::PreloadProgress preloadProgress;
		bool							loadingThumbnails = true;
		if (!visibleFiles_.empty()) {
			std::vector<fs::path> warm;
			warm.reserve(visibleFiles_.size());
			for (const auto i : visibleFiles_)
				warm.push_back(files_[i].path);
			preloadProgress = thumbnails_.preload(warm);
			loadingThumbnails =
				scanning_
				|| preloadProgress.ready + preloadProgress.failed
					   < preloadProgress.total;
			if (!loadingThumbnails && preloadPreviews_ && showPreview_)
				preloadProgress = preview_.preload(warm);
		}
		const auto completed = preloadProgress.ready + preloadProgress.failed;
		if (showPreloadProgress
			&& (loadingThumbnails || (preloadPreviews_ && showPreview_))
			&& completed < preloadProgress.total) {
			// Overlay within the folder pane so completion does not shift the
			// thumbnail grid.
			const float height = ImGui::GetFrameHeight();
			ImGui::SetCursorScreenPos(ImVec2(
				progressOrigin.x,
				progressOrigin.y + std::max(0.0f, progressArea.y - height)));
			char		label[64];
			const float fraction =
				static_cast<float>(completed) / preloadProgress.total;
			std::snprintf(label, sizeof(label), "%s: %.0f%%",
						  loadingThumbnails ? "Thumbnails" : "Previews",
						  fraction * 100.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg,
								  IM_COL32(230, 230, 230, 255));
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
								  IM_COL32(48, 140, 198, 255));
			ImGui::PushStyleColor(ImGuiCol_Border,
								  IM_COL32(150, 150, 150, 255));
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 20, 255));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::ProgressBar(fraction, ImVec2(progressArea.x, height), label);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4);
		}
	}
	ImGui::EndChild();
	drawPreview();
	drawExport();
	watcher_.set(selected_, recursive_, expanded_);
	drawStartupSplash();
}

void FolderBrowser::drawStartupSplash() {
	if (!startupLoading_)
		return;
	std::set<fs::path> initial = expanded_;
	initial.insert(reveal_.begin(), reveal_.end());
	initial.insert(restoreExpanded_.begin(), restoreExpanded_.end());
	initial.insert(root_);
	std::size_t pending = 0;
	for (const auto &path : initial) {
		request(path);
		if (!listings_.at(path).ready)
			++pending;
	}
	if (!pending)
		startupLoading_ = false;
	ImGui::SetCursorPos(ImVec2(24, 24));
	ImGui::TextUnformatted("ImgViewManager");
	ImGui::Separator();
	const int dots = static_cast<int>(ImGui::GetTime() * 3) % 4;
	ImGui::Text("Loading folder tree%.*s", dots, "...");
	ImGui::TextDisabled("%zu tree branches remaining", pending);
	ImGui::TextWrapped("%s", selected_.u8string().c_str());
}

void FolderBrowser::drawPreview() {
	if (!showPreview_)
		hoveredImage_.clear();
	previewImage_ = hoveredImage_;
	if (previewImage_.empty() || previewSize_.x <= 0 || previewSize_.y <= 0)
		return;
	const auto &texture = preview_.get(previewImage_);
	// The foreground layer stays above the tree without taking mouse focus.
	auto		*draw = ImGui::GetForegroundDrawList();
	const ImVec2 end(previewPosition_.x + previewSize_.x,
					 previewPosition_.y + previewSize_.y);
	draw->PushClipRect(previewPosition_, end, true);
	draw->AddRectFilled(previewPosition_, end, IM_COL32(229, 243, 255, 127));
	draw->AddRect(previewPosition_, end, ImGui::GetColorU32(ImGuiCol_Border));
	const auto	 padding = ImGui::GetStyle().WindowPadding;
	const ImVec2 origin(previewPosition_.x + padding.x,
						previewPosition_.y + padding.y);
	const float	 captionHeight =
		ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
	const ImVec2 box(
		std::max(1.0f, previewSize_.x - padding.x * 2),
		std::max(1.0f, previewSize_.y - padding.y * 2 - captionHeight));
	if (texture.id) {
		const float scale =
			std::min({box.x / texture.width, box.y / texture.height, 1.0f});
		const ImVec2 size(texture.width * scale, texture.height * scale);
		const ImVec2 pos(origin.x + (box.x - size.x) / 2,
						 origin.y + (box.y - size.y) / 2);
		draw->AddImage(static_cast<ImTextureID>(texture.id), pos,
					   ImVec2(pos.x + size.x, pos.y + size.y));
	} else {
		const char *label =
			texture.error.empty() ? "Loading preview..." : "Cannot load image";
		const auto size = ImGui::CalcTextSize(label);
		draw->AddText(ImVec2(origin.x + (box.x - size.x) / 2,
							 origin.y + (box.y - size.y) / 2),
					  ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
	}
	draw->AddText(
		ImVec2(origin.x, origin.y + box.y + ImGui::GetStyle().ItemSpacing.y),
		ImGui::GetColorU32(ImGuiCol_Text),
		previewImage_.filename().u8string().c_str());
	draw->PopClipRect();
}

void FolderBrowser::saveInto(AppState &state) const {
	state.folder = selected_;
	state.selectedImage = selectedImage_;
	state.expanded = expanded_;
	state.filter = filter_;
	state.treeWidth = treeWidth_;
	state.thumbnailSize = thumbnailSize_;
	state.recursive = recursive_;
	state.showPreview = showPreview_;
	state.preloadPreviews = preloadPreviews_;
	state.sortByPath = sortByPath_;
	state.gridScroll = gridScroll_;
	state.treeScroll = treeScroll_;
}

void FolderBrowser::refreshTree() {
	// Explicit refresh or lost events: reconcile cached rows without blanking
	// the tree.
	std::vector<fs::path> paths;
	for (const auto &[path, listing] : listings_)
		paths.push_back(path);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		++listingGeneration_;
		++requestEpoch_;
		requests_.clear();
		results_.clear();
		filterInvalidations_.clear();
	}
	for (const auto &path : paths)
		request(path, true);
	request(selected_, true);
}

void FolderBrowser::updateTree(
	const std::vector<DirectoryWatcher::Change> &changes) {
	std::set<fs::path> affected;
	for (const auto &change : changes) {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			filterInvalidations_.push_back(change.path);
		}
		if (change.removed) {
			// Pluck the row immediately, including its cached descendants.
			for (auto &[path, listing] : listings_)
				listing.entries.erase(
					std::remove_if(
						listing.entries.begin(), listing.entries.end(),
						[&](const auto &entry) {
							return entry.path == change.path
								   || (change.directory
									   && pathWithin(entry.path, change.path));
						}),
					listing.entries.end());
			if (change.directory) {
				for (auto it = listings_.begin(); it != listings_.end();) {
					if (pathWithin(it->first, change.path)) {
						std::lock_guard<std::mutex> lock(mutex_);
						++listingVersions_[it->first];
						it = listings_.erase(it);
					} else
						++it;
				}
				for (auto *paths : {&expanded_, &restoreExpanded_, &reveal_})
					for (auto it = paths->begin(); it != paths->end();)
						if (pathWithin(*it, change.path))
							it = paths->erase(it);
						else
							++it;
				std::lock_guard<std::mutex> lock(mutex_);
				requests_.erase(
					std::remove_if(requests_.begin(), requests_.end(),
								   [&](const auto &job) {
									   return pathWithin(job.first,
														 change.path);
								   }),
					requests_.end());
			}
		} else if (change.directory && listings_.count(change.path))
			affected.insert(change.path);
		// Image presence can change along the ancestors, but unrelated branches
		// stay cached.
		for (auto parent = change.path.parent_path(); !parent.empty();) {
			if (listings_.count(parent))
				affected.insert(parent);
			const auto next = parent.parent_path();
			if (next == parent)
				break;
			parent = next;
		}
	}
	for (const auto &path : affected)
		if (listings_.count(path))
			request(path, true);
}

void FolderBrowser::watchChanges() {
	auto changes = watcher_.takeChanges();
	if (!changes.error.empty())
		watchError_ = changes.error;
	const auto within = [](const fs::path &path, const fs::path &directory) {
		const auto relative = path.lexically_relative(directory);
		return path == directory
			   || (!relative.empty() && *relative.begin() != "..");
	};
	std::vector<fs::path> removedFolders;
	bool				  selectedDeleted = false;
	for (const auto &change : changes.items) {
		if (change.removed && change.directory) {
			removedFolders.push_back(change.path);
			selectedDeleted |= pathWithin(selected_, change.path);
		}
	}
	const auto exists = [](const fs::path &path) {
		std::error_code ec;
		return fs::is_directory(path, ec);
	};
	if ((changes.resync || changes.viewResync) && !exists(selected_)) {
		selectedDeleted = true;
		removedFolders.push_back(selected_);
	}
	fs::path replacement;
	if (selectedDeleted) {
		replacement =
			folderAfterDeletion(treeOrder_, selected_, removedFolders, exists);
		if (replacement.empty()) {
			replacement = selected_.parent_path();
			while (replacement != replacement.parent_path()
				   && !exists(replacement))
				replacement = replacement.parent_path();
		}
	}
	bool filesChanged = changes.resync || changes.viewResync;
	for (const auto &change : changes.items) {
		if (!within(change.path, selected_))
			continue;
		if (!recursive_ && change.path != selected_
			&& change.path.parent_path() != selected_)
			continue;
		filesChanged = true;
		thumbnails_.remove(change.path);
		preview_.remove(change.path);
		if (change.path == previewImage_
			|| (change.directory && within(previewImage_, change.path))) {
			preview_.remove(previewImage_);
			previewImage_.clear();
		}
		if (!change.removed)
			removed_.erase(std::remove_if(removed_.begin(), removed_.end(),
										  [&](const auto &item) {
											  return item.path == change.path;
										  }),
						   removed_.end());
		if (change.removed) {
			removed_.push_back(change);
			const auto erase = [&](auto &files) {
				files.erase(std::remove_if(
								files.begin(), files.end(),
								[&](const auto &file) {
									const bool removed =
										file.path == change.path
										|| (change.directory
											&& within(file.path, change.path));
									if (removed) {
										thumbnails_.remove(file.path);
										preview_.remove(file.path);
										if (selectedImage_ == file.path)
											selectedImage_.clear();
									}
									return removed;
								}),
							files.end());
			};
			erase(files_);
			erase(replacement_);
			updateFilter();
		}
	}
	if (changes.resync)
		refreshTree();
	else if (!changes.items.empty())
		updateTree(changes.items);
	if (selectedDeleted && !replacement.empty()) {
		select(replacement);
		scrollToSelected_ = true;
		return;
	}
	if (filesChanged) {
		refreshPending_ = true;
		refreshAt_ = ImGui::GetTime() + 0.15;
	}
	if (refreshPending_ && ImGui::GetTime() >= refreshAt_ && !scanning_) {
		std::error_code ec;
		if (!fs::is_directory(selected_, ec)) {
			const std::vector<fs::path> missing{selected_};
			auto						fallback =
				folderAfterDeletion(treeOrder_, selected_, missing, exists);
			if (fallback.empty())
				fallback = selected_.parent_path();
			while (fallback != fallback.parent_path()
				   && !fs::is_directory(fallback, ec))
				fallback = fallback.parent_path();
			select(fallback);
			scrollToSelected_ = true;
		} else {
			refreshPending_ = false;
			startScan(true);
		}
	}
}
