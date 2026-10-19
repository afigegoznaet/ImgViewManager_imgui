#pragma once
#include <algorithm>
#include <filesystem>
#include <map>
#include <vector>

inline bool pathWithin(const std::filesystem::path &path,
					   const std::filesystem::path &parent) {
	const auto relative = path.lexically_relative(parent);
	return path == parent || (!relative.empty() && *relative.begin() != "..");
}

// Merge worker snapshots into the existing rows. Partial scans only add rows;
// completed scans can remove missing rows. Node IDs remain based on full paths.
template <class Entry>
void mergeTreeEntries(std::vector<Entry>	   &current,
					  const std::vector<Entry> &incoming, bool complete) {
	const auto order = [](const Entry &a, const Entry &b) {
		if (a.directory != b.directory)
			return a.directory;
		if (a.name != b.name)
			return a.name < b.name;
		return a.path < b.path;
	};
	std::map<std::filesystem::path, const Entry *> incomingByPath;
	for (const auto &entry : incoming)
		incomingByPath.emplace(entry.path, &entry);
	current.erase(
		std::remove_if(current.begin(), current.end(),
					   [&](const Entry &entry) {
						   const auto it = incomingByPath.find(entry.path);
						   if (it == incomingByPath.end())
							   return complete;
						   // A file replaced by a directory (or vice versa)
						   // must move to its new sorted position.
						   return it->second->directory != entry.directory
								  || it->second->name != entry.name;
					   }),
		current.end());
	for (const auto &entry : incoming) {
		auto it =
			std::lower_bound(current.begin(), current.end(), entry, order);
		if (it != current.end() && it->path == entry.path)
			*it = entry;
		else
			current.insert(it, entry);
	}
}

// Choose from the expanded tree's displayed row order, skipping deleted
// subtrees.
template <class Exists>
std::filesystem::path
folderAfterDeletion(const std::vector<std::filesystem::path> &order,
					const std::filesystem::path				 &current,
					const std::vector<std::filesystem::path> &removed,
					Exists									  exists) {
	auto position = std::find(order.begin(), order.end(), current);
	if (position == order.end()) {
		position =
			std::find_if(order.begin(), order.end(), [&](const auto &path) {
				return pathWithin(current, path)
					   && std::find(removed.begin(), removed.end(), path)
							  != removed.end();
			});
	}
	if (position == order.end())
		return {};
	const auto valid = [&](const auto &path) {
		for (const auto &deleted : removed)
			if (pathWithin(path, deleted))
				return false;
		return exists(path);
	};
	for (auto next = std::next(position); next != order.end(); ++next)
		if (valid(*next))
			return *next;
	while (position != order.begin()) {
		--position;
		if (valid(*position))
			return *position;
	}
	return {};
}
