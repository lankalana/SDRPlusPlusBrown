// Shared entry point for test binaries that link sdrpp_core.
//
// We provide main() ourselves rather than using Catch2WithMain because the
// process has to be torn down by hand, see below.

#include <catch2/catch_session.hpp>

#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace {
    // KNOWN ISSUE: a process that links sdrpp_core hangs during process exit,
    // after main() has returned and after the executable's own static
    // destructors have run, i.e. somewhere in the core's own teardown. It
    // reproduces with a program that does nothing but call one core function;
    // see tests/tools/link_probe.cpp, which is built by the
    // `sdrpp_core_link_probe` target for exactly that purpose.
    //
    // Until that is fixed, the test binary would hang after reporting its
    // results, which breaks both CTest and Catch2's build-time test discovery.
    // Ending the process without running the exit handlers avoids it. The test
    // results have already been written and flushed at this point.
    [[noreturn]] void exitWithoutTeardown(int code) {
        std::fflush(nullptr);
#ifdef _WIN32
        // TerminateProcess, not exit()/_exit(): those still run DLL_PROCESS_DETACH.
        ::TerminateProcess(::GetCurrentProcess(), (UINT)code);
        for (;;) {}
#else
        ::_exit(code);
#endif
    }
}

int main(int argc, char* argv[]) {
    int result = Catch::Session().run(argc, argv);

    exitWithoutTeardown(result);
}
