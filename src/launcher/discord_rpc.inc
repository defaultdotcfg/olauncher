// ============================================================================
// Minimal Discord Rich Presence client (local IPC pipe, no SDK).
// Frames: uint32 opcode, uint32 length, UTF-8 JSON. op 0 = handshake, op 1 = frame, op 2 = close.
// The large image key "onrush" must be uploaded in the Discord developer portal
// (Rich Presence > Art Assets) to show the picture; without it only the text appears.
// ============================================================================
#include "version.h"
#include <string>
#include <time.h>

static const char *kDiscordDefaultAppId = "1568376204916953188";

struct DiscordRpc
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    int nonce = 0;
    std::string lastError;   // why the last Connect/SetPlaying failed (Discord's own reply when it sent one)

    bool Write(uint32_t op, const std::string &json)
    {
        if (pipe == INVALID_HANDLE_VALUE) return false;
        std::string buf(8 + json.size(), '\0');
        uint32_t len = (uint32_t)json.size();
        memcpy(&buf[0], &op, 4); memcpy(&buf[4], &len, 4); memcpy(&buf[8], json.data(), json.size());
        DWORD wr = 0;
        return WriteFile(pipe, buf.data(), (DWORD)buf.size(), &wr, nullptr) && wr == buf.size();
    }

    // Reads one reply frame (waits up to ~1.5 s) so the pipe never fills up.
    bool ReadReply(std::string *out = nullptr)
    {
        if (pipe == INVALID_HANDLE_VALUE) return false;
        for (int t = 0; t < 30; ++t)
        {
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) return false;
            if (avail >= 8)
            {
                uint32_t hdr[2]; DWORD rd = 0;
                if (!ReadFile(pipe, hdr, 8, &rd, nullptr) || rd != 8) return false;
                std::string body(hdr[1], '\0');
                if (hdr[1] && (!ReadFile(pipe, &body[0], hdr[1], &rd, nullptr) || rd != hdr[1])) return false;
                if (out) *out = body;
                return true;
            }
            Sleep(50);
        }
        return false;
    }

    bool Connect(const char *appId)
    {
        Close();
        for (int i = 0; i < 10 && pipe == INVALID_HANDLE_VALUE; ++i)
        {
            wchar_t name[64];
            swprintf_s(name, L"\\\\.\\pipe\\discord-ipc-%d", i);
            pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (pipe == INVALID_HANDLE_VALUE) { lastError = "Discord não está aberto (o app do Discord para PC precisa estar rodando; o Discord no navegador não aceita Rich Presence)"; return false; }
        std::string hs = std::string("{\"v\":1,\"client_id\":\"") + appId + "\"}";
        std::string reply;
        if (!Write(0, hs) || !ReadReply(&reply)) { lastError = "o Discord não respondeu"; Close(); return false; }
        if (reply.find("READY") == std::string::npos) { lastError = "o Discord recusou a conexão: " + reply.substr(0, 300); Close(); return false; }
        lastError.clear();
        return true;
    }

    bool Connected() const { return pipe != INVALID_HANDLE_VALUE; }

    // false once Discord closed its end (it was quit or restarted)
    bool Alive()
    {
        if (pipe == INVALID_HANDLE_VALUE) return false;
        DWORD avail = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) { Close(); return false; }
        if (avail >= 8) ReadReply();   // drop unsolicited frames so the pipe never fills up
        return true;
    }

    bool SetPlaying(long long startEpoch)
    {
        char json[1024];
        sprintf_s(json,
            "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{"
            "\"details\":\"Jogando ONRUSH\",\"state\":\"RushLauncher v%ls\","
            "\"timestamps\":{\"start\":%lld},"
            "\"assets\":{\"large_image\":\"onrush\",\"large_text\":\"ONRUSH\"}}},\"nonce\":\"%d\"}",
            GetCurrentProcessId(), OLAUNCHER_VERSION, startEpoch, ++nonce);
        std::string reply;
        if (!Write(1, json) || !ReadReply(&reply)) { lastError = "o Discord não respondeu ao SET_ACTIVITY"; Close(); return false; }
        if (reply.find("\"evt\":\"ERROR\"") != std::string::npos) { lastError = "o Discord recusou a atividade: " + reply.substr(0, 300); return false; }
        lastError.clear();
        return true;
    }

    void Clear()
    {
        if (pipe == INVALID_HANDLE_VALUE) return;
        char json[256];
        sprintf_s(json, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu},\"nonce\":\"%d\"}", GetCurrentProcessId(), ++nonce);
        Write(1, json); ReadReply();
    }

    void Close()
    {
        if (pipe != INVALID_HANDLE_VALUE) { CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; }
    }
};

static DiscordRpc g_discord;
