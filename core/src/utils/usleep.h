#pragma once

#ifdef __linux__
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <unistd.h>
#endif

#ifdef _WIN32
// Needed so this header is self contained: without it every including TU has
// to pull in Windows.h itself before using usleep().
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <Windows.h>
#define usleep(x) ::Sleep(x/1000)
#endif
