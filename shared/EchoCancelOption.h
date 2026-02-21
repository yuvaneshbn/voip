
#ifndef NOX_ECHOCANCELOPTION_H_
#define NOX_ECHOCANCELOPTION_H_

#include <QtCore/QObject>
#include <vector>


/// This enum lists a series of echo cancellation options
/// Each audio backend will determine which option is indeed available to that backend and
/// to the specific device combination
enum class EchoCancelOptionID {
	DISABLED           = 0,
	SPEEX_MIXED        = 1,
	SPEEX_MULTICHANNEL = 2,
	APPLE_AEC          = 3 // Apple's Acoustic Echo Cancellation support for macOS and iOS.
};

struct EchoCancelOption {
	EchoCancelOptionID id;
	QString description;
	QString explanation;

	/// @returns A vector of EchoCancelOption objects in the same order as specified in the
	/// EchoCancelOptionID enum. Thus the enums can be used as an index for the returned vector.
	static const std::vector< EchoCancelOption > &getOptions();
};

#endif // NOX_ECHOCANCELOPTION_H_
