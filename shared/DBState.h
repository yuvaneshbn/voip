#ifndef MUMBLE_MURMUR_DBSTATE_H_
#define MUMBLE_MURMUR_DBSTATE_H_

/**
 * Possible states the database can be in
 */
enum class DBState {
	Normal,
	ReadOnly,
};

#endif // MUMBLE_MURMUR_DBSTATE_H_
