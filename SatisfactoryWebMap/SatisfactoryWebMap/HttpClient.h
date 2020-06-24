#include <Windows.h>
#include <winhttp.h>

#include <string>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <optional>

#include "Utils.h"

#pragma comment(lib, "Winhttp.lib")

std::vector<uint8_t> HttpClient(const char *verb, const std::wstring &url, bool *res = nullptr)
{
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    std::vector<uint8_t> buffer;

    try {
        std::wstring hostname;
        std::wstring path;

        hostname.resize(MAX_PATH);
        path.resize(MAX_PATH * 5);

        URL_COMPONENTS urlComp;
        memset(&urlComp, 0, sizeof(urlComp));
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.lpszHostName = hostname.data();
        urlComp.dwHostNameLength = (DWORD)hostname.size();
        urlComp.lpszUrlPath = path.data();
        urlComp.dwUrlPathLength = (DWORD)path.size();
        urlComp.dwSchemeLength = 1;

        if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &urlComp)) {
            throw std::runtime_error("WinHttpCrackUrl Failed");
        }

        // Use WinHttpOpen to obtain a session handle.
        hSession = WinHttpOpen(L"SatisfactoryWebMap",
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);

        if (!hSession) {
            throw std::runtime_error("WinHttpOpen failed");
        }

        WinHttpSetTimeouts(hSession, 0, 60000, 30000, 30000);

        // Specify an HTTP server.
        hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
        if (!hConnect) {
            throw std::runtime_error("WinHttpConnect failed");
        }

        auto wverb = std::atow(verb);

        // Create an HTTP request handle.
        DWORD dwOpenRequestFlag = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        hRequest = WinHttpOpenRequest(hConnect, wverb.c_str(), urlComp.lpszUrlPath,
                                      NULL, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      dwOpenRequestFlag);
        if (!hRequest) {
            throw std::runtime_error("WinHttpOpenRequest failed");
        }

        // Send a request.
        if (!WinHttpSendRequest(hRequest,
                                WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0,
                                0, 0)) {
            throw std::runtime_error("WinHttpSendRequest failed");
        }

        // End the request.
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            throw std::runtime_error("WinHttpReceiveResponse failed");
        }

        DWORD size = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, NULL,
                            &size, WINHTTP_NO_HEADER_INDEX);

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 0) {
            std::wstring headers;
            headers.resize(size);

            // Now, use WinHttpQueryHeaders to retrieve the header.
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                                    WINHTTP_HEADER_NAME_BY_INDEX,
                                    headers.data(), &size,
                                    WINHTTP_NO_HEADER_INDEX)) {
                buffer.resize(_wtoi(headers.c_str()));
            }
        }

        size_t ptr = 0;
        DWORD  downloaded = 0;
        do {
            size = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &size)) {
                throw std::runtime_error("WinHttpQueryDataAvailable failed");
            }

            if (size == 0) {
                break;
            }

            if (buffer.size() < ptr + size) {
                buffer.resize(ptr + size);
            }

            if (!WinHttpReadData(hRequest, buffer.data() + ptr, size, &downloaded)) {
                throw std::runtime_error("WinHttpReadData failed");
            }

            ptr += downloaded;
        } while (size > 0);

        if (res) {
            *res = true;
        }

    } catch (const std::exception &ex) {
        OutputDebugStringA(ex.what());
        if (res) {
            *res = false;
        }
    }

    // Close any open handles.
    if (hRequest)
        WinHttpCloseHandle(hRequest);
    if (hConnect)
        WinHttpCloseHandle(hConnect);
    if (hSession)
        WinHttpCloseHandle(hSession);

    return buffer;
}
