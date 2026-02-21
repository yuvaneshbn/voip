#pragma once

#include <QtCore/QString>

class ManualPlugin {
public:
	ManualPlugin() = default;
	bool isValid() const { return false; }
	QString name() const { return QStringLiteral("manual-plugin-stub"); }
};
