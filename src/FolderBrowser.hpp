#pragma once
#include "DirectoryScanner.hpp"
#include "DirectoryWatcher.hpp"
#include "AppState.hpp"
#include "ThumbnailCache.hpp"
#include "ContextActions.hpp"
#include <memory>
#include "imgui.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class FolderBrowser {
public:
	explicit FolderBrowser(const std::filesystem::path &start,
						   const AppState			   &state = {});
	~FolderBrowser();
	FolderBrowser(const FolderBrowser &) = delete;
	FolderBrowser &operator=(const FolderBrowser &) = delete;
	void		   draw();
	void		   saveInto(AppState &state) const;
	bool		   startupLoading() const { return startupLoading_; }
	const std::filesystem::path &selectedFolder() const { return selected_; }

private:
	struct Entry {
		std::filesystem::path path;
		std::string			  name;
		bool				  directory = false;
	};
	struct Listing {
		std::filesystem::path path;
		std::vector<Entry>	  entries;
		std::string			  error;
		bool				  ready = false;
		std::uint64_t		  generation = 0;
		std::uint64_t		  version = 0;
	};
	void request(const std::filesystem::path &path, bool refresh = false);
	void select(const std::filesystem::path &path);
	void tree(const std::filesystem::path &path, unsigned depth = 0);
	void contents();
	void treeKeyboard();
	void gridKeyboard(int columns, float rowHeight, int count);
	void selectImage(int index, bool range, bool additive, bool toggle);
	void openImages(bool source);
	void beginContext(const std::filesystem::path &pointed);
	void drawContextMenu();
	void requestExport(const std::vector<std::filesystem::path> &images);
	void drawExport();
	struct ExportDialog {
		std::mutex						   mutex;
		bool							   done = false;
		std::vector<std::filesystem::path> images;
		std::filesystem::path			   destination, origin;
		std::string						   error;
	};
	ContextActions					   contextActions_;
	std::shared_ptr<ExportDialog>	   exportDialog_;
	std::filesystem::path			   contextPointed_, exportDirectory_;
	std::vector<std::filesystem::path> contextImages_, contextSelection_;
	bool exportProgressOpen_ = false, exportApplied_ = false;
	std::set<std::filesystem::path> exportedImages_;
	std::string						exportError_;
	bool focusTree_ = true, focusGrid_ = false, tabHandled_ = false;
	bool focusFilter_ = false, focusPath_ = false;
	int typeAhead(const std::vector<std::filesystem::path> &order, int current);
	std::string						searchPrefix_;
	double							searchTime_ = 0;
	std::filesystem::path			treeToggle_;
	bool							treeToggleOpen_ = false;
	std::set<std::filesystem::path> selectedImages_;
	std::filesystem::path			imageAnchor_;
	void							drawPreview();
	void							drawStartupSplash();
	void							work();
	void							startScan(bool preserve = false);
	void							watchChanges();
	void							refreshTree();
	void updateTree(const std::vector<DirectoryWatcher::Change> &changes);
	void receiveScan();
	void updateFilter();
	DirectoryWatcher				   watcher_;
	std::set<std::filesystem::path>	   expanded_;
	std::vector<std::filesystem::path> treeOrder_;
	std::set<std::filesystem::path>	   restoreExpanded_;
	float							   gridScroll_ = 0, treeScroll_ = 0;
	bool   restoreGridScroll_ = false, restoreTreeWidth_ = true;
	bool   scrollToSelected_ = true;
	bool   startupLoading_ = true;
	bool   refreshPending_ = false, replacing_ = false;
	double refreshAt_ = 0;
	std::vector<DirectoryScanner::File>	  replacement_;
	std::vector<DirectoryWatcher::Change> removed_;
	std::string							  watchError_;
	DirectoryScanner					  scanner_;
	ThumbnailCache thumbnails_{256, static_cast<std::size_t>(-1),
							   std::size_t{1} * 1024 * 1024 * 1024, 32, true};
	ThumbnailCache preview_{2048,
							static_cast<std::size_t>(-1),
							std::size_t{10} * 1024 * 1024 * 1024,
							2,
							true,
							false};
	bool		   preloadPreviews_ = true, sortByPath_ = true;
	int			   firstVisible_ = 0, hoveredIndex_ = -1;
	bool		   showPreview_ = true;
	std::filesystem::path				hoveredImage_;
	std::filesystem::path				previewImage_;
	ImVec2								previewPosition_;
	ImVec2								previewSize_;
	float								thumbnailSize_ = 160;
	std::filesystem::path				selectedImage_;
	std::uint64_t						scanGeneration_ = 0;
	std::vector<DirectoryScanner::File> files_;
	std::vector<std::size_t>			visibleFiles_;
	char								filter_[512] = {};
	bool								recursive_ = true;
	bool								scanning_ = false;
	std::size_t							scannedDirectories_ = 0;
	std::size_t							scanErrors_ = 0;
	std::string							scanError_;

	std::filesystem::path							   root_;
	std::filesystem::path							   selected_;
	std::map<std::filesystem::path, Listing>		   listings_;
	std::set<std::filesystem::path>					   reveal_;
	std::string										   navigationError_;
	std::vector<char>								   address_;
	float											   treeWidth_ = 280.0f;
	std::mutex										   mutex_;
	std::condition_variable							   wake_;
	std::deque<std::pair<std::filesystem::path, bool>> requests_;
	std::deque<Listing>								   results_;
	std::map<std::filesystem::path, std::uint64_t>	   listingVersions_;
	std::vector<std::filesystem::path>				   filterInvalidations_;
	std::atomic<bool>								   stopping_{false};
	std::atomic<std::uint64_t>						   listingGeneration_{0};
	std::atomic<std::uint64_t>						   requestEpoch_{0};
	std::thread										   worker_;
};
