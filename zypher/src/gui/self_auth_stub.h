#pragma once
#include <string>
#include <windows.h>
#include <winhttp.h>
#include <intrin.h>

#pragma comment(lib, "winhttp.lib")

namespace SelfAuth {

    struct api {
        std::string server;
        int port;
        std::string encryption_key;

        api(const std::string& srv, int p) : server(srv), port(p) {
            encryption_key = "zypher_encryption_key_2024";
        }

        std::string get_hwid() {
            int cpuInfo[4] = { 0 };
            __cpuid(cpuInfo, 1);
            char hwid[64];
            std::snprintf(hwid, sizeof(hwid), "%08X%08X", cpuInfo[0], cpuInfo[3]);
            return std::string(hwid);
        }

        std::string http_post(const std::string& path, const std::string& body) {
            HINTERNET hSession = WinHttpOpen(L"Zypher/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS, 0);

            if (!hSession) return "";

            HINTERNET hConnect = WinHttpConnect(hSession,
                std::wstring(server.begin(), server.end()).c_str(),
                port, 0);

            if (!hConnect) {
                WinHttpCloseHandle(hSession);
                return "";
            }

            HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                L"POST",
                std::wstring(path.begin(), path.end()).c_str(),
                NULL,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                port == 443 ? WINHTTP_FLAG_SECURE : 0);

            if (!hRequest) {
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return "";
            }

            std::wstring headers = L"Content-Type: application/json\r\n";
            WinHttpAddRequestHeaders(hRequest, headers.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

            BOOL sent = WinHttpSendRequest(hRequest,
                WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                (LPVOID)body.c_str(), body.length(), body.length(), 0);

            if (!sent) {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return "";
            }

            WinHttpReceiveResponse(hRequest, NULL);

            std::string response;
            DWORD bytesAvailable = 0;
            do {
                bytesAvailable = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) break;
                if (bytesAvailable == 0) break;

                char* buffer = new char[bytesAvailable + 1];
                DWORD bytesRead = 0;
                WinHttpReadData(hRequest, buffer, bytesAvailable, &bytesRead);
                buffer[bytesRead] = '\0';
                response += buffer;
                delete[] buffer;
            } while (bytesAvailable > 0);

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);

            return response;
        }

        std::string discord_info(const std::string& license_key) {
            std::string hwid = get_hwid();

            std::string json_body = "{\"key\":\"" + license_key + "\",\"hwid\":\"" + hwid + "\"}";

            std::string response = http_post("/api/auth/validate", json_body);

            if (response.empty()) return "";

            return response;
        }
    };
}
