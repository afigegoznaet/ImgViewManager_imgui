#include "ImageSorting.hpp"
#include <unicode/coll.h>
#include <unicode/sortkey.h>
#include <unicode/unistr.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
void sortImages(std::vector<std::size_t>				  &order,
				const std::vector<DirectoryScanner::File> &files,
				bool									   fullPath) {
	UErrorCode					   status = U_ZERO_ERROR;
	std::unique_ptr<icu::Collator> collator(
		icu::Collator::createInstance(status));
	if (U_FAILURE(status) || !collator)
		throw std::runtime_error("Cannot initialize image sorting");
	collator->setAttribute(UCOL_NUMERIC_COLLATION, UCOL_ON, status);
	if (U_FAILURE(status))
		throw std::runtime_error("Cannot enable numeric image sorting");
	struct Key {
		std::size_t		  index;
		icu::CollationKey value;
	};
	std::vector<Key> keys;
	keys.reserve(order.size());
	for (auto index : order) {
		const auto text =
			fullPath ? files[index].path.u8string() : files[index].name;
		keys.push_back({index, {}});
		collator->getCollationKey(icu::UnicodeString::fromUTF8(text),
								  keys.back().value, status);
		if (U_FAILURE(status))
			throw std::runtime_error("Cannot sort image filename");
	}
	std::stable_sort(keys.begin(), keys.end(), [](const Key &a, const Key &b) {
		UErrorCode status = U_ZERO_ERROR;
		return a.value.compareTo(b.value, status) == UCOL_LESS;
	});
	for (std::size_t i = 0; i < keys.size(); ++i)
		order[i] = keys[i].index;
}
