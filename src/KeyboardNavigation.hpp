#pragma once
#include <algorithm>
#include <filesystem>
#include <set>
#include <vector>

enum class NavigationKey {
	None,
	Left,
	Right,
	Up,
	Down,
	Home,
	End,
	PageUp,
	PageDown
};
inline int navigateIndex(int current, int count, int columns, int pageRows,
						 NavigationKey key) {
	if (count == 0)
		return -1;
	if (current < 0)
		return key == NavigationKey::End ? count - 1 : 0;
	int target = current;
	switch (key) {
	case NavigationKey::Left:
		--target;
		break;
	case NavigationKey::Right:
		++target;
		break;
	case NavigationKey::Up:
		target -= columns;
		break;
	case NavigationKey::Down:
		target += columns;
		break;
	case NavigationKey::Home:
		target = 0;
		break;
	case NavigationKey::End:
		target = count - 1;
		break;
	case NavigationKey::PageUp:
		target -= columns * pageRows;
		break;
	case NavigationKey::PageDown:
		target += columns * pageRows;
		break;
	default:
		break;
	}
	// Up/Down at a boundary should keep the current column where possible.
	if (key == NavigationKey::Up && target < 0)
		return current;
	if (key == NavigationKey::Down
		&& current / columns == (count - 1) / columns)
		return current;
	return std::clamp(target, 0, count - 1);
}
inline void selectRange(std::set<std::filesystem::path>			 &selection,
						const std::vector<std::filesystem::path> &order,
						int anchor, int target, bool additive) {
	if (!additive)
		selection.clear();
	if (order.empty())
		return;
	anchor = std::clamp(anchor, 0, static_cast<int>(order.size()) - 1);
	target = std::clamp(target, 0, static_cast<int>(order.size()) - 1);
	for (int i = std::min(anchor, target); i <= std::max(anchor, target); ++i)
		selection.insert(order[i]);
}
