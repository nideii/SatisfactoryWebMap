#pragma once

#include <string>
#include <stdexcept>
#include <vector>
#include <cstdint>

namespace {
#include <Windows.h>

static inline int _wtoa(const wchar_t *w, char *a, int chars, UINT codepage = CP_THREAD_ACP)
{
    return WideCharToMultiByte(codepage, 0, w, -1, a, (int)(a ? chars : 0), 0, 0);
}

static inline int _atow(const char *a, wchar_t *w, int chars, UINT codepage = CP_THREAD_ACP)
{
    return MultiByteToWideChar(codepage, 0, a, -1, w, (int)(w ? chars : 0));
}
};

class StopWatch
{
public:
    StopWatch() : _stopped(0), _start(0), _end(0)
    {
        QueryPerformanceFrequency(&frequency);
        startCount.QuadPart = 0;
        endCount.QuadPart = 0;

        freqBase = 1.0 / frequency.QuadPart;

        start();
    }

    inline void start()
    {
        _stopped = 0; // reset stop flag
        QueryPerformanceCounter(&startCount);
    }

    inline double stop()
    {
        _stopped = 1; // set timer stopped flag
        QueryPerformanceCounter(&endCount);
        return ms();
    }

    inline double us()
    {
        if (!_stopped) {
            QueryPerformanceCounter(&endCount);
        }

        _start = startCount.QuadPart * 1000000.0 * freqBase;
        _end = endCount.QuadPart * 1000000.0 * freqBase;

        return _end - _start;
    }

    inline double ms()
    {
        return us() * 0.001;
    }

    inline double sec()
    {
        return us() * 0.000001;
    }

private:
    double _start;
    double _end;
    int    _stopped;

    double freqBase;

    LARGE_INTEGER frequency;
    LARGE_INTEGER startCount;
    LARGE_INTEGER endCount;
};

namespace std {

// convert ANSI to utf-16le
static inline std::wstring atow(const std::string &string)
{
    int len = _atow(string.data(), 0, 0);
    wchar_t *buffer = new wchar_t[len];
    memset(buffer, 0, len * sizeof(*buffer));
    _atow(string.data(), buffer, (int)len);
    std::wstring s = buffer;
    delete[] buffer;
    return s;
}

};