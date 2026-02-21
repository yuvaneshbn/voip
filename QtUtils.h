#pragma once

#include <QtCore/QString>

#include <filesystem>
#include <string>

namespace Mumble {
namespace QtUtils {

struct CaseInsensitiveQString {
	QString value;

	CaseInsensitiveQString() = default;
	CaseInsensitiveQString(const QString &str) : value(str) {}

	friend bool operator==(const CaseInsensitiveQString &lhs, const CaseInsensitiveQString &rhs) {
		return lhs.value.compare(rhs.value, Qt::CaseInsensitive) == 0;
	}
};

inline size_t qHash(const CaseInsensitiveQString &key, size_t seed = 0) {
	return qHash(key.value.toCaseFolded(), seed);
}

inline std::filesystem::path qstring_to_path(const QString &path) {
#ifdef _WIN32
	return std::filesystem::path(path.toStdWString());
#else
	return std::filesystem::path(path.toStdString());
#endif
}

} // namespace QtUtils
} // namespace Mumble
