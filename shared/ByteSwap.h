

#ifndef MUMBLE_BYTESWAP_H_
#define MUMBLE_BYTESWAP_H_


#if Q_BYTE_ORDER == Q_BIG_ENDIAN
#	define SWAP64(x) (x)
#else
#	if defined(__x86_64__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
#		define SWAP64(x) __builtin_bswap64(x)
#	else
#		include <QtCore/QtEndian>
#		define SWAP64(x) qbswap< quint64 >(x)
#	endif
#endif

#endif
