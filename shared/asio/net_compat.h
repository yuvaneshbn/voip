#pragma once

#if __has_include(<asio.hpp>)
#include <asio.hpp>
namespace noxasio = asio;
#define NOX_ASIO_STANDALONE 1
#elif __has_include(<boost/asio.hpp>)
#include <boost/asio.hpp>
namespace noxasio = boost::asio;
#define NOX_ASIO_BOOST 1
#else
#error "Neither standalone Asio nor Boost.Asio headers found."
#endif

