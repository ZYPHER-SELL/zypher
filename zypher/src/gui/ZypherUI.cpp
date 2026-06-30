#include "ZypherUI.h"
#include "gui_runtime.h"
#include "Assets/game_images.h"
#include "self_auth_stub.h"

#include <algorithm>

#include <ctime>
#include <tlhelp32.h>
#include <intrin.h>
#include <mutex>
#include <string>
#include <cstdint>

struct PendingAuthState {
    std::mutex   mu;
    int          result  = 0;
    int64_t      expiry  = 0;
    std::string  games;
    std::string  message;
};

inline PendingAuthState& PendingAuth() {
    static PendingAuthState s;
    return s;
}

namespace {

    struct Entitlement {
        std::string game;
        std::string display_name;
        bool        is_active     = false;
        int64_t     seconds_left  = 0;
    };

    struct LogLine { std::string text; bool ok = true; };

    enum class Phase { Login, ContactingServer, WelcomeBack, Home };

    struct State {

        std::string licenseKey;

        Phase  phase            = Phase::Login;
        float  sizeAnim         = 0.0f;
        DWORD  contactStartTick = 0;
        DWORD  welcomeStartTick = 0;

        bool        envPanelOpen = false;
        float       envPanelAnim = 0.0f;
        double      envCopiedAt  = 0.0;
        float       envBtnAnim   = 0.0f;

        float       minBtnAnim   = 0.0f;
        float       closeBtnAnim = 0.0f;

        bool        settingsPanelOpen   = false;
        float       settingsPanelAnim   = 0.0f;
        int         settingsActiveTab   = 0;
        float       settingsBtnAnim     = 0.0f;
        double      settingsCopiedAt    = 0.0;
        int         settingsCopiedField = -1;

        float       settingsTabAnim     = 1.0f;
        int         settingsLastTab     = 0;
        int         settingsTabSlideDir = 1;

        double      sessionStart        = 0.0;

        std::vector<Entitlement> ents;
        std::vector<float>       cardAnim;

        std::vector<ImVec2>      cardOffset;

        std::vector<ImVec2>      cardLastSlot;
        std::vector<char>        cardLastSlotKnown;
        int                      hoverCard = -1;

        int                      pressedCard     = -1;
        bool                     dragActive      = false;
        ImVec2                   pressMouseStart = { 0.0f, 0.0f };
        ImVec2                   dragGrabOffset  = { 0.0f, 0.0f };

        ImVec2                   dragSizeEased   = { 0.0f, 0.0f };

        int                      detailModalEnt   = -1;
        float                    detailModalAnim  = 0.0f;
        float                    detailExpandAnim = 0.0f;

        ImVec2                   detailModalSrcPos  = { 0.0f, 0.0f };
        ImVec2                   detailModalSrcSize = { 0.0f, 0.0f };
        bool                     entsLoading = false;
        std::string              entsError;

        char        licenseKeyInput[40] = {};
        std::string loginError;

        float       licenseKeyAnim      = 0.0f;

        struct InputCaret {
            int    cursorIdx     = 0;
            float  caretX        = 0.0f;
            float  caretXTarget  = 0.0f;
            double blinkResetSec = 0.0;
        };
        InputCaret  discordIdCaret;

        bool        showUserIdHelp     = false;
        float       userIdHelpAnim     = 0.0f;

        struct PairPanel {
            bool        active     = false;
            std::string pair_code;
            std::string verify_url;
            int         expires_in = 0;
            DWORD       startedTick = 0;
            std::string status_text;
            bool        finished   = false;
            bool        succeeded  = false;
        };
        PairPanel   pair;
        std::mutex  pairMu;

        double      bootCheckDemoStartSec = 0.0;

        int    loginHeroIdx     = 0;
        int    loginHeroPrev    = -1;
        double loginHeroSwitchAt = 0.0;

        std::vector<LogLine> log;
    };
    static State g;

    static float  EaseTo(float cur, float target, float speed);
    static double NowSec();

    struct Tex { ID3D11ShaderResourceView* srv = nullptr; int w = 0; int h = 0; bool tried = false; };
    static std::unordered_map<std::string, Tex> g_TexCache;

    static const std::string& AssetsDir() {
        static std::string cached;
        if (!cached.empty()) return cached;
        char exePath[MAX_PATH] = {};
        ::GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string dir = exePath;
        const size_t slash = dir.find_last_of("\\/");
        if (slash != std::string::npos) dir.resize(slash);
        cached = dir + "\\Assets";
        return cached;
    }

    static const Tex& GetGameTexture(const char* id) {
        auto it = g_TexCache.find(id);
        if (it != g_TexCache.end()) return it->second;

        Tex t;
        t.tried = true;

        if (Overlay::g_pd3dDevice) {

            GameImages::Bytes b = GameImages::For(id);
            if (b.data && b.size > 0) {
                ID3D11ShaderResourceView* srv = nullptr;
                int w = 0, h = 0;
                if (Render::LoadTextureFromMemory(b.data, b.size,
                                                    &srv, &w, &h)) {
                    t.srv = srv;
                    t.w   = w;
                    t.h   = h;
                }
            }

            if (!t.srv) {
                static const char* kExts[] = { ".png", ".jpg", ".jpeg" };
                const std::string& dir = AssetsDir();
                for (const char* ext : kExts) {
                    const std::string path = dir + "\\" + id + ext;

                    DWORD attr = ::GetFileAttributesA(path.c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES ||
                        (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                        continue;
                    }
                    ID3D11ShaderResourceView* srv = nullptr;
                    int w = 0, h = 0;
                    if (Render::LoadTextureFromFile(path, &srv, &w, &h)) {
                        t.srv = srv;
                        t.w   = w;
                        t.h   = h;
                        break;
                    }
                }
            }
        }

        g_TexCache[id] = t;
        return g_TexCache[id];
    }

    static void AspectFit(ImVec2 outerA, ImVec2 outerB,
                           float srcAR,
                           ImVec2& innerA, ImVec2& innerB) {
        const float dstW = outerB.x - outerA.x;
        const float dstH = outerB.y - outerA.y;
        if (dstW <= 0.0f || dstH <= 0.0f || srcAR <= 0.0f) {
            innerA = outerA;
            innerB = outerB;
            return;
        }
        const float dstAR = dstW / dstH;
        if (srcAR > dstAR) {

            const float newH = dstW / srcAR;
            const float padY = (dstH - newH) * 0.5f;
            innerA = { outerA.x,           outerA.y + padY };
            innerB = { outerB.x,           outerA.y + padY + newH };
        } else {

            const float newW = dstH * srcAR;
            const float padX = (dstW - newW) * 0.5f;
            innerA = { outerA.x + padX,    outerA.y };
            innerB = { outerA.x + padX + newW, outerB.y };
        }
    }

    static void CoverUVs(int srcW, int srcH, ImVec2 dstSize,
                          float zoom,
                          ImVec2& uvA, ImVec2& uvB) {
        if (srcW <= 0 || srcH <= 0 || dstSize.x <= 0.0f || dstSize.y <= 0.0f) {
            uvA = { 0.0f + zoom, 0.0f + zoom };
            uvB = { 1.0f - zoom, 1.0f - zoom };
            return;
        }
        const float srcAR = (float)srcW / (float)srcH;
        const float dstAR = dstSize.x / dstSize.y;
        if (srcAR > dstAR) {

            const float visW = dstAR / srcAR;
            const float side = (1.0f - visW) * 0.5f;
            const float zx   = zoom * visW;
            uvA = { side + zx,         0.0f + zoom };
            uvB = { (1.0f - side) - zx, 1.0f - zoom };
        } else {

            const float visH = srcAR / dstAR;
            const float side = (1.0f - visH) * 0.5f;
            const float zy   = zoom * visH;
            uvA = { 0.0f + zoom, side + zy         };
            uvB = { 1.0f - zoom, (1.0f - side) - zy };
        }
    }

    static ID3D11ShaderResourceView* g_WallpaperSrv  = nullptr;
    static int                       g_WallpaperW    = 0;
    static int                       g_WallpaperH    = 0;
    static bool                      g_WallpaperTried = false;

    static void TryLoadWallpaper() {
        if (g_WallpaperTried) return;
        g_WallpaperTried = true;
        if (!Overlay::g_pd3dDevice) return;

        wchar_t wpath[MAX_PATH] = {};
        if (!::SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, wpath, 0))
            return;
        if (!wpath[0]) return;

        char path[MAX_PATH * 4] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, wpath, -1,
                                path, sizeof(path) - 1,
                                nullptr, nullptr);

        Render::LoadTextureFromFile(path, &g_WallpaperSrv,
                                     &g_WallpaperW, &g_WallpaperH);
    }

    static void ReleaseWallpaper() {
        if (g_WallpaperSrv) { g_WallpaperSrv->Release(); g_WallpaperSrv = nullptr; }
        g_WallpaperW = 0; g_WallpaperH = 0; g_WallpaperTried = false;
    }

    static void CoverUVs(int srcW, int srcH, ImVec2 dstSize,
                         float zoom, ImVec2& uvA, ImVec2& uvB);

    static void DrawWallpaperBackground(ImDrawList* dl,
                                         ImVec2 pos, ImVec2 size,
                                         float darken) {
        TryLoadWallpaper();
        const ImVec2 b = { pos.x + size.x, pos.y + size.y };

        if (g_WallpaperSrv && g_WallpaperW > 0 && g_WallpaperH > 0) {
            ImVec2 uvA, uvB;
            CoverUVs(g_WallpaperW, g_WallpaperH, size, 0.0f, uvA, uvB);
            dl->AddImage(reinterpret_cast<ImTextureID>(g_WallpaperSrv),
                          pos, b, uvA, uvB, IM_COL32_WHITE);
        } else {

            dl->AddRectFilledMultiColor(
                pos, b,
                IM_COL32(18, 10, 30, 255), IM_COL32(18, 10, 30, 255),
                IM_COL32(10, 6, 20, 255), IM_COL32(10, 6, 20, 255));
        }

        const BYTE darkA = (BYTE)((darken < 0.0f ? 0.0f
                                    : (darken > 1.0f ? 1.0f : darken)) * 255.0f);
        if (darkA > 0)
            dl->AddRectFilled(pos, b, IM_COL32(0, 0, 0, darkA));
    }

    static ID3D11ShaderResourceView* g_AvatarSrv  = nullptr;
    static int                       g_AvatarW    = 0;
    static int                       g_AvatarH    = 0;

    static std::atomic<int>          g_AvatarState{ 0 };
    static std::vector<unsigned char> g_AvatarBytes;
    static std::mutex                 g_AvatarBytesMu;

    static std::string               g_DiscordUsername;
    static std::string               g_DiscordGlobalName;

    static std::atomic<int>          g_LinkStatus{ 0 };

    static std::vector<std::pair<std::string, long long>> g_LinkedGames;
    static std::mutex                                     g_LinkedGamesMu;

    static void TryLoadDiscordAvatar() { return; }

#if 0
    static void TryLoadDiscordAvatar_LEGACY_UNUSED() {
        const int state = g_AvatarState.load();
        if (state == 3) return;

        if (state == 2) {

            if (!Overlay::g_pd3dDevice) return;
            std::lock_guard<std::mutex> lk(g_AvatarBytesMu);
            if (g_AvatarBytes.empty()) {
                g_AvatarState.store(0);
                return;
            }
            const bool ok = Render::LoadTextureFromMemory(
                g_AvatarBytes.data(), g_AvatarBytes.size(),
                &g_AvatarSrv, &g_AvatarW, &g_AvatarH);
            g_AvatarBytes.clear();
            g_AvatarBytes.shrink_to_fit();
            g_AvatarState.store(ok ? 3 : 0);
            return;
        }

        if (state == 1) return;

        if (g.licenseKey.empty()) return;

        const std::string idCopy = g.licenseKey;
        g_AvatarState.store(1);

        std::thread([idCopy]() {
            uint64_t snowflake = 0;
            try { snowflake = std::stoull(idCopy); }
            catch (...) { g_AvatarState.store(0); return; }

            SelfAuth::api auth("zypher.me", 443);
            const std::string body = auth.discord_info(idCopy);

            if (body.empty()) {
                g_LinkStatus.store(3);
                g_AvatarState.store(0);
                return;
            }

            auto scrape = [&](const char* key) -> std::string {
                const std::string needle = std::string("\"") + key + "\":\"";
                const size_t k = body.find(needle);
                if (k == std::string::npos) return {};
                const size_t s = k + needle.size();
                const size_t e = body.find('"', s);
                if (e == std::string::npos) return {};
                return body.substr(s, e - s);
            };

            if (body.find("\"linked\":false") != std::string::npos) {
                g_LinkStatus.store(2);
                g_AvatarState.store(0);
                return;
            }

            const std::string hash        = scrape("avatar_hash");
            const std::string username    = scrape("username");
            const std::string global_name = scrape("global_name");

            std::vector<std::pair<std::string, long long>> linkedGames;
            {
                const std::string gNeedle = "\"games\":[";
                size_t k = body.find(gNeedle);
                if (k != std::string::npos) {
                    const size_t s = k + gNeedle.size();
                    const size_t e = body.find(']', s);
                    if (e != std::string::npos) {
                        size_t p = s;
                        while (p < e) {
                            const size_t q1 = body.find('"', p);
                            if (q1 == std::string::npos || q1 >= e) break;
                            const size_t q2 = body.find('"', q1 + 1);
                            if (q2 == std::string::npos || q2 >= e) break;
                            linkedGames.emplace_back(
                                body.substr(q1 + 1, q2 - q1 - 1), 0LL);
                            p = q2 + 1;
                        }
                    }
                }

                const std::string eNeedle = "\"expiry\":{";
                size_t ke = body.find(eNeedle);
                if (ke != std::string::npos) {
                    const size_t s = ke + eNeedle.size();
                    const size_t e = body.find('}', s);
                    if (e != std::string::npos) {
                        size_t p = s;
                        while (p < e) {
                            const size_t q1 = body.find('"', p);
                            if (q1 == std::string::npos || q1 >= e) break;
                            const size_t q2 = body.find('"', q1 + 1);
                            if (q2 == std::string::npos || q2 >= e) break;
                            const std::string gid = body.substr(q1 + 1, q2 - q1 - 1);
                            const size_t colon = body.find(':', q2);
                            if (colon == std::string::npos || colon >= e) break;
                            size_t ns = colon + 1;
                            while (ns < e && (body[ns] == ' ' || body[ns] == '\t')) ++ns;
                            size_t ne = ns;
                            while (ne < e
                                   && (body[ne] == '-'
                                       || (body[ne] >= '0' && body[ne] <= '9'))) {
                                ++ne;
                            }
                            long long ts = 0;
                            try { ts = std::stoll(body.substr(ns, ne - ns)); }
                            catch (...) {}

                            for (auto& row : linkedGames) {
                                if (row.first == gid) { row.second = ts; break; }
                            }
                            p = ne;
                        }
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lk(g_LinkedGamesMu);
                g_LinkedGames = std::move(linkedGames);
            }

            g_DiscordUsername   = username;
            g_DiscordGlobalName = global_name;
            g_LinkStatus.store(1);

            wchar_t cdnPath[256];
            if (!hash.empty()) {
                ::swprintf(cdnPath, 256,
                            L"/avatars/%hs/%hs.png?size=128",
                            idCopy.c_str(), hash.c_str());
            } else {
                const int idx = (int)((snowflake >> 22) % 6);
                ::swprintf(cdnPath, 256, L"/embed/avatars/%d.png", idx);
            }

            auto bytes = HttpsGetBytes(L"cdn.discordapp.com", cdnPath);
            if (bytes.empty()) {
                g_AvatarState.store(0);
                return;
            }
            {
                std::lock_guard<std::mutex> lk(g_AvatarBytesMu);
                g_AvatarBytes = std::move(bytes);
            }

            g_AvatarState.store(2, std::memory_order_release);
        }).detach();
    }
#endif

    static void ReleaseAvatar() {
        if (g_AvatarSrv) { g_AvatarSrv->Release(); g_AvatarSrv = nullptr; }
        g_AvatarW = 0;
        g_AvatarH = 0;
        std::lock_guard<std::mutex> lk(g_AvatarBytesMu);
        g_AvatarBytes.clear();
        g_AvatarState.store(0);
    }

    static void Log(const std::string& s, bool ok = true) {
        if (g.log.size() > 500) g.log.erase(g.log.begin(), g.log.begin() + 100);
        g.log.push_back({ s, ok });
    }

    static ImVec4 ToImVec4(const Color& c) {
        return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
    }

    static ImU32 ToImU32(const Color& c) {
        return IM_COL32(c.r, c.g, c.b, c.a);
    }

    static void PushTitleFont() {
        if (Render::Fonts::montserratBold16px) ImGui::PushFont(Render::Fonts::montserratBold16px);
    }
    static void PushBodyFont() {
        if (Render::Fonts::montserrat14px) ImGui::PushFont(Render::Fonts::montserrat14px);
    }
    static void PushBigFont() {
        if (Render::Fonts::montserratBold16px) ImGui::PushFont(Render::Fonts::montserratBold16px);
    }
    static void PushIconFont() {
        if (Render::Fonts::fontAwesome14px) ImGui::PushFont(Render::Fonts::fontAwesome14px);
    }
    static void PopFont() { ImGui::PopFont(); }

    static void DrawFAIcon(ImDrawList* dl, ImVec2 c, float r,
                            ImU32 col, ImWchar codepoint,
                            float rotation = 0.0f) {
        ImFont* fa = Render::Fonts::fontAwesome14px;
        if (!fa) return;
        const float renderSize = r * 2.0f;
        ImFontBaked* baked = fa->GetFontBaked(renderSize);
        if (!baked) return;
        const ImFontGlyph* g = baked->FindGlyph(codepoint);
        if (!g) return;
        const float qX0 = g->X0, qY0 = g->Y0;
        const float qX1 = g->X1, qY1 = g->Y1;
        const float midX = (qX0 + qX1) * 0.5f;
        const float midY = (qY0 + qY1) * 0.5f;
        const ImVec2 cornersLocal[4] = {
            ImVec2(qX0 - midX, qY0 - midY),
            ImVec2(qX1 - midX, qY0 - midY),
            ImVec2(qX1 - midX, qY1 - midY),
            ImVec2(qX0 - midX, qY1 - midY),
        };
        const float cs = std::cosf(rotation);
        const float sn = std::sinf(rotation);
        const float pivotX = (rotation == 0.0f) ? std::floor(c.x + 0.5f) : c.x;
        const float pivotY = (rotation == 0.0f) ? std::floor(c.y + 0.5f) : c.y;
        ImVec2 p[4];
        for (int i = 0; i < 4; ++i) {
            p[i] = ImVec2(
                pivotX + cornersLocal[i].x * cs - cornersLocal[i].y * sn,
                pivotY + cornersLocal[i].x * sn + cornersLocal[i].y * cs);
        }
        const ImVec2 uv[4] = {
            ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V0),
            ImVec2(g->U1, g->V1), ImVec2(g->U0, g->V1),
        };
        dl->AddImageQuad(ImGui::GetIO().Fonts->TexRef,
                          p[0], p[1], p[2], p[3],
                          uv[0], uv[1], uv[2], uv[3], col);
    }

    static void DrawGearIcon(ImDrawList* dl, ImVec2 c, float r,
                              ImU32 col, float rotation = 0.0f) {
        ImFont* fa = Render::Fonts::fontAwesome14px;
        if (!fa) return;
        static constexpr ImWchar kGearCodepoint = 0xf013;

        const float renderSize = r * 2.0f;
        ImFontBaked* baked = fa->GetFontBaked(renderSize);
        if (!baked) return;
        const ImFontGlyph* g = baked->FindGlyph(kGearCodepoint);
        if (!g) return;

        const float qX0 = g->X0, qY0 = g->Y0;
        const float qX1 = g->X1, qY1 = g->Y1;
        const float midX = (qX0 + qX1) * 0.5f;
        const float midY = (qY0 + qY1) * 0.5f;

        const ImVec2 cornersLocal[4] = {
            ImVec2(qX0 - midX, qY0 - midY),
            ImVec2(qX1 - midX, qY0 - midY),
            ImVec2(qX1 - midX, qY1 - midY),
            ImVec2(qX0 - midX, qY1 - midY),
        };

        const float cs = std::cosf(rotation);
        const float sn = std::sinf(rotation);

        const float pivotX = (rotation == 0.0f) ? std::floor(c.x + 0.5f) : c.x;
        const float pivotY = (rotation == 0.0f) ? std::floor(c.y + 0.5f) : c.y;

        ImVec2 p[4];
        for (int i = 0; i < 4; ++i) {
            p[i] = ImVec2(
                pivotX + cornersLocal[i].x * cs - cornersLocal[i].y * sn,
                pivotY + cornersLocal[i].x * sn + cornersLocal[i].y * cs);
        }

        const ImVec2 uv[4] = {
            ImVec2(g->U0, g->V0),
            ImVec2(g->U1, g->V0),
            ImVec2(g->U1, g->V1),
            ImVec2(g->U0, g->V1),
        };

        dl->AddImageQuad(ImGui::GetIO().Fonts->TexRef,
                          p[0], p[1], p[2], p[3],
                          uv[0], uv[1], uv[2], uv[3],
                          col);
    }

    static std::string GameDisplay(const Entitlement& e) {
        if (!e.display_name.empty()) return e.display_name;
        if (e.game == "rust")      return "Rust";
        if (e.game == "dbd")       return "Dead by Daylight";
        if (e.game == "apex")      return "Apex Legends";
        if (e.game == "dayz")      return "DayZ";
        if (e.game == "offscript") return "Offscript";
        std::string out = e.game;
        if (!out.empty()) out[0] = (char)::toupper((unsigned char)out[0]);
        return out;
    }

    struct BadgePill { std::string label; Color bg; Color text; };
    static BadgePill ExpiryPill(const Entitlement& e) {
        if (!e.is_active && e.seconds_left == 0)
            return { "Required", Color(220, 38, 38), Color(255, 255, 255) };
        if (e.seconds_left < 0)
            return { "NONE",     Color(64, 64, 72), Color(170, 160, 200) };
        int days = (int)(e.seconds_left / 86400);
        if (days <= 0) {
            int hours = (int)(e.seconds_left / 3600);
            if (hours <= 0) return { "<1h",        Color(220, 38, 38),  Color(255, 255, 255) };
            return { std::to_string(hours) + "h",  Color(220, 38, 38),  Color(255, 255, 255) };
        }
        if (days == 1)  return { "1 day",                    Color(220, 38, 38),  Color(255, 255, 255) };
        if (days <= 7)  return { std::to_string(days) + " days", Color(234, 88, 12),  Color(255, 255, 255) };
        return            { std::to_string(days) + " days", Color(22, 163, 74),  Color(255, 255, 255) };
    }

    static void RefreshEntitlements() {
        g.ents.clear();
        g.entsError.clear();
        g.entsLoading = false;

        std::vector<std::pair<std::string, long long>> linked;
        {
            std::lock_guard<std::mutex> lk(g_LinkedGamesMu);
            linked = g_LinkedGames;
        }

        const long long now = (long long)::time(nullptr);
        for (const auto& gid_exp : linked) {
            const std::string& gid = gid_exp.first;
            const long long    exp = gid_exp.second;

            long long seconds_left;
            if (exp <= 0)            seconds_left = -1;
            else if (exp <= now)     continue;
            else                     seconds_left = exp - now;

            g.ents.push_back({ gid, /*display_name=*/"",
                                /*is_active=*/true, seconds_left });
        }
    }

    static void ApplyDarkTheme() {
        auto& s = ImGui::GetStyle();
        s.WindowRounding = 0.0f;
        s.FrameRounding  = 6.0f;
        s.PopupRounding  = 8.0f;
        s.ChildRounding  = 12.0f;
        s.GrabRounding   = Style::rounding;
        s.WindowPadding  = ImVec2(0, 0);
        s.ItemSpacing    = ImVec2(8, 8);
        s.FramePadding   = ImVec2(10, 6);

        auto& c = s.Colors;
        c[ImGuiCol_WindowBg]       = ToImVec4(Style::background);
        c[ImGuiCol_ChildBg]        = ToImVec4(Style::container);
        c[ImGuiCol_PopupBg]        = ToImVec4(Style::container);
        c[ImGuiCol_Border]         = ToImVec4(Style::outline);
        c[ImGuiCol_FrameBg]        = ToImVec4(Style::widgetBackground);
        c[ImGuiCol_FrameBgHovered] = ToImVec4(Style::widgetBackgroundHovered);
        c[ImGuiCol_FrameBgActive]  = ToImVec4(Style::widgetBackgroundHovered);
        c[ImGuiCol_Button]         = ToImVec4(Style::widgetBackground);
        c[ImGuiCol_ButtonHovered]  = ToImVec4(Style::accentColor);
        c[ImGuiCol_ButtonActive]   = ToImVec4(Style::accentColor);
        c[ImGuiCol_CheckMark]      = ToImVec4(Style::accentColor);
        c[ImGuiCol_SliderGrab]     = ToImVec4(Style::accentColor);
        c[ImGuiCol_Header]         = ToImVec4(Style::widgetBackground);
        c[ImGuiCol_HeaderHovered]  = ToImVec4(Style::widgetBackgroundHovered);
        c[ImGuiCol_HeaderActive]   = ToImVec4(Style::accentColor);
        c[ImGuiCol_Text]           = ToImVec4(Style::text);
        c[ImGuiCol_TextDisabled]   = ToImVec4(Style::dimmedText);
        c[ImGuiCol_Separator]      = ToImVec4(Style::outline);
    }

    static void TryLogin() {
        std::string id = g.licenseKeyInput;
        while (!id.empty() && std::isspace((unsigned char)id.front()))
            id.erase(id.begin());
        while (!id.empty() && std::isspace((unsigned char)id.back()))
            id.pop_back();

        if (id.empty()) {
            g.loginError = "Enter your license key.";
            return;
        }

        ReleaseAvatar();
        g_LinkStatus.store(0);
        {
            std::lock_guard<std::mutex> lk(g_LinkedGamesMu);
            g_LinkedGames.clear();
        }

        {
            auto& pa = ::PendingAuth();
            std::lock_guard<std::mutex> lk(pa.mu);
            pa.result = 0;
            pa.expiry = 0;
            pa.games.clear();
            pa.message.clear();
        }

        g.licenseKey         = id;
        g.phase             = Phase::ContactingServer;
        g.contactStartTick  = ::GetTickCount();
        g.loginError.clear();
        std::memset(g.licenseKeyInput, 0, sizeof(g.licenseKeyInput));

        std::thread([idCopy = id]() {
            SelfAuth::api auth("localhost", 3000);
            const std::string response = auth.discord_info(idCopy);

            auto& pa = ::PendingAuth();
            std::lock_guard<std::mutex> lk(pa.mu);

            if (response.empty()) {
                pa.result = 2;
                pa.message = "Auth server unreachable.";
                return;
            }

            if (response.find("\"success\":true") != std::string::npos ||
                response.find("\"success\": true") != std::string::npos) {
                pa.result = 1;
                pa.expiry = -1;
                pa.games = "zypher";
                pa.message.clear();
            } else {
                pa.result = 2;
                if (response.find("HWID mismatch") != std::string::npos) {
                    pa.message = "HWID mismatch. Contact support.";
                } else if (response.find("expired") != std::string::npos) {
                    pa.message = "License expired.";
                } else {
                    pa.message = "Invalid license key.";
                }
            }
        }).detach();

        gui_runtime::LoginReply reply;
        reply.action   = gui_runtime::LoginReply::Action::SignIn;
        reply.email    = id;
        reply.password = "";
        gui_runtime::DeliverLoginReply(reply);
    }

    static void DoLogout() {
        g.phase = Phase::Login;
        g.licenseKey.clear();
        g.ents.clear();
        std::memset(g.licenseKeyInput, 0, sizeof(g.licenseKeyInput));
        g.loginError.clear();

        ReleaseAvatar();
        g_LinkStatus.store(0);
        {
            std::lock_guard<std::mutex> lk(g_LinkedGamesMu);
            g_LinkedGames.clear();
        }
        g_DiscordUsername.clear();
        g_DiscordGlobalName.clear();

        Log("Signed out.");
    }

    constexpr int kLoginWinW    = 480;
    constexpr int kLoginWinH    = 620;
    constexpr int kHomeWinW     = 1100;
    constexpr int kHomeWinH     = 680;

    constexpr int kDetailPanelW = 320;

    static void DriveWindowSize() {
        if (!Memory::hwnd) return;

        const float phaseTarget = (g.phase == Phase::Login) ? 0.0f : 1.0f;
        g.sizeAnim = EaseTo(g.sizeAnim, phaseTarget, 4.0f);
        const float u = g.sizeAnim;
        const float t = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);

        const float detailTarget = (g.detailModalEnt >= 0) ? 1.0f : 0.0f;
        g.detailExpandAnim = EaseTo(g.detailExpandAnim, detailTarget, 4.0f);
        const float du = g.detailExpandAnim;
        const float dt = 1.0f - (1.0f - du) * (1.0f - du) * (1.0f - du);
        const int sideExpand = (int)(kDetailPanelW * dt);

        const int w = (int)(kLoginWinW + (kHomeWinW - kLoginWinW) * t) + sideExpand;
        const int h = (int)(kLoginWinH + (kHomeWinH - kLoginWinH) * t);

        static int s_lastW = -1, s_lastH = -1;
        if (w == s_lastW && h == s_lastH) {
            return;
        }

        constexpr float kEps = 0.001f;
        const bool phaseSettled = fabsf(g.sizeAnim - phaseTarget) < kEps;

        int x, y;
        if (!phaseSettled) {
            const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
            const int screenH = ::GetSystemMetrics(SM_CYSCREEN);
            x = (screenW - w) / 2;
            y = (screenH - h) / 2;
        } else {
            RECT rc{};
            ::GetWindowRect(Memory::hwnd, &rc);
            x = rc.left;
            y = rc.top;
        }

        ::SetWindowPos(Memory::hwnd, nullptr, x, y, w, h,
                        SWP_NOZORDER | SWP_NOACTIVATE);
        s_lastW = w;
        s_lastH = h;
    }

    constexpr DWORD kContactTimeoutMs = 75000;

    static void UpdateContactPhase() {
        if (g.phase != Phase::ContactingServer) return;

        int             result  = 0;
        int64_t         expiry  = 0;
        std::string     games;
        std::string     message;
        {
            auto& pa = ::PendingAuth();
            std::lock_guard<std::mutex> lk(pa.mu);
            if (pa.result != 0) {
                result  = pa.result;
                expiry  = pa.expiry;
                games   = pa.games;
                message = pa.message;
                pa.result = 0;
                pa.expiry = 0;
                pa.games.clear();
                pa.message.clear();
            }
        }

        if (result == 1) {
            std::vector<std::string> gameList;
            {
                std::string s = games;
                size_t pos = 0;
                while ((pos = s.find(',')) != std::string::npos) {
                    std::string tok = s.substr(0, pos);
                    if (!tok.empty()) gameList.push_back(tok);
                    s.erase(0, pos + 1);
                }
                if (!s.empty()) gameList.push_back(s);
            }
            const long long now = (long long)::time(nullptr);
            long long secondsLeft;
            if (expiry <= 0)            secondsLeft = -1;
            else if (expiry <= now)     secondsLeft = 0;
            else                        secondsLeft = expiry - now;

            g.ents.clear();
            for (auto& gid : gameList) {
                g.ents.push_back({ gid, std::string{}, true, secondsLeft });
            }
            Log("Signed in.");
            g.phase            = Phase::WelcomeBack;
            g.welcomeStartTick = ::GetTickCount();
            return;
        }
        if (result == 2) {
            g.loginError = message.empty() ? std::string("Invalid license key.") : message;
            g.phase      = Phase::Login;
            g.licenseKey.clear();
            return;
        }

        const DWORD elapsed = ::GetTickCount() - g.contactStartTick;
        if (elapsed > kContactTimeoutMs) {
            g.loginError = "Auth server unreachable.";
            g.phase      = Phase::Login;
            g.licenseKey.clear();
        }
    }

    constexpr DWORD kWelcomeDurationMs = 2000;

    static void UpdateWelcomePhase() {
        if (g.phase != Phase::WelcomeBack) return;
        const DWORD elapsed = ::GetTickCount() - g.welcomeStartTick;
        if (elapsed >= kWelcomeDurationMs) {
            g.phase = Phase::Home;
        }
    }

    static void DrawContactingScreen() {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##contacting", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar| ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        DrawWallpaperBackground(dl, {0, 0}, disp, 0.72f);
        const float t = (float)ImGui::GetTime();

        const float cx     = disp.x * 0.5f;
        const float cy     = disp.y * 0.44f;
        const float radius = 24.0f;
        const float stroke = 3.0f;

        dl->AddCircle({cx, cy}, radius,
                       IM_COL32(255, 255, 255, 38), 64, stroke);

        const float startAngle = t * 5.0f;
        const float endAngle   = startAngle + 3.84f;
        const float pulse      = 0.90f + 0.10f * sinf(t * 3.0f);
        Color acc = Style::accentColor;
        const ImU32 arcCol = IM_COL32(acc.r, acc.g, acc.b,
                                       (BYTE)(255.0f * pulse));
        dl->PathArcTo({cx, cy}, radius, startAngle, endAngle, 32);
        dl->PathStroke(arcCol, 0, stroke);

        const int dotCount = ((int)(t * 2.0f) % 3) + 1;
        char caption[64];
        std::snprintf(caption, sizeof(caption),
                       "Connecting%s",
                       dotCount == 1 ? "."   :
                       dotCount == 2 ? ".."  : "...");

        PushBodyFont();
        const ImVec2 ts = ImGui::CalcTextSize(caption);
        dl->AddText({cx - ts.x * 0.5f, cy + radius + 28.0f},
                    IM_COL32(220, 210, 240, 255), caption);
        PopFont();

        if (!g.licenseKey.empty()) {
            char sub[96];
            const std::string& id = g.licenseKey;

            if (id.size() >= 10) {
                std::snprintf(sub, sizeof(sub),
                               "Key %.4s…%s",
                               id.c_str(),
                               id.c_str() + id.size() - 4);
            } else {
                std::snprintf(sub, sizeof(sub), "Key %s", id.c_str());
            }
            PushBodyFont();
            const ImVec2 ss = ImGui::CalcTextSize(sub);
            dl->AddText({cx - ss.x * 0.5f, cy + radius + 52.0f},
                        IM_COL32(130, 120, 160, 255), sub);
            PopFont();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    static void DrawWelcomeScreen() {

        TryLoadDiscordAvatar();

        const ImVec2 disp    = ImGui::GetIO().DisplaySize;
        const DWORD  elapsed = ::GetTickCount() - g.welcomeStartTick;
        const float  t       = (float)elapsed / (float)kWelcomeDurationMs;

        auto smoothstep = [](float a, float b, float x) {
            const float u  = (x - a) / (b - a);
            const float cl = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            return cl * cl * (3.0f - 2.0f * cl);
        };

        const float headIn  = smoothstep(0.00f, 0.28f, t);
        const float subIn   = smoothstep(0.18f, 0.45f, t);
        const float exitOut = smoothstep(0.72f, 1.00f, t);
        const float global  = 1.0f - exitOut;

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##welcome", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize   |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        DrawWallpaperBackground(dl, {0, 0}, disp, 0.72f);
        const float cx = disp.x * 0.5f;
        const float cy = disp.y * 0.46f;

        {
            PushBigFont();
            ImFont*     font     = ImGui::GetFont();
            const float baseSize = ImGui::GetFontSize();
            const char* head     = "Welcome back";

            const float slide = (1.0f - headIn) * 12.0f;
            const float scale = 0.97f + headIn * 0.03f;
            const float size  = baseSize * scale;
            const ImVec2 ts   = font->CalcTextSizeA(size, FLT_MAX, 0.0f, head);

            const float hx = cx - ts.x * 0.5f;
            const float hy = cy - ts.y * 0.5f - 8.0f + slide;
            const BYTE  a  = (BYTE)(255.0f * headIn * global);
            dl->AddText(font, size, {hx, hy},
                        IM_COL32(240, 230, 255, a), head);
            PopFont();
        }

        std::string subStr;
        if (g_AvatarState.load(std::memory_order_acquire) >= 2) {
            subStr = !g_DiscordGlobalName.empty()
                     ? g_DiscordGlobalName
                     : g_DiscordUsername;
        }
        if (subStr.empty() && !g.licenseKey.empty()) {
            const std::string& id = g.licenseKey;
            char buf[96];
            if (id.size() >= 10) {
                std::snprintf(buf, sizeof(buf),
                               "Key %.4s…%s",
                               id.c_str(),
                               id.c_str() + id.size() - 4);
            } else {
                std::snprintf(buf, sizeof(buf), "Key %s", id.c_str());
            }
            subStr = buf;
        }

        if (!subStr.empty()) {
            const char* sub = subStr.c_str();
            PushBodyFont();
            const float slide = (1.0f - subIn) * 8.0f;
            const ImVec2 ts   = ImGui::CalcTextSize(sub);
            const BYTE   a    = (BYTE)(165.0f * subIn * global);
            dl->AddText({cx - ts.x * 0.5f, cy + 28.0f + slide},
                        IM_COL32(160, 148, 195, a), sub);
            PopFont();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    static void DrawLoginWindowChrome(ImDrawList* dl, float winRightX, float winTopY) {
        const float ctrlW = 36.0f;
        const float ctrlH = 28.0f;
        const ImVec2 closeA = { winRightX - ctrlW, winTopY };
        const ImVec2 closeB = { winRightX,         winTopY + ctrlH };
        const ImVec2 minA   = { closeA.x - ctrlW,  winTopY };
        const ImVec2 minB   = { closeA.x,          winTopY + ctrlH };

        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool minHov   = mp.x >= minA.x && mp.x <= minB.x &&
                              mp.y >= minA.y && mp.y <= minB.y;
        const bool closeHov = mp.x >= closeA.x && mp.x <= closeB.x &&
                              mp.y >= closeA.y && mp.y <= closeB.y;

        g.minBtnAnim   = EaseTo(g.minBtnAnim,   minHov   ? 1.0f : 0.0f, 18.0f);
        g.closeBtnAnim = EaseTo(g.closeBtnAnim, closeHov ? 1.0f : 0.0f, 18.0f);

        if (g.minBtnAnim > 0.001f) {
            const BYTE alpha = (BYTE)(36.0f * g.minBtnAnim);
            dl->AddRectFilled(minA, minB, IM_COL32(255, 255, 255, alpha));
        }
        if (g.closeBtnAnim > 0.001f) {
            const BYTE alpha = (BYTE)(220.0f * g.closeBtnAnim);
            dl->AddRectFilled(closeA, closeB, IM_COL32(232, 17, 35, alpha));
        }

        {
            const float lineW = 12.0f;
            const float cx = (minA.x + minB.x) * 0.5f;
            const float cy = minA.y + ctrlH * 0.5f;
            dl->AddLine({cx - lineW * 0.5f, cy}, {cx + lineW * 0.5f, cy},
                         IM_COL32(220, 210, 240, 255), 1.0f);
        }

        {
            const float xS = 5.0f;
            const float cx = (closeA.x + closeB.x) * 0.5f;
            const float cy = closeA.y + ctrlH * 0.5f;
            const ImU32 col = closeHov ? IM_COL32_WHITE
                                        :                     IM_COL32(220, 210, 240, 255);
            dl->AddLine({cx - xS, cy - xS}, {cx + xS, cy + xS}, col, 1.2f);
            dl->AddLine({cx - xS, cy + xS}, {cx + xS, cy - xS}, col, 1.2f);
        }
        if (ImGui::IsMouseClicked(0)) {
            if (minHov && Memory::hwnd) {
                ::ShowWindow(Memory::hwnd, SW_MINIMIZE);
            } else if (closeHov && Memory::hwnd) {
                ::PostMessageW(Memory::hwnd, WM_CLOSE, 0, 0);
            }
        }
    }

    static int InputCaretCaptureCb(ImGuiInputTextCallbackData* data) {
        auto* caret = (decltype(g.discordIdCaret)*)data->UserData;
        if (caret) caret->cursorIdx = data->CursorPos;
        return 0;
    }

    static bool IconInput(const char* id, const char* hint,
                           char* buf, size_t bufSize, ImWchar icon,
                           float width, float height,
                           float& anim,
                           decltype(g.discordIdCaret)* caret,
                           bool error = false,
                           ImGuiInputTextFlags flags = 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos  = ImGui::GetCursorScreenPos();
        const ImVec2 boxB = { pos.x + width, pos.y + height };

        const ImVec2 savedCursor = ImGui::GetCursorPos();
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(id);
        const bool boxClicked = ImGui::InvisibleButton("##box", {width, height});
        const bool boxHov     = ImGui::IsItemHovered();
        ImGui::PopID();
        ImGui::SetCursorPos(savedCursor);
        if (boxClicked) ImGui::SetKeyboardFocusHere(0);

        ImGuiID textId = ImGui::GetCurrentWindow()->GetID(id);
        const bool focused = (ImGui::GetActiveID() == textId);

        const float target = focused ? 1.0f : (boxHov ? 0.55f : 0.0f);
        anim = EaseTo(anim, target, 16.0f);

        auto lerp = [](BYTE a, BYTE b, float t) -> BYTE {
            return (BYTE)((float)a + ((float)b - (float)a) * t);
        };
        ImU32 bgCol;
        if (error) {

            const BYTE r = lerp(70, 96, anim);
            const BYTE g = lerp(28, 36, anim);
            const BYTE b = lerp(36, 44, anim);
            bgCol = IM_COL32(r, g, b, 255);
        } else {
            const BYTE r = lerp(40, 60, anim);
            const BYTE g = lerp(40, 60, anim);
            const BYTE b = lerp(50, 76, anim);
            bgCol = IM_COL32(r, g, b, 255);
        }
        dl->AddRectFilled(pos, boxB, bgCol, 10.0f);

        const Color acc = Style::accentColor;
        ImU32 ringCol;
        if (error) {
            ringCol = IM_COL32(220, 70, 70, (BYTE)(200 + 55 * anim));
        } else {
            const BYTE a = (BYTE)(220.0f * anim);
            ringCol = IM_COL32(acc.r, acc.g, acc.b, a);
        }
        if (((ringCol >> 24) & 0xFF) > 1) {
            dl->AddRect(pos, boxB, ringCol, 10.0f, 0, 1.6f);
        }

        const float iconR  = height * 0.28f;
        const float iconCx = pos.x + 18.0f;
        const float iconCy = pos.y + height * 0.5f;
        const BYTE iconR8  = lerp(150, 235, anim);
        const ImU32 iconCol = error
            ? IM_COL32(230, 110, 110, 255)
            : IM_COL32(iconR8, iconR8, (BYTE)(iconR8 + 8), 255);
        DrawFAIcon(dl, {iconCx, iconCy}, iconR, iconCol, icon);

        const float textLeft = iconCx + iconR + 10.0f;
        const float padY     = (height - ImGui::GetTextLineHeight()) * 0.5f;
        const ImVec2 textOrigin = { textLeft, pos.y + padY };
        ImGui::SetCursorScreenPos(textOrigin);

        ImGui::PushStyleColor(ImGuiCol_FrameBg,         IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,   IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg,
                                IM_COL32(acc.r, acc.g, acc.b, 110));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::SetNextItemWidth(boxB.x - textLeft - 12.0f);

        const ImGuiInputTextFlags allFlags = caret
            ? (flags | ImGuiInputTextFlags_CallbackAlways)
            : flags;
        const bool changed = ImGui::InputTextWithHint(
            id, hint, buf, bufSize, allFlags,
            caret ? InputCaretCaptureCb : nullptr,
            caret);

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);

        const size_t bufLen   = std::strlen(buf);
        ImFont*      font     = ImGui::GetFont();
        const float  fontSize = ImGui::GetFontSize();

        if (bufLen > 0) {
            dl->AddText(font, fontSize, textOrigin,
                ToImU32(Style::text), buf);
        } else if (!focused && hint && hint[0]) {
            dl->AddText(font, fontSize, textOrigin,
                    IM_COL32(110, 100, 140, 255), hint);
        }

        if (caret && focused) {

            const int  cIdx    = (caret->cursorIdx < 0) ? 0
                                  : (caret->cursorIdx > (int)bufLen)
                                    ? (int)bufLen : caret->cursorIdx;
            const ImVec2 preSz = font->CalcTextSizeA(
                fontSize, FLT_MAX, 0.0f, buf, buf + cIdx);
            const float newTarget = textOrigin.x + preSz.x;
            if (std::fabsf(newTarget - caret->caretXTarget) > 0.5f) {
                caret->caretXTarget = newTarget;
                caret->blinkResetSec = NowSec();
            }

            if (caret->caretX <= 0.001f) caret->caretX = newTarget;
            caret->caretX = EaseTo(caret->caretX, caret->caretXTarget, 22.0f);

            const double sinceMove = NowSec() - caret->blinkResetSec;
            const bool   blinkOn   = ((int)(sinceMove / 0.53) & 1) == 0;
            if (blinkOn) {
                const float cx   = caret->caretX;
                const float topY = textOrigin.y + 1.0f;
                const float botY = textOrigin.y + fontSize - 1.0f;
                dl->AddLine({cx, topY}, {cx, botY},
                             ToImU32(Style::text), 1.5f);
            }
        }

        ImGui::SetCursorScreenPos({pos.x, pos.y + height + 8.0f});
        return changed;
    }

    static void DrawProceduralCover(ImDrawList* dl,
                                     const std::string& gameId,
                                     const std::string& displayName,
                                     ImVec2 a, ImVec2 b, float roundness);

    static void DrawUserIdHelpPopup();

    struct HeroSlide { const char* gameId; const char* displayName; };
    static const HeroSlide kHeroSlides[] = {
        { "dbd",       "Dead by Daylight" },
        { "apex",      "Apex Legends"     },
        { "dayz",      "DayZ"             },
        { "rust",      "Rust"             },
        { "offscript", "Offscript"        },
    };
    static constexpr int    kHeroSlideCount = (int)(sizeof(kHeroSlides) / sizeof(kHeroSlides[0]));
    static constexpr double kHeroSlideSec   = 4.5;
    static constexpr double kHeroFadeSec    = 0.55;

    static void DrawHeroSlide(ImDrawList* dl, const HeroSlide& s,
                                ImVec2 a, ImVec2 b, float rounding,
                                float alpha) {
        if (alpha <= 0.001f) return;
        const BYTE A = (BYTE)(255.0f * alpha);

        dl->AddRectFilled(a, b,
            IM_COL32(14, 14, 20, (BYTE)(255 * alpha)),
            rounding);

        const Tex& t = GetGameTexture(s.gameId);
        if (t.srv && t.w > 0 && t.h > 0) {

            const float srcAR = (float)t.w / (float)t.h;
            ImVec2 imgA, imgB;
            AspectFit(a, b, srcAR, imgA, imgB);

            dl->AddImageRounded(
                reinterpret_cast<ImTextureID>(t.srv),
                imgA, imgB,
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, A),
                rounding);
        } else {

            dl->PushClipRect(a, b, true);
            DrawProceduralCover(dl, s.gameId, s.displayName,
                                  a, b, rounding);
            dl->PopClipRect();
        }

        const BYTE darkA = (BYTE)(180 * alpha);

        const float solidH = std::min((b.y - a.y) * 0.30f, rounding * 2.5f);
        const float solidTop = b.y - solidH;
        dl->AddRectFilled(
            { a.x, solidTop }, b,
            IM_COL32(0, 0, 0, darkA),
            rounding, ImDrawFlags_RoundCornersBottom);

        const float fadeH = (b.y - a.y) * 0.30f;
        const float fadeTop = solidTop - fadeH;
        dl->AddRectFilledMultiColor(
            { a.x, fadeTop }, { b.x, solidTop },
            IM_COL32(0, 0, 0, 0),     IM_COL32(0, 0, 0, 0),
            IM_COL32(0, 0, 0, darkA), IM_COL32(0, 0, 0, darkA));

        PushBigFont();
        ImFont*     bf = ImGui::GetFont();
        const float bs = ImGui::GetFontSize() * 1.15f;
        const ImVec2 ts = bf->CalcTextSizeA(bs, FLT_MAX, 0.0f, s.displayName);
        dl->AddText(bf, bs,
                     { a.x + 22.0f, b.y - ts.y - 18.0f },
                     IM_COL32(255, 255, 255, A), s.displayName);
        PopFont();
    }

    static void DrawLoginHeroSlideshow(ImDrawList* dl, ImVec2 a, ImVec2 b,
                                         float rounding) {
        if (kHeroSlideCount == 0) return;

        if (g.loginHeroSwitchAt < 1.0) g.loginHeroSwitchAt = NowSec();

        const double now     = NowSec();
        const double elapsed = now - g.loginHeroSwitchAt;

        if (elapsed >= kHeroSlideSec) {
            g.loginHeroPrev    = g.loginHeroIdx;
            g.loginHeroIdx     = (g.loginHeroIdx + 1) % kHeroSlideCount;
            g.loginHeroSwitchAt = now;
        }

        const double sinceSwitch = now - g.loginHeroSwitchAt;
        const float  fadeT = (kHeroFadeSec > 0.0)
            ? (float)(sinceSwitch / kHeroFadeSec)
            : 1.0f;
        const float  currA = fadeT >= 1.0f ? 1.0f : fadeT;
        const float  prevA = 1.0f - currA;

        dl->PushClipRect(a, b, true);

        if (g.loginHeroPrev >= 0 && g.loginHeroPrev < kHeroSlideCount && prevA > 0.001f) {
            DrawHeroSlide(dl, kHeroSlides[g.loginHeroPrev],
                            a, b, rounding, prevA);
        } else {
            g.loginHeroPrev = -1;
        }
        DrawHeroSlide(dl, kHeroSlides[g.loginHeroIdx],
                        a, b, rounding, currA);

        if (currA >= 1.0f) g.loginHeroPrev = -1;

        dl->PopClipRect();

        const float dotR    = 3.5f;
        const float dotGap  = 6.0f;
        const float padR    = 16.0f;
        const float padB    = 12.0f;
        const float totalW  = kHeroSlideCount * (dotR * 2.0f) + (kHeroSlideCount - 1) * dotGap;
        const float dotsX   = b.x - padR - totalW;
        const float dotsY   = b.y - padB - dotR;
        const Color acc     = Style::accentColor;
        const ImVec2 mp     = ImGui::GetIO().MousePos;
        for (int i = 0; i < kHeroSlideCount; ++i) {
            const float cx = dotsX + dotR + i * (dotR * 2.0f + dotGap);
            const bool isCur = (i == g.loginHeroIdx);
            const ImU32 col = isCur
                ? IM_COL32(acc.r, acc.g, acc.b, 255)
                : IM_COL32(220, 220, 230, 130);
            dl->AddCircleFilled({ cx, dotsY }, dotR, col, 14);

            const float hit = 8.0f;
            if (mp.x >= cx - hit && mp.x <= cx + hit &&
                mp.y >= dotsY - hit && mp.y <= dotsY + hit &&
                ImGui::IsMouseClicked(0) && i != g.loginHeroIdx)
            {
                g.loginHeroPrev    = g.loginHeroIdx;
                g.loginHeroIdx     = i;
                g.loginHeroSwitchAt = NowSec();
            }
        }
    }

    static void DrawLoginScreen() {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(Color(18, 14, 28, 255)));
        ImGui::Begin("##login", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize  |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl   = ImGui::GetWindowDrawList();
        const ImVec2 win = ImGui::GetWindowPos();
        const Color acc  = Style::accentColor;

        DrawLoginWindowChrome(dl, win.x + disp.x, win.y);

        const float heroPadX = 16.0f;
        const float heroPadY = 16.0f;
        const float heroR    = 16.0f;
        const float heroW    = disp.x - heroPadX * 2.0f;

        float heroHCand = heroW * (9.0f / 16.0f);
        const float heroHMax = disp.y * 0.48f;
        const float heroH    = heroHCand > heroHMax ? heroHMax : heroHCand;
        const ImVec2 heroA = { win.x + heroPadX,
                                win.y + heroPadY + 30.0f };
        const ImVec2 heroB = { win.x + heroPadX + heroW,
                                heroA.y + heroH };

        DrawLoginHeroSlideshow(dl, heroA, heroB, heroR);

        dl->AddRect(heroA, heroB, IM_COL32(255, 255, 255, 22),
                     heroR, 0, 1.0f);

        const float padX = heroPadX + 18.0f;
        const float contentW = disp.x - padX * 2.0f;
        ImGui::SetCursorScreenPos({ win.x + padX, heroB.y + 22.0f });

        PushBigFont();
        {
            ImFont* f = ImGui::GetFont();
            const float fs = ImGui::GetFontSize() * 1.15f;
            const char* head = "Good to see you again!";
            const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, head);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            dl->AddText(f, fs, cur, IM_COL32(240, 230, 255, 255), head);
            ImGui::SetCursorScreenPos({ cur.x, cur.y + ts.y + 4.0f });
        }
        PopFont();

        PushBodyFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
        ImGui::TextUnformatted("Enter your license key to log in to your account below");
        ImGui::PopStyleColor();
        PopFont();

        ImGui::Dummy({0, 14.0f});

        bool pairActive = false;
        std::string pcode, purl, pstatus;
        int  psecsLeft = 0;
        bool pfinished = false;
        bool psucceeded = false;
        {
            std::lock_guard<std::mutex> lk(g.pairMu);
            pairActive = g.pair.active;
            if (pairActive) {
                pcode   = g.pair.pair_code;
                purl    = g.pair.verify_url;
                pstatus = g.pair.status_text;
                pfinished  = g.pair.finished;
                psucceeded = g.pair.succeeded;
                const DWORD elapsedMs = ::GetTickCount() - g.pair.startedTick;
                const int   elapsedS  = (int)(elapsedMs / 1000);
                psecsLeft = g.pair.expires_in - elapsedS;
                if (psecsLeft < 0) psecsLeft = 0;
            }
        }

        if (pairActive) {

            ImGui::SetCursorPosX(padX);
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
            ImGui::TextUnformatted(
                "First-time setup: approve this device in your browser. "
                "Enter this code on the page that opens.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            PopFont();

            ImGui::Dummy({0, 10.0f});

            {
                PushBigFont();
                ImFont* f = ImGui::GetFont();
                const float fs = ImGui::GetFontSize() * 1.30f;
                const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, pcode.c_str());
                ImGui::SetCursorPosX(padX + (contentW - ts.x) * 0.5f);
                const ImVec2 cur = ImGui::GetCursorScreenPos();
                dl->AddText(f, fs, cur, IM_COL32(240, 230, 255, 255), pcode.c_str());
                ImGui::Dummy({ ts.x, ts.y + 6.0f });
                PopFont();
            }

            ImGui::SetCursorPosX(padX);
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
            char statusLine[256];
            if (pfinished) {
                snprintf(statusLine, sizeof(statusLine), "%s", pstatus.c_str());
            } else {
                snprintf(statusLine, sizeof(statusLine),
                         "%s  (expires in %d:%02d)",
                         pstatus.c_str(), psecsLeft / 60, psecsLeft % 60);
            }
            ImGui::TextUnformatted(statusLine);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            PopFont();

            ImGui::Dummy({0, 14.0f});

            ImGui::SetCursorPosX(padX);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            const ImVec4 accVec_   = ToImVec4(acc);
            const ImVec4 accHov_   = ImVec4(accVec_.x + 0.07f, accVec_.y + 0.07f,
                                            accVec_.z + 0.07f, accVec_.w);
            const ImVec4 accAct_   = ImVec4(accVec_.x - 0.04f, accVec_.y - 0.04f,
                                            accVec_.z - 0.04f, accVec_.w);
            ImGui::PushStyleColor(ImGuiCol_Button,        accVec_);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accHov_);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  accAct_);
            ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32_WHITE);
            PushBigFont();
            const char* btnLabel = pfinished
                ? (psucceeded ? "Continue" : "Retry")
                : "Open in Browser";
            if (ImGui::Button(btnLabel, { contentW, 48.0f })) {

                (void)pfinished;
                (void)psucceeded;
                (void)purl;
            }
            PopFont();
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();

            ImGui::Dummy({0, 8.0f});
            ImGui::SetCursorPosX(padX);
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
            ImGui::TextUnformatted(purl.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            PopFont();

            ImGui::Dummy({0, 14.0f});
            {
                PushBodyFont();
                const char* link = "Don't have a key?";
                const ImVec2 ts  = ImGui::CalcTextSize(link);
                ImGui::SetCursorPosX((disp.x - ts.x) * 0.5f);
                const ImVec2 cur = ImGui::GetCursorScreenPos();
                const ImVec2 mp  = ImGui::GetIO().MousePos;
                const bool hov   = mp.x >= cur.x && mp.x <= cur.x + ts.x &&
                                   mp.y >= cur.y && mp.y <= cur.y + ts.y;
                const ImU32 col = hov
                    ?                     IM_COL32(190, 180, 215, 255)
                    : ToImU32(Style::dimmedText);
                dl->AddText(cur, col, link);
                ImGui::Dummy({ts.x, ts.y});
                PopFont();
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
            DrawUserIdHelpPopup();
            return;
        }

        ImGui::SetCursorPosX(padX);
        const bool enterFromId = IconInput(
            "##did", "License key",
            g.licenseKeyInput, sizeof(g.licenseKeyInput),
            (ImWchar)0xf007, contentW, 44.0f,
            g.licenseKeyAnim,
            &g.discordIdCaret,
            !g.loginError.empty(),
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CharsUppercase);

        if (enterFromId == false /* changed checked above */) {

            static size_t lastBufLen = 0;
            const size_t curLen = std::strlen(g.licenseKeyInput);
            if (curLen > lastBufLen && !g.loginError.empty()) {
                g.loginError.clear();
            }
            lastBufLen = curLen;
        }

        if (false) { PushBodyFont(); PopFont(); }

        ImGui::Dummy({0, 18.0f});

        ImGui::SetCursorPosX(padX);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        const ImVec4 accVec   = ToImVec4(acc);
        const ImVec4 accHov   = ImVec4(accVec.x + 0.07f, accVec.y + 0.07f,
                                        accVec.z + 0.07f, accVec.w);
        const ImVec4 accActive= ImVec4(accVec.x - 0.04f, accVec.y - 0.04f,
                                        accVec.z - 0.04f, accVec.w);
        ImGui::PushStyleColor(ImGuiCol_Button,        accVec);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  accActive);
        ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32_WHITE);

        PushBigFont();
        const bool clicked = ImGui::Button("Enter", { contentW, 48.0f });
        PopFont();

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();

        if (clicked || enterFromId) TryLogin();

        if (!g.loginError.empty()) {
            ImGui::Dummy({0, 6.0f});
            ImGui::SetCursorPosX(padX);
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::warning));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
            ImGui::TextUnformatted(g.loginError.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            PopFont();
        }

        ImGui::Dummy({0, 14.0f});
        {
            PushBodyFont();
            const char* link = "Don't have a key?";
            const ImVec2 ts  = ImGui::CalcTextSize(link);
            ImGui::SetCursorPosX((disp.x - ts.x) * 0.5f);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            const ImVec2 mp  = ImGui::GetIO().MousePos;
            const bool hov   = mp.x >= cur.x && mp.x <= cur.x + ts.x &&
                               mp.y >= cur.y && mp.y <= cur.y + ts.y;
            const ImU32 col = hov
                ?                     IM_COL32(190, 180, 215, 255)
                : ToImU32(Style::dimmedText);
            dl->AddText(cur, col, link);
            ImGui::Dummy({ts.x, ts.y});
            PopFont();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        DrawUserIdHelpPopup();
    }

    static void DrawUserIdHelpPopup() {

        g.showUserIdHelp = false;
        return;
        g.userIdHelpAnim = EaseTo(g.userIdHelpAnim,
                                    g.showUserIdHelp ? 1.0f : 0.0f,
                                    16.0f);
        if (g.userIdHelpAnim < 0.001f && !g.showUserIdHelp) return;

        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const float  t    = g.userIdHelpAnim;

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        ImGui::Begin("##useridhelp_bg", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const BYTE bgA = (BYTE)(170.0f * t);
        dl->AddRectFilled({0, 0}, disp, IM_COL32(6, 6, 10, bgA));

        const float cardW = 380.0f;
        const float cardH = 360.0f;
        const float pop   = 0.96f + 0.04f * t;
        const float w     = cardW * pop;
        const float h     = cardH * pop;
        const ImVec2 a    = { (disp.x - w) * 0.5f, (disp.y - h) * 0.5f };
        const ImVec2 b    = { a.x + w,             a.y + h };

        if (g.showUserIdHelp && ImGui::IsMouseClicked(0)) {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            if (mp.x < a.x || mp.x > b.x || mp.y < a.y || mp.y > b.y) {
                g.showUserIdHelp = false;
            }
        }

        if (g.showUserIdHelp && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g.showUserIdHelp = false;
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        ImGui::SetNextWindowPos(a);
        ImGui::SetNextWindowSize({w, h});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(28, 24));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,            t);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(Color(22, 17, 36, 255)));
        ImGui::PushStyleColor(ImGuiCol_Border,   IM_COL32(255, 255, 255, 22));
        ImGui::Begin("##useridhelp_card", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList*  cardDl = ImGui::GetWindowDrawList();
        const ImVec2 win    = ImGui::GetWindowPos();
        const Color  acc    = Style::accentColor;

        {
            const float cx = win.x + w - 18.0f - 12.0f;
            const float cy = win.y + 22.0f;
            const ImVec2 hitA = {cx - 14.0f, cy - 14.0f};
            const ImVec2 hitB = {cx + 14.0f, cy + 14.0f};
            const ImVec2 mp   = ImGui::GetIO().MousePos;
            const bool   hov  = mp.x >= hitA.x && mp.x <= hitB.x &&
                                mp.y >= hitA.y && mp.y <= hitB.y;
            const ImU32 col = hov ? IM_COL32_WHITE : IM_COL32(170, 160, 200, 255);
            const float s = 6.0f;
            cardDl->AddLine({cx - s, cy - s}, {cx + s, cy + s}, col, 1.4f);
            cardDl->AddLine({cx - s, cy + s}, {cx + s, cy - s}, col, 1.4f);
            if (hov && ImGui::IsMouseClicked(0)) g.showUserIdHelp = false;
        }

        PushBigFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::text));
        ImGui::TextUnformatted("Find your Discord User ID");
        ImGui::PopStyleColor();
        PopFont();

        {
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            cardDl->AddLine({cur.x, cur.y + 6.0f},
                              {cur.x + 36.0f, cur.y + 6.0f},
                              IM_COL32(acc.r, acc.g, acc.b, 255), 2.0f);
        }
        ImGui::Dummy({0, 14.0f});

        static const char* const kSteps[] = {
            "Open Discord and click the gear icon (User Settings) "
            "next to your name in the bottom-left.",
            "Go to Advanced (under App Settings) and toggle on "
            "Developer Mode.",
            "Close settings. Right-click your own name in any "
            "server or DM and pick \"Copy User ID\".",
            "Paste the 18-19 digit number into the field above "
            "and press Enter.",
        };
        for (int i = 0; i < (int)(sizeof(kSteps) / sizeof(kSteps[0])); ++i) {
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();

            const float bubR = 10.0f;
            const ImVec2 bubC = { rowStart.x + bubR, rowStart.y + bubR };
            cardDl->AddCircleFilled(bubC, bubR,
                IM_COL32(acc.r, acc.g, acc.b, 255), 18);
            char numBuf[4];
            std::snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
            PushBodyFont();
            const ImVec2 nts = ImGui::CalcTextSize(numBuf);
            cardDl->AddText({bubC.x - nts.x * 0.5f, bubC.y - nts.y * 0.5f},
                              IM_COL32_WHITE, numBuf);
            PopFont();

            ImGui::SetCursorScreenPos({rowStart.x + bubR * 2.0f + 12.0f,
                                          rowStart.y - 1.0f});
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::text));
            ImGui::PushTextWrapPos(win.x + w - 28.0f);
            ImGui::TextUnformatted(kSteps[i]);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            PopFont();

            ImGui::Dummy({0, 10.0f});
        }

        const float btnH = 36.0f;
        ImGui::SetCursorScreenPos({ win.x + 28.0f, win.y + h - 28.0f - btnH });
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ToImVec4(acc));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(ToImVec4(acc).x + 0.07f, ToImVec4(acc).y + 0.07f,
                    ToImVec4(acc).z + 0.07f, ToImVec4(acc).w));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(ToImVec4(acc).x - 0.04f, ToImVec4(acc).y - 0.04f,
                    ToImVec4(acc).z - 0.04f, ToImVec4(acc).w));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
        if (ImGui::Button("Got it", { w - 56.0f, btnH })) {
            g.showUserIdHelp = false;
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }

    static float EaseTo(float cur, float target, float speed) {
        float k = ImGui::GetIO().DeltaTime * speed;
        if (k > 1.0f) k = 1.0f;
        return cur + (target - cur) * k;
    }

    static double NowSec() {
        LARGE_INTEGER c, f;
        QueryPerformanceCounter(&c);
        QueryPerformanceFrequency(&f);
        return (double)c.QuadPart / (double)f.QuadPart;
    }

    struct EnvCheck {
        const char* name;
        bool        desiredOn;
        bool        actualOn;
        const char* hint;
    };

    inline static const EnvCheck kEnvChecks[] = {
        { "Secure Boot",                 false, false, "Required OFF — bootkit is unsigned" },
        { "Memory Integrity (HVCI)",     false, false, "Required OFF for HV install"        },
        { "Virtualization-Based Security", false, true, "Disable Core Isolation"            },
        { "Defender real-time",          false, true,  "Add folder exclusion"               },
        { "Hardware virtualization",     true,  true,  "Enable in BIOS"                     },
        { "Hyper-V launch type",         true,  true,  "bcdedit set hypervisorlaunchtype auto" },
        { "BitLocker on C:",             false, false, "Suspend if currently protecting"    },
        { "Administrator",               true,  true,  nullptr                              },
    };

    inline static const char* kMockHwid =
        "4F3A-2B81-9C7D-E1F2-A6B5-8C4E-D903-1A2B";

    constexpr float kSidebarW = 76.0f;

    static bool SidebarIcon(ImDrawList* dl, float cx, float cy,
                            const char* glyph, bool active,
                            float& anim) {
        const float cellW = 48.0f, cellH = 48.0f;
        const ImVec2 a = { cx - cellW * 0.5f, cy - cellH * 0.5f };
        const ImVec2 b = { a.x + cellW,       a.y + cellH };

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool hov  = mouse.x >= a.x && mouse.x <= b.x &&
                          mouse.y >= a.y && mouse.y <= b.y;
        const bool clk  = hov && ImGui::IsMouseClicked(0);

        anim = EaseTo(anim, (hov || active) ? 1.0f : 0.0f, 16.0f);

        Color acc = Style::accentColor;

        if (active) {

            for (int i = 3; i >= 0; --i) {
                float infl = (i + 1) * 3.0f;
                float falloff = 1.0f - (float)i / 4.0f;
                BYTE  alpha = (BYTE)(80.0f * falloff * falloff);
                dl->AddRect(
                    {a.x - infl, a.y - infl},
                    {b.x + infl, b.y + infl},
                    IM_COL32(acc.r, acc.g, acc.b, alpha),
                    12.0f + infl, 0, 1.6f);
            }
            dl->AddRectFilled(a, b,
                IM_COL32(acc.r, acc.g, acc.b, 86), 12.0f);

            dl->AddRectFilled(
                { 0.0f, a.y + 8.0f },
                { 3.0f, b.y - 8.0f },
                ToImU32(acc));
        } else if (anim > 0.001f) {

            BYTE alpha = (BYTE)(28.0f * anim);
            dl->AddRectFilled(a, b,
                IM_COL32(255, 255, 255, alpha), 12.0f);
        }

        PushIconFont();
        ImVec2 ts = ImGui::CalcTextSize(glyph);
        ImU32  col;
        if (active) {
            col = IM_COL32_WHITE;
        } else {
            BYTE  base  = 144;
            BYTE  bright = 232;
            BYTE  v     = (BYTE)(base + (bright - base) * anim);
            col = IM_COL32(v, v, v + 8, 255);
        }
        dl->AddText({cx - ts.x * 0.5f, cy - ts.y * 0.5f},
                    col, glyph);
        PopFont();

        return clk;
    }

    static void DrawSidebar() {
        const float h = ImGui::GetIO().DisplaySize.y;
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({kSidebarW, h});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##sidebar", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilledMultiColor(
            {0, 0}, {kSidebarW, h},
            IM_COL32(20, 16, 34, 255),
            IM_COL32(20, 16, 34, 255),
            IM_COL32(14, 10, 26, 255),
            IM_COL32(14, 10, 26, 255));

        dl->AddRectFilledMultiColor(
            {0, 0}, {kSidebarW, 80.0f},
            IM_COL32(255, 255, 255, 8), IM_COL32(255, 255, 255, 8),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));

        dl->AddLine({kSidebarW - 0.5f, 8.0f}, {kSidebarW - 0.5f, h - 8.0f},
                    IM_COL32(255, 255, 255, 18), 1.0f);

        {
            const float logoY = 16.0f;
            const float boxS  = 36.0f;
            const float lx    = (kSidebarW - boxS) * 0.5f;
            Color acc = Style::accentColor;

            dl->AddRectFilled(
                {lx, logoY}, {lx + boxS, logoY + boxS},
                ToImU32(acc), 8.0f);

            dl->AddRectFilledMultiColor(
                {lx + 2, logoY + 2}, {lx + boxS - 2, logoY + boxS * 0.55f},
                IM_COL32(255, 255, 255, 38), IM_COL32(255, 255, 255, 38),
                IM_COL32(255, 255, 255, 0),  IM_COL32(255, 255, 255, 0));

            PushBigFont();
            const char* mark = "g";
            ImVec2 ts = ImGui::CalcTextSize(mark);
            dl->AddText({lx + (boxS - ts.x) * 0.5f,
                         logoY + (boxS - ts.y) * 0.5f},
                        IM_COL32_WHITE, mark);
            PopFont();
        }

        const float cx = kSidebarW * 0.5f;

        {
            const float cy    = 108.0f;
            const float cellW = 48.0f, cellH = 48.0f;
            const ImVec2 a = { cx - cellW * 0.5f, cy - cellH * 0.5f };
            const ImVec2 b = { a.x + cellW,        a.y + cellH };

            const ImVec2 savedCursor = ImGui::GetCursorPos();
            ImGui::SetCursorScreenPos(a);
            const bool clicked = ImGui::InvisibleButton(
                "##gear_btn", { cellW, cellH });
            const bool hov = ImGui::IsItemHovered();
            ImGui::SetCursorPos(savedCursor);

            const bool active = g.settingsPanelOpen;
            g.settingsBtnAnim = EaseTo(g.settingsBtnAnim,
                                        (hov || active) ? 1.0f : 0.0f,
                                        16.0f);

            Color acc = Style::accentColor;

            if (active) {

                for (int i = 3; i >= 0; --i) {
                    const float infl    = (i + 1) * 3.0f;
                    const float falloff = 1.0f - (float)i / 4.0f;
                    const BYTE  alpha   = (BYTE)(80.0f * falloff * falloff);
                    dl->AddRect(
                        { a.x - infl, a.y - infl },
                        { b.x + infl, b.y + infl },
                        IM_COL32(acc.r, acc.g, acc.b, alpha),
                        12.0f + infl, 0, 1.6f);
                }
                dl->AddRectFilled(a, b,
                    IM_COL32(acc.r, acc.g, acc.b, 86), 12.0f);

                dl->AddRectFilled(
                    { 0.0f, a.y + 8.0f },
                    { 3.0f, b.y - 8.0f },
                    ToImU32(acc));
            } else if (g.settingsBtnAnim > 0.001f) {

                const BYTE alpha = (BYTE)(28.0f * g.settingsBtnAnim);
                dl->AddRectFilled(a, b,
                    IM_COL32(255, 255, 255, alpha), 12.0f);
            }

            {
                ImU32 col;
                if (active) {
                    col = IM_COL32_WHITE;
                } else {
                    const BYTE base   = 144;
                    const BYTE bright = 232;
                    const BYTE v = (BYTE)(base + (bright - base) * g.settingsBtnAnim);
                    col = IM_COL32(v, v, v + 8, 255);
                }

                const float rot = g.settingsPanelAnim * 0.28f;
                DrawGearIcon(dl, { cx, cy }, 12.0f, col, rot);
            }

            if (clicked) {
                g.settingsPanelOpen = !g.settingsPanelOpen;

                if (g.settingsPanelOpen) g.envPanelOpen = false;
            }
        }

        {
            const float ay   = h - 56.0f;
            const float r    = 18.0f;
            const ImVec2 ctr = { cx, ay };

            TryLoadDiscordAvatar();

            Color acc = Style::accentColor;
            dl->AddCircleFilled(ctr, r + 2.0f,
                IM_COL32(acc.r, acc.g, acc.b, 80), 24);

            if (g_AvatarSrv) {

                const ImVec2 a = { ctr.x - r, ctr.y - r };
                const ImVec2 b = { ctr.x + r, ctr.y + r };
                dl->AddImageRounded(
                    reinterpret_cast<ImTextureID>(g_AvatarSrv),
                    a, b,
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                    IM_COL32_WHITE, r);
            } else {

                dl->AddCircleFilled(ctr, r,
                    IM_COL32(46, 38, 66, 255), 24);
                PushIconFont();
                ImVec2 ts = ImGui::CalcTextSize(ICON_FA_USER);
                dl->AddText({ctr.x - ts.x * 0.5f, ctr.y - ts.y * 0.5f},
                            IM_COL32_WHITE, ICON_FA_USER);
                PopFont();
            }
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    constexpr float kTopBarH = 52.0f;

    static void DrawTopBar() {
        const float w = ImGui::GetIO().DisplaySize.x - kSidebarW;
        ImGui::SetNextWindowPos({kSidebarW, 0});
        ImGui::SetNextWindowSize({w, kTopBarH});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##topbar", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();

        dl->AddRectFilledMultiColor(
            origin,
            { origin.x + w, origin.y + kTopBarH },
            IM_COL32(18, 14, 28, 255), IM_COL32(18, 14, 28, 255),
            IM_COL32(12, 8, 22, 255), IM_COL32(12, 8, 22, 255));
        dl->AddLine(
            { origin.x,     origin.y + kTopBarH - 0.5f },
            { origin.x + w, origin.y + kTopBarH - 0.5f },
            IM_COL32(255, 255, 255, 18), 1.0f);

        PushBodyFont();
        const float padL = 28.0f;
        const float baseY = origin.y + 18.0f;

        const char* trail = "";
        dl->AddText({origin.x + padL, baseY},
                    IM_COL32(140, 130, 165, 255), trail);
        ImVec2 trailSz = ImGui::CalcTextSize(trail);

        const float chevX = origin.x + padL + trailSz.x + 12.0f;
        PushIconFont();
        ImVec2 chSz = ImGui::CalcTextSize(ICON_FA_CHEVRON_RIGHT);
        dl->AddText({chevX, baseY + (trailSz.y - chSz.y) * 0.5f},
                    IM_COL32(110, 100, 140, 255), ICON_FA_CHEVRON_RIGHT);
        PopFont();

        const char* current = "Games";
        dl->AddText({chevX + chSz.x + 12.0f, baseY},
                    IM_COL32(230, 220, 248, 255), current);
        PopFont();

        const float ctrlH = 28.0f;
        const float ctrlW = 36.0f;
        const float ctrlPadR = 0.0f;
        const float ctrlTop  = 0.0f;

        const ImVec2 closeA = { origin.x + w - ctrlW - ctrlPadR, origin.y + ctrlTop };
        const ImVec2 closeB = { closeA.x + ctrlW,                closeA.y + ctrlH };
        const ImVec2 minA   = { closeA.x - ctrlW,                closeA.y };
        const ImVec2 minB   = { closeA.x,                        closeA.y + ctrlH };

        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool   minHov   = mp.x >= minA.x   && mp.x <= minB.x &&
                                mp.y >= minA.y   && mp.y <= minB.y;
        const bool   closeHov = mp.x >= closeA.x && mp.x <= closeB.x &&
                                mp.y >= closeA.y && mp.y <= closeB.y;

        g.minBtnAnim   = EaseTo(g.minBtnAnim,   minHov   ? 1.0f : 0.0f, 18.0f);
        g.closeBtnAnim = EaseTo(g.closeBtnAnim, closeHov ? 1.0f : 0.0f, 18.0f);

        if (g.minBtnAnim > 0.001f) {
            BYTE alpha = (BYTE)(36.0f * g.minBtnAnim);
            dl->AddRectFilled(minA, minB,
                IM_COL32(255, 255, 255, alpha));
        }

        if (g.closeBtnAnim > 0.001f) {
            BYTE alpha = (BYTE)(220.0f * g.closeBtnAnim);
            dl->AddRectFilled(closeA, closeB,
                IM_COL32(232, 17, 35, alpha));
        }

        {
            const float lineW = 12.0f;
            const float cx    = (minA.x + minB.x) * 0.5f;
            const float cy    = minA.y + ctrlH * 0.5f;
            dl->AddLine({cx - lineW * 0.5f, cy},
                         {cx + lineW * 0.5f, cy},
                         IM_COL32(220, 210, 240, 255), 1.0f);
        }

        {
            const float xS = 5.0f;
            const float cx = (closeA.x + closeB.x) * 0.5f;
            const float cy = closeA.y + ctrlH * 0.5f;
            ImU32 col = closeHov ? IM_COL32_WHITE
                                  :                     IM_COL32(220, 210, 240, 255);
            dl->AddLine({cx - xS, cy - xS}, {cx + xS, cy + xS}, col, 1.2f);
            dl->AddLine({cx - xS, cy + xS}, {cx + xS, cy - xS}, col, 1.2f);
        }

        if (ImGui::IsMouseClicked(0)) {
            if (minHov && Memory::hwnd) {
                ::ShowWindow(Memory::hwnd, SW_MINIMIZE);
            } else if (closeHov && Memory::hwnd) {
                ::PostMessageW(Memory::hwnd, WM_CLOSE, 0, 0);
            }
        }

        const float btnW = 96.0f;
        const float btnH = 32.0f;
        const float signOutRight = w - ctrlW * 2.0f - 12.0f;
        ImGui::SetCursorPos({signOutRight - btnW, (kTopBarH - btnH) * 0.5f});
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255,255,255,18));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(255,255,255,32));
        ImGui::PushStyleColor(ImGuiCol_Text,          ToImVec4(Style::dimmedText));
        if (ImGui::Button("Sign out", {btnW, btnH}))
            DoLogout();
        ImGui::PopStyleColor(4);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    static void DrawEnvPanel() {

        g.envPanelAnim = EaseTo(g.envPanelAnim,
                                 g.envPanelOpen ? 1.0f : 0.0f,
                                 14.0f);
        if (g.envPanelAnim < 0.001f && !g.envPanelOpen) return;

        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const float panelW = 420.0f;
        const float panelX = kSidebarW + (g.envPanelAnim - 1.0f) * panelW;

        {
            ImGui::SetNextWindowPos({kSidebarW, 0});
            ImGui::SetNextWindowSize({disp.x - kSidebarW, disp.y});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0,0,0,0));
            ImGui::Begin("##envbackdrop", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoScrollbar| ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoSavedSettings);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            BYTE bgA = (BYTE)(150.0f * g.envPanelAnim);
            dl->AddRectFilled({kSidebarW, 0}, disp,
                IM_COL32(8, 8, 12, bgA));

            if (g.envPanelOpen && ImGui::IsMouseClicked(0)) {
                ImVec2 mp = ImGui::GetIO().MousePos;
                if (mp.x > panelX + panelW) g.envPanelOpen = false;
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }

        ImGui::SetNextWindowPos({panelX, 0});
        ImGui::SetNextWindowSize({panelW, disp.y});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32, 32));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
            ToImVec4(Color(20, 16, 34, 255)));
        ImGui::Begin("##envpanel", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar| ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl   = ImGui::GetWindowDrawList();
        const ImVec2 win = ImGui::GetWindowPos();
        Color acc        = Style::accentColor;

        dl->AddRectFilled(
            {win.x + panelW - 2.0f, win.y},
            {win.x + panelW,        win.y + disp.y},
            IM_COL32(acc.r, acc.g, acc.b, 110));

        PushBigFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::text));
        ImGui::TextUnformatted("System status");
        ImGui::PopStyleColor();
        PopFont();

        {
            const float xSize = 28.0f;
            const float xx = panelW - 32.0f - xSize;
            const float yy = 28.0f;
            ImVec2 cA = {win.x + xx,         win.y + yy};
            ImVec2 cB = {win.x + xx + xSize, win.y + yy + xSize};
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool xh = mp.x >= cA.x && mp.x <= cB.x &&
                      mp.y >= cA.y && mp.y <= cB.y;
            if (xh) {
                dl->AddRectFilled(cA, cB,
                    IM_COL32(255, 255, 255, 24), 6.0f);
                if (ImGui::IsMouseClicked(0)) g.envPanelOpen = false;
            }
            PushIconFont();
            ImVec2 ts = ImGui::CalcTextSize(ICON_FA_CIRCLE_XMARK);
            dl->AddText({cA.x + (xSize - ts.x) * 0.5f,
                          cA.y + (xSize - ts.y) * 0.5f},
                xh ? IM_COL32_WHITE : IM_COL32(170, 160, 200, 255),
                ICON_FA_CIRCLE_XMARK);
            PopFont();
        }

        ImGui::Dummy({0, 6});
        PushBodyFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
        ImGui::TextUnformatted("Your hardware ID and the environment");
        ImGui::TextUnformatted("settings the loader needs.");
        ImGui::PopStyleColor();
        PopFont();

        ImGui::Dummy({0, 28});

        PushBodyFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
        ImGui::TextUnformatted("HARDWARE ID");
        ImGui::PopStyleColor();
        PopFont();

        ImGui::Dummy({0, 8});

        {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            const float bw = ImGui::GetContentRegionAvail().x;
            const float bh = 52.0f;
            dl->AddRectFilled(cp, {cp.x + bw, cp.y + bh},
                IM_COL32(28, 22, 44, 255), 10.0f);

            PushBodyFont();
            ImVec2 ts = ImGui::CalcTextSize(kMockHwid);
            dl->AddText({cp.x + 16.0f, cp.y + (bh - ts.y) * 0.5f},
                IM_COL32(225, 215, 244, 255), kMockHwid);
            PopFont();

            const float btnW = 78.0f;
            const float btnH = bh - 14.0f;
            ImVec2 bA = {cp.x + bw - btnW - 7.0f, cp.y + 7.0f};
            ImVec2 bB = {bA.x + btnW,              bA.y + btnH};
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const bool hov = mp.x >= bA.x && mp.x <= bB.x &&
                             mp.y >= bA.y && mp.y <= bB.y;
            const bool clk = hov && ImGui::IsMouseClicked(0);
            const bool justCopied = (NowSec() - g.envCopiedAt) < 1.2;

            ImU32 bgC;
            if (justCopied) {
                bgC = IM_COL32(22, 163, 74, 255);
            } else if (hov) {
                bgC = IM_COL32(acc.r, acc.g, acc.b, 230);
            } else {
                bgC =                 IM_COL32(46, 38, 66, 255);
            }
            dl->AddRectFilled(bA, bB, bgC, 7.0f);

            const char* lab = justCopied ? "Copied" : "Copy";
            PushBodyFont();
            ImVec2 lts = ImGui::CalcTextSize(lab);
            dl->AddText({bA.x + (btnW - lts.x) * 0.5f,
                          bA.y + (btnH - lts.y) * 0.5f},
                IM_COL32_WHITE, lab);
            PopFont();

            if (clk) {
                if (OpenClipboard(nullptr)) {
                    EmptyClipboard();
                    size_t len = std::strlen(kMockHwid);
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
                    if (hMem) {
                        void* p = GlobalLock(hMem);
                        if (p) {
                            std::memcpy(p, kMockHwid, len + 1);
                            GlobalUnlock(hMem);
                            SetClipboardData(CF_TEXT, hMem);
                        }
                    }
                    CloseClipboard();
                    g.envCopiedAt = NowSec();
                }
            }

            ImGui::Dummy({0, bh + 24.0f});
        }

        PushBodyFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
        ImGui::TextUnformatted("ENVIRONMENT");
        ImGui::PopStyleColor();
        PopFont();

        ImGui::Dummy({0, 8});

        for (const auto& chk : kEnvChecks) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            const float bw = ImGui::GetContentRegionAvail().x;
            const float bh = chk.hint ? 56.0f : 44.0f;

            dl->AddRectFilled(cp, {cp.x + bw, cp.y + bh},
                IM_COL32(26, 20, 42, 255), 10.0f);

            PushBodyFont();
            dl->AddText({cp.x + 16.0f, cp.y + 10.0f},
                IM_COL32(225, 215, 244, 255), chk.name);
            if (chk.hint) {
                dl->AddText({cp.x + 16.0f, cp.y + 30.0f},
                    IM_COL32(130, 120, 162, 255), chk.hint);
            }
            PopFont();

            const bool ok = (chk.actualOn == chk.desiredOn);
            const char* badge = chk.actualOn ? "ON" : "OFF";
            const ImU32 bgC = ok ? IM_COL32(22, 163, 74, 255)
                                 : IM_COL32(220, 38, 38, 255);
            const ImU32 iconBg = ok ? IM_COL32(22, 163, 74, 70)
                                    : IM_COL32(220, 38, 38, 70);

            PushBodyFont();
            ImVec2 bs = ImGui::CalcTextSize(badge);
            const float pw  = bs.x + 22.0f;
            const float ph  = 26.0f;
            const float pxR = cp.x + bw - pw - 16.0f;
            const float pyR = cp.y + (bh - ph) * 0.5f;
            dl->AddRectFilled({pxR, pyR},
                              {pxR + pw, pyR + ph}, bgC, ph * 0.5f);
            dl->AddText({pxR + (pw - bs.x) * 0.5f,
                          pyR + (ph - bs.y) * 0.5f},
                IM_COL32_WHITE, badge);
            PopFont();

            PushIconFont();
            const char* gly = ok ? ICON_FA_CIRCLE_CHECK : ICON_FA_CIRCLE_XMARK;
            ImVec2 gs = ImGui::CalcTextSize(gly);
            dl->AddText({pxR - gs.x - 10.0f,
                          cp.y + (bh - gs.y) * 0.5f},
                ok ? IM_COL32(22, 163, 74, 255) : IM_COL32(220, 38, 38, 255),
                gly);
            PopFont();

            (void)iconBg;

            ImGui::Dummy({0, bh + 10.0f});
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    static float HueForGame(const std::string& id) {
        uint32_t h = 5381u;
        for (char c : id) h = (h * 33u) ^ (uint8_t)c;
        float hue = (float)(h % 360);
        if (hue > 55.0f && hue < 110.0f) hue += 140.0f;
        if (hue >= 360.0f)               hue -= 360.0f;
        return hue;
    }

    static ImU32 HsvCol(float h, float s, float v, BYTE a) {
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(h / 360.0f, s, v, r, g, b);
        return IM_COL32((BYTE)(r * 255.0f),
                        (BYTE)(g * 255.0f),
                        (BYTE)(b * 255.0f), a);
    }

    static const char* const kSettingsTabs[] = {
        "Diagnostics",
        "Log",
        "Account",
        "Theme",
    };
    static constexpr int kSettingsTabCount =
        (int)(sizeof(kSettingsTabs) / sizeof(kSettingsTabs[0]));

    static inline ImU32 ColScaleAlpha(ImU32 c, float mul) {
        const ImU32 a  = (c >> 24) & 0xFF;
        const ImU32 a2 = (ImU32)((float)a * mul);
        return (c & 0x00FFFFFFu) | ((a2 & 0xFFu) << 24);
    }

    static bool DrawCopyableRow(const char* label, const char* value,
                                 int fieldId, float width,
                                 float alpha = 1.0f) {
        const Color  acc       = Style::accentColor;
        const float  rowH      = 38.0f;

        const float  labW      = 120.0f;
        const float  btnW      = 64.0f;
        const float  btnH      = 24.0f;
        const float  pad       = 12.0f;

        ImDrawList* dl    = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 rowA = origin;
        const ImVec2 rowB = { origin.x + width, origin.y + rowH };

        dl->AddRectFilled(rowA, rowB,
                ColScaleAlpha(IM_COL32(28, 22, 44, 255), alpha), 6.0f);

        PushBodyFont();
        const ImVec2 labSz = ImGui::CalcTextSize(label);
        dl->AddText({rowA.x + pad, rowA.y + (rowH - labSz.y) * 0.5f},
                     ColScaleAlpha(ToImU32(Style::dimmedText), alpha),
                     label);

        const ImVec2 valSz = ImGui::CalcTextSize(value);
        const float valX = rowA.x + pad + labW;
        const float valMaxX = rowB.x - pad - btnW - 10.0f;

        dl->PushClipRect({valX, rowA.y}, {valMaxX, rowB.y}, true);
        dl->AddText({valX, rowA.y + (rowH - valSz.y) * 0.5f},
                     ColScaleAlpha(ToImU32(Style::text), alpha), value);
        dl->PopClipRect();
        PopFont();

        const ImVec2 bA = {rowB.x - pad - btnW,
                            rowA.y + (rowH - btnH) * 0.5f};
        const ImVec2 bB = {bA.x + btnW, bA.y + btnH};
        const ImVec2 mp = ImGui::GetIO().MousePos;

        const bool   visible = alpha >= 0.9f;
        const bool   hov = visible &&
                           mp.x >= bA.x && mp.x <= bB.x &&
                           mp.y >= bA.y && mp.y <= bB.y;
        const bool   clk = hov && ImGui::IsMouseClicked(0);
        const bool   justCopied =
            (g.settingsCopiedField == fieldId) &&
            (NowSec() - g.settingsCopiedAt) < 1.2;

        ImU32 bgC;
        if (justCopied)  bgC = IM_COL32(22, 163, 74, 255);
        else if (hov)    bgC = IM_COL32(acc.r, acc.g, acc.b, 230);
        else             bgC =                 IM_COL32(46, 38, 66, 255);
        dl->AddRectFilled(bA, bB, ColScaleAlpha(bgC, alpha), 6.0f);

        const char* lab = justCopied ? "Copied" : "Copy";
        PushBodyFont();
        const ImVec2 lts = ImGui::CalcTextSize(lab);
        dl->AddText({bA.x + (btnW - lts.x) * 0.5f,
                      bA.y + (btnH - lts.y) * 0.5f},
                     ColScaleAlpha(IM_COL32_WHITE, alpha), lab);
        PopFont();

        if (clk) {

            if (OpenClipboard(nullptr)) {
                EmptyClipboard();
                const size_t len = std::strlen(value);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
                if (hMem) {
                    void* p = GlobalLock(hMem);
                    if (p) {
                        std::memcpy(p, value, len + 1);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_TEXT, hMem);
                    }
                }
                CloseClipboard();
                g.settingsCopiedAt    = NowSec();
                g.settingsCopiedField = fieldId;
            }
        }

        ImGui::Dummy({width, rowH + 10.0f});
        return clk;
    }

    #ifdef LOADER_BUILD_ID
    static constexpr const char* kLoaderBuildId = LOADER_BUILD_ID;
    #else
    static constexpr const char* kLoaderBuildId = "loadergui-preview";
    #endif

    static const char* OsVersionStr() {
        static char cached[64] = {0};
        if (cached[0]) return cached;
        typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
        HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            auto fn = (RtlGetVersionFn)::GetProcAddress(ntdll, "RtlGetVersion");
            if (fn) {
                RTL_OSVERSIONINFOW v = { sizeof(v) };
                if (fn(&v) == 0) {

                    const char* friendly =
                        (v.dwBuildNumber >= 22000) ? "Windows 11"
                      : (v.dwMajorVersion >= 10)   ? "Windows 10"
                                                   : "Windows";
                    std::snprintf(cached, sizeof(cached),
                        "%s (build %lu)", friendly, v.dwBuildNumber);
                    return cached;
                }
            }
        }
        std::strncpy(cached, "Windows (unknown build)", sizeof(cached) - 1);
        return cached;
    }

    static const char* HypervStatusStr() {
        for (const EnvCheck& c : kEnvChecks) {
            if (std::strcmp(c.name, "Hyper-V launch type") == 0) {
                return c.actualOn ? "Active" : "Inactive";
            }
        }
        return "Unknown";
    }

    static const char* BuildDateStr() {
        static char cached[40] = {0};
        if (cached[0]) return cached;
        const char* s = kLoaderBuildId;

        if (std::strlen(s) >= 15 && s[8] == '-') {
            auto digits = [&](int off, int len) {
                int v = 0;
                for (int i = 0; i < len; ++i) {
                    if (s[off + i] < '0' || s[off + i] > '9') return -1;
                    v = v * 10 + (s[off + i] - '0');
                }
                return v;
            };
            const int y = digits(0, 4);
            const int M = digits(4, 2);
            const int d = digits(6, 2);
            const int hh = digits(9, 2);
            const int mm = digits(11, 2);
            static const char* kMon[] = {
                "Jan","Feb","Mar","Apr","May","Jun",
                "Jul","Aug","Sep","Oct","Nov","Dec"
            };
            if (y > 0 && M >= 1 && M <= 12 && d >= 1 && d <= 31 &&
                hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
                std::snprintf(cached, sizeof(cached),
                    "%s %d, %d %02d:%02d",
                    kMon[M - 1], d, y, hh, mm);
                return cached;
            }
        }

        std::strncpy(cached, s, sizeof(cached) - 1);
        return cached;
    }

    static const char* SessionUptimeStr() {
        static char buf[32];
        const double elapsed = NowSec() - g.sessionStart;
        const int total = (int)(elapsed + 0.5);
        const int hh = total / 3600;
        const int mm = (total / 60) % 60;
        const int ss = total % 60;
        if (hh > 0) std::snprintf(buf, sizeof(buf), "%dh %dm %ds", hh, mm, ss);
        else if (mm > 0) std::snprintf(buf, sizeof(buf), "%dm %ds", mm, ss);
        else std::snprintf(buf, sizeof(buf), "%ds", ss);
        return buf;
    }

    static const char* WindowSizeStr() {
        static char buf[24];
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        std::snprintf(buf, sizeof(buf), "%d x %d",
            (int)(d.x + 0.5f), (int)(d.y + 0.5f));
        return buf;
    }

    static const char* ProcessIdStr() {
        static char buf[16] = {0};
        if (!buf[0]) {
            std::snprintf(buf, sizeof(buf), "%lu", ::GetCurrentProcessId());
        }
        return buf;
    }

    static void DrawSettingsPanel() {

        g.settingsPanelAnim = EaseTo(g.settingsPanelAnim,
                                       g.settingsPanelOpen ? 1.0f : 0.0f,
                                       14.0f);
        if (g.settingsPanelAnim < 0.001f && !g.settingsPanelOpen) return;

        g.settingsTabAnim = EaseTo(g.settingsTabAnim, 1.0f, 12.0f);

        const ImVec2 disp   = ImGui::GetIO().DisplaySize;

        const float panelW  = 460.0f;
        const float panelX  = kSidebarW + (g.settingsPanelAnim - 1.0f) * panelW;

        {
            ImGui::SetNextWindowPos({kSidebarW, 0});
            ImGui::SetNextWindowSize({disp.x - kSidebarW, disp.y});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
            ImGui::Begin("##settingsbackdrop", nullptr,
                ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoSavedSettings);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            BYTE bgA = (BYTE)(150.0f * g.settingsPanelAnim);
            dl->AddRectFilled({kSidebarW, 0}, disp,
                IM_COL32(8, 8, 12, bgA));

            if (g.settingsPanelOpen && ImGui::IsMouseClicked(0)) {
                const ImVec2 mp = ImGui::GetIO().MousePos;
                if (mp.x > panelX + panelW) g.settingsPanelOpen = false;
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }

        ImGui::SetNextWindowPos({panelX, 0});
        ImGui::SetNextWindowSize({panelW, disp.y});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
            ToImVec4(Color(20, 16, 34, 255)));
        ImGui::Begin("##settingspanel", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList*  dl  = ImGui::GetWindowDrawList();
        const ImVec2 win = ImGui::GetWindowPos();
        const Color  acc = Style::accentColor;

        dl->AddRectFilled(
            {win.x + panelW - 2.0f, win.y},
            {win.x + panelW,        win.y + disp.y},
            IM_COL32(acc.r, acc.g, acc.b, 110));

        const float tabStripW = 110.0f;
        const float tabH      = 40.0f;
        const float tabPadL   = 14.0f;
        const float tabTopY   = 76.0f;

        PushBigFont();
        const float titleY = 26.0f;
        dl->AddText({win.x + 22.0f, win.y + titleY},
                     ToImU32(Style::text), "Settings");
        PopFont();

        {
            const float cx = win.x + panelW - 18.0f - 14.0f;
            const float cy = win.y + titleY + 8.0f;
            const ImVec2 hitA = {cx - 14.0f, cy - 14.0f};
            const ImVec2 hitB = {cx + 14.0f, cy + 14.0f};
            const ImVec2 mp   = ImGui::GetIO().MousePos;
            const bool   hov  = mp.x >= hitA.x && mp.x <= hitB.x &&
                                mp.y >= hitA.y && mp.y <= hitB.y;
            ImU32 col = hov ? IM_COL32_WHITE : IM_COL32(170, 160, 200, 255);
            const float s = 6.0f;
            dl->AddLine({cx - s, cy - s}, {cx + s, cy + s}, col, 1.4f);
            dl->AddLine({cx - s, cy + s}, {cx + s, cy - s}, col, 1.4f);
            if (hov && ImGui::IsMouseClicked(0)) g.settingsPanelOpen = false;
        }

        dl->AddRectFilled(
            {win.x,             win.y + tabTopY - 16.0f},
            {win.x + tabStripW, win.y + disp.y},
            IM_COL32(20, 20, 26, 255));

        for (int i = 0; i < kSettingsTabCount; ++i) {
            const ImVec2 tA = {win.x,             win.y + tabTopY + i * tabH};
            const ImVec2 tB = {win.x + tabStripW, tA.y + tabH};

            const ImVec2 mp = ImGui::GetIO().MousePos;
            const bool   hov = mp.x >= tA.x && mp.x <= tB.x &&
                               mp.y >= tA.y && mp.y <= tB.y;
            const bool   active = (g.settingsActiveTab == i);

            if (active) {
                dl->AddRectFilled(tA, tB, IM_COL32(26, 20, 42, 255));

                dl->AddRectFilled(
                    {tA.x,        tA.y + tabH * 0.15f},
                    {tA.x + 3.0f, tB.y - tabH * 0.15f},
                    IM_COL32(acc.r, acc.g, acc.b, 255));
            } else if (hov) {
                dl->AddRectFilled(tA, tB, IM_COL32(255, 255, 255, 14));
            }

            PushBodyFont();
            const ImVec2 lts = ImGui::CalcTextSize(kSettingsTabs[i]);
            ImU32 col = active ? IM_COL32_WHITE
                       : hov                   ? IM_COL32(220, 210, 240, 255)
                                : IM_COL32(150, 140, 175, 255);
            dl->AddText({tA.x + tabPadL,
                          tA.y + (tabH - lts.y) * 0.5f},
                         col, kSettingsTabs[i]);
            PopFont();

            if (hov && ImGui::IsMouseClicked(0) && g.settingsActiveTab != i) {
                g.settingsLastTab     = g.settingsActiveTab;
                g.settingsTabSlideDir = (i > g.settingsActiveTab) ? +1 : -1;
                g.settingsActiveTab   = i;
                g.settingsTabAnim     = 0.0f;
            }
        }

        dl->AddLine(
            {win.x + tabStripW, win.y + tabTopY - 16.0f},
            {win.x + tabStripW, win.y + disp.y},
            IM_COL32(255, 255, 255, 14), 1.0f);

        const float contentAlpha = g.settingsTabAnim;
        const float slideY = (1.0f - g.settingsTabAnim) * 12.0f
                              * (float)g.settingsTabSlideDir;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha);
        ImGui::SetCursorPos({tabStripW + 22.0f, tabTopY + slideY});
        const float contentW = panelW - tabStripW - 22.0f * 2.0f;

        switch (g.settingsActiveTab) {
        case 0: {
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
            ImGui::TextUnformatted("DIAGNOSTICS");
            ImGui::PopStyleColor();
            PopFont();
            ImGui::Dummy({0, 6.0f});

            auto row = [&](const char* lab, const char* val, int id) {
                ImGui::SetCursorPosX(tabStripW + 22.0f);
                DrawCopyableRow(lab, val, id, contentW, contentAlpha);
            };
            row("Loader build",   kLoaderBuildId,   0);
            row("Built",          BuildDateStr(),   1);
            row("HWID",           kMockHwid,        2);
            row("OS version",     OsVersionStr(),   3);
            row("Hypervisor",     HypervStatusStr(),4);
            row("Session uptime", SessionUptimeStr(),5);
            row("Process ID",     ProcessIdStr(),   6);
            row("Window",         WindowSizeStr(),  7);

            ImGui::Dummy({0, 8.0f});
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
            ImGui::SetCursorPosX(tabStripW + 22.0f);
            ImGui::TextWrapped(
                "Copy any of these into a support ticket. Session "
                "uptime + window size update live; the rest are "
                "fixed for the session.");
            ImGui::PopStyleColor();
            PopFont();
            break;
        }
        case 1: {
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
            ImGui::TextUnformatted("LOG");
            ImGui::PopStyleColor();
            PopFont();

            ImGui::Dummy({0, 6.0f});
            ImGui::SetCursorPosX(tabStripW + 22.0f);
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::text));
            ImGui::Text("%zu entries", g.log.size());
            ImGui::PopStyleColor();
            PopFont();

            const float actBtnW = 64.0f;
            const float actBtnH = 22.0f;
            ImGui::SameLine();
            ImGui::SetCursorPosX(tabStripW + 22.0f + contentW
                                  - actBtnW * 2.0f - 6.0f);
            ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(32, 26, 50, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(44, 36, 66, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(56, 46, 82, 255));
            ImGui::PushStyleColor(ImGuiCol_Text,          ToImVec4(Style::text));
            if (ImGui::Button("Copy all", {actBtnW, actBtnH})) {
                std::string buf;
                buf.reserve(g.log.size() * 64);
                for (const auto& ln : g.log) {
                    buf.append(ln.text);
                    buf.push_back('\n');
                }
                if (!buf.empty() && OpenClipboard(nullptr)) {
                    EmptyClipboard();
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, buf.size() + 1);
                    if (hMem) {
                        void* p = GlobalLock(hMem);
                        if (p) {
                            std::memcpy(p, buf.data(), buf.size() + 1);
                            GlobalUnlock(hMem);
                            SetClipboardData(CF_TEXT, hMem);
                        }
                    }
                    CloseClipboard();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear", {actBtnW, actBtnH})) {
                g.log.clear();
            }
            ImGui::PopStyleColor(4);

            ImGui::Dummy({0, 8.0f});

            ImGui::SetCursorPosX(tabStripW + 22.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg,
                IM_COL32(18, 14, 28, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                ImVec2(10, 8));
            const float remainingH = disp.y - tabTopY - 120.0f;
            ImGui::BeginChild("##logbox",
                {contentW, remainingH < 120.0f ? 120.0f : remainingH},
                true,
                ImGuiWindowFlags_HorizontalScrollbar);

            PushBodyFont();
            if (g.log.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
                ImGui::TextUnformatted("(no log entries yet)");
                ImGui::PopStyleColor();
            } else {
                for (const auto& ln : g.log) {
                    const ImVec4 col = ln.ok
                        ? ToImVec4(Style::text)
                        : ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::TextUnformatted(ln.text.c_str());
                    ImGui::PopStyleColor();
                }

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            PopFont();

            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            break;
        }
        default: {
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
            ImGui::TextUnformatted("COMING SOON");
            ImGui::PopStyleColor();
            PopFont();

            ImGui::Dummy({0, 8.0f});
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::text));
            ImGui::SetCursorPosX(tabStripW + 22.0f);
            ImGui::TextWrapped(
                "This tab is reserved for future use. The layout is "
                "in place so adding content here later is just a "
                "matter of replacing this stub.");
            ImGui::PopStyleColor();
            PopFont();
            break;
        }
        }

        ImGui::PopStyleVar();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    static void DrawProceduralCover(ImDrawList* dl,
                                     const std::string& gameId,
                                     const std::string& displayName,
                                     ImVec2 a, ImVec2 b, float roundness) {
        const float w       = b.x - a.x;
        const float h       = b.y - a.y;
        const float maxDim  = (std::max)(w, h);
        const float baseHue = HueForGame(gameId);
        const float t       = (float)ImGui::GetTime();
        constexpr ImU32 cardBg = IM_COL32(22, 17, 36, 255);

        dl->AddRectFilled(a, b, cardBg, roundness);

        dl->PushClipRect(a, b, true);

        struct Orb {
            float xFrac, yFrac;
            float radFrac;
            float hueOffset;
            float sat, val;
            BYTE  peak;
            float driftAmp, driftPhase, pulsePhase;
        };

        static constexpr Orb orbs[] = {
            { 0.22f, 0.25f, 1.00f,   0.0f, 0.85f, 0.62f, 245, 16.0f, 0.00f, 0.0f },
            { 0.85f, 0.18f, 0.70f,  45.0f, 0.78f, 0.55f, 215, 14.0f, 1.70f, 2.3f },
            { 0.55f, 0.95f, 1.20f, -55.0f, 0.72f, 0.38f, 230, 20.0f, 3.10f, 4.6f },
            { 0.05f, 0.85f, 0.60f, 100.0f, 0.80f, 0.45f, 190, 12.0f, 4.50f, 1.2f },
        };

        for (const auto& orb : orbs) {
            const float dx = sinf(t * 0.35f + orb.driftPhase) * orb.driftAmp;
            const float dy = cosf(t * 0.28f + orb.driftPhase) * orb.driftAmp * 0.6f;
            const float cx = a.x + w * orb.xFrac + dx;
            const float cy = a.y + h * orb.yFrac + dy;

            const float pulse = 0.85f + 0.15f * sinf(t * 0.60f + orb.pulsePhase);
            const BYTE  peak  = (BYTE)((float)orb.peak * pulse);

            const float radius = maxDim * orb.radFrac;
            const ImU32 base   = HsvCol(baseHue + orb.hueOffset,
                                         orb.sat, orb.val, peak);

            constexpr int kRings = 18;
            for (int i = kRings; i >= 0; --i) {
                const float rFrac = (float)i / (float)kRings;
                float fall  = 1.0f - rFrac;
                fall = fall * fall * fall;
                const BYTE alpha = (BYTE)((float)peak * fall);
                if (alpha < 2) continue;
                const ImU32 col = (base & 0x00FFFFFFu)
                                   | ((ImU32)alpha << 24);
                dl->AddCircleFilled({cx, cy}, radius * rFrac, col, 28);
            }
        }

        {
            const float slope = 1.7f;
            const float step  = w * 0.30f;
            for (int i = -3; i <= 3; ++i) {
                const float xTop = a.x + w * 0.5f + (float)i * step;
                const ImVec2 p1 = { xTop,                 a.y };
                const ImVec2 p2 = { xTop + h / slope,     b.y };
                dl->AddLine(p1, p2, IM_COL32(255, 255, 255, 10), 1.0f);
            }
        }

        {
            ImFont* font = Render::Fonts::montserratBold16px;
            if (!font) font = ImGui::GetFont();

            float fontSize = h * 0.52f;
            ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f,
                                             displayName.c_str());

            if (ts.x > w * 0.92f) {
                fontSize *= (w * 0.92f) / ts.x;
                ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f,
                                          displayName.c_str());
            }
            const float tx = a.x + w * 0.5f  - ts.x * 0.5f;
            const float ty = a.y + h * 0.32f - ts.y * 0.5f;

            const ImU32 stroke = IM_COL32(0, 0, 0, 110);
            dl->AddText(font, fontSize, {tx - 1, ty},     stroke, displayName.c_str());
            dl->AddText(font, fontSize, {tx + 1, ty},     stroke, displayName.c_str());
            dl->AddText(font, fontSize, {tx,     ty - 1}, stroke, displayName.c_str());
            dl->AddText(font, fontSize, {tx,     ty + 1}, stroke, displayName.c_str());
            dl->AddText(font, fontSize, {tx,     ty},
                        IM_COL32(255, 255, 255, 46), displayName.c_str());
        }

        {
            const ImU32 hi   = HsvCol(baseHue + 20.0f, 0.85f, 0.95f, 60);
            const ImU32 fade = IM_COL32(0, 0, 0, 0);
            dl->AddRectFilledMultiColor(a, {b.x, a.y + 72.0f},
                                         hi, hi, fade, fade);
        }

        dl->PopClipRect();

        {
            const ImU32 bg  = ToImU32(Style::background);
            const float rr  = roundness;
            const int   seg = 8;

            dl->PathLineTo({a.x, a.y});
            dl->PathLineTo({a.x + rr, a.y});
            dl->PathArcTo({a.x + rr, a.y + rr}, rr, -IM_PI * 0.5f, -IM_PI, seg);
            dl->PathFillConvex(bg);

            dl->PathLineTo({b.x, a.y});
            dl->PathLineTo({b.x, a.y + rr});
            dl->PathArcTo({b.x - rr, a.y + rr}, rr, 0.0f, -IM_PI * 0.5f, seg);
            dl->PathFillConvex(bg);

            dl->PathLineTo({b.x, b.y});
            dl->PathLineTo({b.x - rr, b.y});
            dl->PathArcTo({b.x - rr, b.y - rr}, rr, IM_PI * 0.5f, 0.0f, seg);
            dl->PathFillConvex(bg);

            dl->PathLineTo({a.x, b.y});
            dl->PathLineTo({a.x, b.y - rr});
            dl->PathArcTo({a.x + rr, b.y - rr}, rr, IM_PI, IM_PI * 0.5f, seg);
            dl->PathFillConvex(bg);
        }
    }

    static bool DrawGameCard(ImDrawList* dl, const Entitlement& ent,
                              float anim, ImVec2 pos, ImVec2 size,
                              bool dragged) {

        constexpr float r = 12.0f;

        const bool inputAllowed = !g.envPanelOpen && g.detailModalEnt < 0;
        const float lift = dragged ? 14.0f : 6.0f * anim;
        const ImVec2 a = { pos.x,          pos.y - lift };
        const ImVec2 b = { pos.x + size.x, pos.y + size.y - lift };

        const ImVec2 mp  = ImGui::GetIO().MousePos;
        const bool   hov = inputAllowed && !dragged &&
                           mp.x >= a.x && mp.x <= b.x &&
                           mp.y >= a.y && mp.y <= b.y;

        {
            const float baseA = dragged ? 110.0f : (38.0f + 36.0f * anim);
            for (int i = 5; i >= 0; --i) {
                const float infl    = (i + 1) * (dragged ? 5.5f : 3.5f);
                const float yOff    = (dragged ? 5.0f : 2.0f) + (float)i * 1.8f;
                const float falloff = 1.0f - (float)i / 6.0f;
                const BYTE  alpha   = (BYTE)(baseA * falloff * falloff);
                if (alpha == 0) continue;
                dl->AddRectFilled(
                    { a.x - infl, a.y - infl + yOff },
                    { b.x + infl, b.y + infl + yOff },
                    IM_COL32(0, 0, 0, alpha),
                    r + infl);
            }
        }

        const Tex& tex = GetGameTexture(ent.game.c_str());
        if (tex.srv) {
            const float zoom = 0.035f * anim;
            ImVec2 uvA, uvB;
            CoverUVs(tex.w, tex.h, size, zoom, uvA, uvB);
            dl->AddImageRounded(
                reinterpret_cast<ImTextureID>(tex.srv),
                a, b, uvA, uvB,
                IM_COL32_WHITE, r);
        } else {
            DrawProceduralCover(dl, ent.game, GameDisplay(ent), a, b, r);
        }

        if (!dragged) {
            const BYTE washA = (BYTE)(130.0f * (1.0f - anim));
            if (washA > 0) {
                dl->AddRectFilled(a, b, IM_COL32(28, 28, 36, washA), r);
            }
        }

        {
            struct GL { float topF; BYTE alpha; };
            constexpr GL kLayers[] = {
                { 0.40f, 18 }, { 0.30f, 32 }, { 0.20f, 52 },
                { 0.11f, 78 }, { 0.05f, 110 },
            };
            const int bump = (int)(14.0f * anim);
            for (const auto& l : kLayers) {
                const int  capped = (std::min)(255, (int)l.alpha + bump);
                const BYTE alpha  = (BYTE)capped;
                dl->AddRectFilled(
                    { a.x, b.y - size.y * l.topF }, b,
                    IM_COL32(0, 0, 0, alpha),
                    r,
                    ImDrawFlags_RoundCornersBottom);
            }
        }

        {
            const BYTE borderA = (BYTE)(36 + 56.0f * anim);
            dl->AddRect(a, b,
                IM_COL32(255, 255, 255, borderA),
                r, 0, 1.0f);
        }

        const float padX       = 22.0f;
        const float padY       = 20.0f;
        const float titleShift = 3.0f * anim;

        std::string title = GameDisplay(ent);
        PushBigFont();
        const ImVec2 titleSz = ImGui::CalcTextSize(title.c_str());
        const float titleY = b.y - padY - titleSz.y - titleShift;

        const ImU32 stroke = IM_COL32(0, 0, 0, 140);
        dl->AddText({a.x + padX - 1, titleY    }, stroke, title.c_str());
        dl->AddText({a.x + padX + 1, titleY    }, stroke, title.c_str());
        dl->AddText({a.x + padX,     titleY - 1}, stroke, title.c_str());
        dl->AddText({a.x + padX,     titleY + 1}, stroke, title.c_str());
        dl->AddText({a.x + padX,     titleY    },
                    IM_COL32(230, 220, 248, 255), title.c_str());
        PopFont();

        BadgePill pill = ExpiryPill(ent);
        PushBodyFont();
        const ImVec2 pillSz = ImGui::CalcTextSize(pill.label.c_str());
        const float pillPadX = 10.0f;
        const float pillPadY = 5.0f;
        const float dotSpace = 12.0f;
        const float pillW    = pillSz.x + pillPadX * 2.0f + dotSpace;
        const float pillH    = pillSz.y + pillPadY * 2.0f;
        const float pillX    = b.x - 14.0f - pillW;
        const float pillY    = a.y + 14.0f;

        dl->AddRectFilled({pillX, pillY}, {pillX + pillW, pillY + pillH},
                           IM_COL32(0, 0, 0, 130), pillH * 0.5f);
        dl->AddRect({pillX, pillY}, {pillX + pillW, pillY + pillH},
                    IM_COL32(255, 255, 255, 40), pillH * 0.5f, 0, 1.0f);

        const float dotR = 3.5f;
        const ImVec2 dotC = { pillX + pillPadX + dotR, pillY + pillH * 0.5f };
        dl->AddCircleFilled(dotC, dotR + 2.0f,
            IM_COL32(pill.bg.r, pill.bg.g, pill.bg.b, 70), 14);
        dl->AddCircleFilled(dotC, dotR,
            IM_COL32(pill.bg.r, pill.bg.g, pill.bg.b, 255), 14);

        dl->AddText({pillX + pillPadX + dotSpace,
                      pillY + (pillH - pillSz.y) * 0.5f},
                    IM_COL32(232, 232, 240, 240),
                    pill.label.c_str());
        PopFont();

        return hov;
    }

    static void DrawGameGrid(const ImVec2& areaOrigin, const ImVec2& areaSize) {
        const int n = (int)g.ents.size();
        if (n == 0) {
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::dimmedText));
            ImGui::SetCursorPos({0, 0});
            ImGui::TextUnformatted("No entitlements linked to this account yet.");
            ImGui::PopStyleColor();
            PopFont();
            return;
        }

        while ((int)g.cardAnim.size()   < n) g.cardAnim.push_back(0.0f);
        if    ((int)g.cardAnim.size()   > n) g.cardAnim.resize(n);
        while ((int)g.cardOffset.size() < n) g.cardOffset.push_back({0.0f, 0.0f});
        if    ((int)g.cardOffset.size() > n) g.cardOffset.resize(n);

        while ((int)g.cardLastSlot.size()      < n) g.cardLastSlot.push_back({0.0f, 0.0f});
        if    ((int)g.cardLastSlot.size()      > n) g.cardLastSlot.resize(n);
        while ((int)g.cardLastSlotKnown.size() < n) g.cardLastSlotKnown.push_back(0);
        if    ((int)g.cardLastSlotKnown.size() > n) g.cardLastSlotKnown.resize(n);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 abs0 = ImGui::GetWindowPos();
        const ImVec2 origin = { abs0.x + areaOrigin.x, abs0.y + areaOrigin.y };

        const float gap = 20.0f;

        struct CardSlot { int idx; ImVec2 pos, sz; };
        std::vector<CardSlot> slots;
        slots.reserve(n);

        if (n == 1) {
            const float w = (std::min)(areaSize.x * 0.78f, 580.0f);
            const float h = (std::min)(areaSize.y * 0.82f, 340.0f);
            const float x = origin.x + (areaSize.x - w) * 0.5f;
            const float y = origin.y + (areaSize.y - h) * 0.5f;
            slots.push_back({ 0, { x, y }, { w, h } });
        }
        else if (n == 2) {
            const float w = (std::min)((areaSize.x - gap) * 0.46f, 380.0f);
            const float h = (std::min)(areaSize.y * 0.82f, 360.0f);
            const float pairW = w * 2 + gap;
            const float x = origin.x + (areaSize.x - pairW) * 0.5f;
            const float y = origin.y + (areaSize.y - h) * 0.42f;
            slots.push_back({ 0, { x,             y }, { w, h } });
            slots.push_back({ 1, { x + w + gap,   y }, { w, h } });
        }
        else if (n == 3) {
            const float leftW   = (areaSize.x - gap) * 0.56f;
            const float rightW  = (areaSize.x - gap) * 0.34f;
            const float totalH  = (std::min)(areaSize.y * 0.88f, 400.0f);
            const float rightH  = (totalH - gap) * 0.5f;
            const float x       = origin.x + (areaSize.x - (leftW + rightW + gap)) * 0.5f;
            const float y       = origin.y + (areaSize.y - totalH) * 0.5f;
            slots.push_back({ 0, { x,                       y                       }, { leftW,  totalH } });
            slots.push_back({ 1, { x + leftW + gap,         y                       }, { rightW, rightH } });
            slots.push_back({ 2, { x + leftW + gap,         y + rightH + gap        }, { rightW, rightH } });
        }
        else if (n == 4) {
            const float w = (std::min)((areaSize.x - gap) * 0.46f, 380.0f);
            const float h = (std::min)((areaSize.y - gap) * 0.46f, 230.0f);
            const float pairW = w * 2 + gap;
            const float pairH = h * 2 + gap;
            const float x = origin.x + (areaSize.x - pairW) * 0.5f;
            const float y = origin.y + (areaSize.y - pairH) * 0.5f;
            for (int i = 0; i < 4; ++i) {
                slots.push_back({ i,
                    { x + (i % 2) * (w + gap),
                      y + (i / 2) * (h + gap) },
                    { w, h } });
            }
        }
        else {

            struct SlotRect { ImVec2 pos, sz; };
            const float masonH = (std::min)(380.0f, areaSize.y);
            const float leftW   = (areaSize.x - gap) * 0.56f;
            const float rightW  = (areaSize.x - gap) * 0.44f;
            const float topH    = (masonH - gap) * 0.60f;
            const float botH    = (masonH - gap) * 0.40f;
            const float rightTopH = botH;
            const float rightBotH = topH;
            const float twoCardW  = (rightW - gap) * 0.5f;
            const SlotRect masonry[5] = {
                { { origin.x,                                origin.y                     }, { leftW,    topH      } },
                { { origin.x + leftW + gap,                  origin.y                     }, { rightW,   rightTopH } },
                { { origin.x,                                origin.y + topH + gap        }, { leftW,    botH      } },
                { { origin.x + leftW + gap,                  origin.y + rightTopH + gap   }, { twoCardW, rightBotH } },
                { { origin.x + leftW + gap + twoCardW + gap, origin.y + rightTopH + gap   }, { twoCardW, rightBotH } },
            };
            for (int i = 0; i < 5; ++i)
                slots.push_back({ i, masonry[i].pos, masonry[i].sz });

            if (n > 5) {
                const float overY = origin.y + masonH + gap;
                const int   cols  = 4;
                const float cw    = (areaSize.x - gap * (cols - 1)) / cols;
                const float ch    = 180.0f;
                for (int i = 5, oIdx = 0; i < n; ++i, ++oIdx) {
                    const int row = oIdx / cols;
                    const int col = oIdx % cols;
                    slots.push_back({
                        i,
                        { origin.x + col * (cw + gap),
                          overY + row * (ch + gap) },
                        { cw, ch }
                    });
                }
            }
        }

        const ImVec2 mp = ImGui::GetIO().MousePos;

        int hoverSlot = -1;
        for (const auto& s : slots) {
            if (mp.x >= s.pos.x && mp.x <= s.pos.x + s.sz.x &&
                mp.y >= s.pos.y && mp.y <= s.pos.y + s.sz.y) {
                hoverSlot = s.idx;
                break;
            }
        }

        const bool inputAllowed = !g.envPanelOpen;

        if (inputAllowed) {
            if (g.pressedCard < 0) {

                if (ImGui::IsMouseClicked(0) && hoverSlot >= 0) {
                    g.pressedCard     = hoverSlot;
                    g.pressMouseStart = mp;
                    g.dragActive      = false;

                    for (const auto& s : slots) {
                        if (s.idx == hoverSlot) {
                            g.dragGrabOffset = { mp.x - s.pos.x,
                                                  mp.y - s.pos.y };
                            g.dragSizeEased  = s.sz;
                            break;
                        }
                    }
                }
            } else {

                if (!g.dragActive) {
                    const float dx = mp.x - g.pressMouseStart.x;
                    const float dy = mp.y - g.pressMouseStart.y;
                    if (dx * dx + dy * dy > 6.0f * 6.0f) {
                        g.dragActive = true;
                    }
                }

                if (ImGui::IsMouseReleased(0)) {
                    if (g.dragActive) {

                        const ImVec2 dropVisual = {
                            mp.x - g.dragGrabOffset.x,
                            mp.y - g.dragGrabOffset.y,
                        };
                        const int destIdx = (hoverSlot >= 0 &&
                                              hoverSlot != g.pressedCard &&
                                              hoverSlot < (int)g.ents.size())
                                              ? hoverSlot : g.pressedCard;
                        ImVec2 destSlotPos = { 0.0f, 0.0f };
                        for (const auto& s : slots) {
                            if (s.idx == destIdx) {
                                destSlotPos = s.pos;
                                break;
                            }
                        }

                        if (destIdx != g.pressedCard) {
                            std::swap(g.ents[g.pressedCard],
                                       g.ents[destIdx]);
                            std::swap(g.cardAnim[g.pressedCard],
                                       g.cardAnim[destIdx]);
                            Log("Reordered: " +
                                  GameDisplay(g.ents[destIdx]) + " ↔ " +
                                  GameDisplay(g.ents[g.pressedCard]));
                        }

                        g.cardOffset[destIdx].x = dropVisual.x - destSlotPos.x;
                        g.cardOffset[destIdx].y = dropVisual.y - destSlotPos.y;
                    } else if (g.pressedCard >= 0 &&
                                g.pressedCard < (int)g.ents.size()) {

                        for (const auto& s : slots) {
                            if (s.idx == g.pressedCard) {
                                g.detailModalSrcPos  = s.pos;
                                g.detailModalSrcSize = s.sz;
                                break;
                            }
                        }
                        g.detailModalEnt = g.pressedCard;
                        Log("Opened: " +
                              GameDisplay(g.ents[g.pressedCard]));
                    }
                    g.pressedCard = -1;
                    g.dragActive  = false;
                }
            }
        }

        const bool dragInFlight = g.dragActive
                                   && g.pressedCard >= 0
                                   && g.pressedCard < (int)g.ents.size();

        const bool hasHeroLayout = (n == 3) || (n >= 5);

        if (dragInFlight && hasHeroLayout && !slots.empty()) {

            std::vector<int> rightIdxs;
            if (n == 3) {
                rightIdxs = { 1, 2 };
            } else {
                rightIdxs = { 1, 3, 4 };
            }

            const CardSlot& hero = slots[0];
            const float thresholdX = hero.pos.x + hero.sz.x + gap * 0.5f;

            const ImVec2 dragTL = { mp.x - g.dragGrabOffset.x,
                                     mp.y - g.dragGrabOffset.y };
            const float dragCx = dragTL.x + g.dragSizeEased.x * 0.5f;

            int swapWith = -1;
            if (g.pressedCard == 0 && dragCx > thresholdX) {

                float bestDy = std::numeric_limits<float>::max();
                for (int idx : rightIdxs) {
                    if (idx >= (int)slots.size()) continue;
                    const auto& s = slots[idx];
                    const float cy = s.pos.y + s.sz.y * 0.5f;
                    const float dy = std::fabsf(cy - mp.y);
                    if (dy < bestDy) { bestDy = dy; swapWith = idx; }
                }
            } else {
                bool inRight = false;
                for (int idx : rightIdxs)
                    if (idx == g.pressedCard) { inRight = true; break; }
                if (inRight && dragCx < thresholdX) {

                    swapWith = 0;
                }
            }

            if (swapWith >= 0 && swapWith != g.pressedCard
                              && swapWith < (int)g.ents.size()) {

                ImVec2 oldSize = g.dragSizeEased;
                ImVec2 newSize = slots[swapWith].sz;

                std::swap(g.ents[g.pressedCard],     g.ents[swapWith]);
                std::swap(g.cardAnim[g.pressedCard], g.cardAnim[swapWith]);

                Log("Swapped (mid-drag): " +
                      GameDisplay(g.ents[swapWith]) + " ↔ " +
                      GameDisplay(g.ents[g.pressedCard]));

                if (oldSize.x > 0.001f && oldSize.y > 0.001f) {
                    const float rx = g.dragGrabOffset.x / oldSize.x;
                    const float ry = g.dragGrabOffset.y / oldSize.y;
                    g.dragGrabOffset.x = rx * newSize.x;
                    g.dragGrabOffset.y = ry * newSize.y;
                }
                g.pressedCard = swapWith;
            }
        }

        ImVec2 dragCentre = { 0.0f, 0.0f };
        ImVec2 dragSize   = { 200.0f, 270.0f };
        if (dragInFlight) {

            ImVec2 targetSize = { 200.0f, 270.0f };
            for (const auto& s : slots) {
                if (s.idx == g.pressedCard) { targetSize = s.sz; break; }
            }
            g.dragSizeEased.x = EaseTo(g.dragSizeEased.x, targetSize.x, 14.0f);
            g.dragSizeEased.y = EaseTo(g.dragSizeEased.y, targetSize.y, 14.0f);
            dragSize = g.dragSizeEased;
            const ImVec2 dragTL = { mp.x - g.dragGrabOffset.x,
                                     mp.y - g.dragGrabOffset.y };
            dragCentre = { dragTL.x + dragSize.x * 0.5f,
                           dragTL.y + dragSize.y * 0.5f };
        }

        for (const auto& s : slots) {
            if (!g.cardLastSlotKnown[s.idx]) {
                g.cardLastSlot[s.idx]      = s.pos;
                g.cardLastSlotKnown[s.idx] = 1;
                continue;
            }
            const float dx = g.cardLastSlot[s.idx].x - s.pos.x;
            const float dy = g.cardLastSlot[s.idx].y - s.pos.y;
            if (dx != 0.0f || dy != 0.0f) {
                const bool isDragged = g.dragActive && g.pressedCard == s.idx;
                if (!isDragged) {
                    g.cardOffset[s.idx].x += dx;
                    g.cardOffset[s.idx].y += dy;
                }
                g.cardLastSlot[s.idx] = s.pos;
            }
        }

        constexpr float kRepelRange    = 280.0f;
        constexpr float kRepelStrength = 1.30f;
        constexpr float kRepelEase     = 12.0f;
        constexpr float kSnapBackEase  = 7.0f;
        for (const auto& s : slots) {
            ImVec2 target = { 0.0f, 0.0f };
            if (dragInFlight && s.idx != g.pressedCard) {
                const ImVec2 slotCentre = {
                    s.pos.x + s.sz.x * 0.5f,
                    s.pos.y + s.sz.y * 0.5f,
                };
                const float dx = slotCentre.x - dragCentre.x;
                const float dy = slotCentre.y - dragCentre.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 > 0.5f) {
                    const float d   = sqrtf(d2);
                    const float fall = kRepelRange - d;
                    if (fall > 0.0f) {
                        const float k = kRepelStrength * fall / d;
                        target = { dx * k, dy * k };
                    }
                }
            }
            const float ease = dragInFlight ? kRepelEase : kSnapBackEase;
            g.cardOffset[s.idx].x = EaseTo(g.cardOffset[s.idx].x, target.x, ease);
            g.cardOffset[s.idx].y = EaseTo(g.cardOffset[s.idx].y, target.y, ease);
        }

        for (const auto& s : slots) {
            if (g.dragActive && g.pressedCard == s.idx) continue;

            const ImVec2 drawPos = {
                s.pos.x + g.cardOffset[s.idx].x,
                s.pos.y + g.cardOffset[s.idx].y,
            };
            const bool hov = DrawGameCard(dl, g.ents[s.idx],
                                            g.cardAnim[s.idx],
                                            drawPos, s.sz, false);
            const bool target = !g.dragActive && hov;
            g.cardAnim[s.idx] = EaseTo(g.cardAnim[s.idx],
                                        target ? 1.0f : 0.0f, 18.0f);
        }

        if (dragInFlight) {
            const ImVec2 dragPos = { mp.x - g.dragGrabOffset.x,
                                      mp.y - g.dragGrabOffset.y };
            DrawGameCard(dl, g.ents[g.pressedCard],
                          1.0f, dragPos, dragSize, true);
        }
    }

    static void DrawHomeScreen() {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;

        const float panelW = kDetailPanelW * g.detailExpandAnim;
        ImGui::SetNextWindowPos({kSidebarW, kTopBarH});
        ImGui::SetNextWindowSize({disp.x - kSidebarW - panelW,
                                   disp.y - kTopBarH});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                             ImVec2(44.0f, 32.0f));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##home", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 winPos  = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();

        dl->AddRectFilled(
            winPos,
            { winPos.x + winSize.x, winPos.y + winSize.y },
            IM_COL32(0, 0, 0, (BYTE)(255.0f * 0.62f)));

        dl->AddRectFilledMultiColor(
            { winPos.x,               winPos.y },
            { winPos.x + winSize.x,   winPos.y + 32.0f },
            IM_COL32(0, 0, 0, 130), IM_COL32(0, 0, 0, 130),
            IM_COL32(0, 0, 0, 0),   IM_COL32(0, 0, 0, 0));
        dl->AddRectFilledMultiColor(
            { winPos.x,            winPos.y },
            { winPos.x + 28.0f,    winPos.y + winSize.y },
            IM_COL32(0, 0, 0, 100), IM_COL32(0, 0, 0, 0),
            IM_COL32(0, 0, 0, 0),   IM_COL32(0, 0, 0, 100));

        const ImVec2 cursor0 = ImGui::GetCursorScreenPos();
        const float  rightEdge = winPos.x + winSize.x - 44.0f;
        const Color  acc       = Style::accentColor;
        const int    activeN   = (int)g.ents.size();

        const char* greeting;
        {
            const std::time_t now = std::time(nullptr);
            std::tm lt;
        #if defined(_MSC_VER)
            ::localtime_s(&lt, &now);
        #else
            lt = *std::localtime(&now);
        #endif
            const int h = lt.tm_hour;
            if      (h >=  5 && h < 12) greeting = "GOOD MORNING";
            else if (h >= 12 && h < 17) greeting = "GOOD AFTERNOON";
            else if (h >= 17 && h < 22) greeting = "GOOD EVENING";
            else                         greeting = "LATE NIGHT";
        }

        dl->AddRectFilled(
            { cursor0.x,        cursor0.y + 3.0f },
            { cursor0.x + 3.0f, cursor0.y + 17.0f },
            ToImU32(acc), 1.5f);
        PushBodyFont();
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(acc.r / 255.0f, acc.g / 255.0f, acc.b / 255.0f, 0.88f));
        ImGui::SetCursorScreenPos({cursor0.x + 14.0f, cursor0.y + 2.0f});
        ImGui::TextUnformatted(greeting);
        ImGui::PopStyleColor();
        PopFont();
        ImGui::Dummy({0, 6.0f});

        std::string displayName;
        if (g_AvatarState.load(std::memory_order_acquire) >= 2) {

            displayName = !g_DiscordGlobalName.empty()
                          ? g_DiscordGlobalName
                          : g_DiscordUsername;
        }
        if (displayName.empty()) displayName = "there";

        const ImVec2 headlineAt = ImGui::GetCursorScreenPos();
        PushBigFont();
        const char* lead = "Welcome back, ";
        const ImVec2 leadSz = ImGui::CalcTextSize(lead);
        const ImVec2 nameSz = ImGui::CalcTextSize(displayName.c_str());
        dl->AddText({headlineAt.x, headlineAt.y},
                    IM_COL32(240, 230, 255, 255), lead);

        float nameX = headlineAt.x + leadSz.x;
        const float avatarS = 24.0f;
        const float avatarGap = 8.0f;
        if (g_AvatarSrv) {
            const float avatarY = headlineAt.y + (leadSz.y - avatarS) * 0.5f;
            const ImVec2 avA = { nameX, avatarY };
            const ImVec2 avB = { nameX + avatarS, avatarY + avatarS };

            const float ringR = avatarS * 0.5f + 1.5f;
            const ImVec2 ringC = { avA.x + avatarS * 0.5f,
                                    avA.y + avatarS * 0.5f };
            dl->AddCircleFilled(ringC, ringR,
                IM_COL32(acc.r, acc.g, acc.b, 90), 24);
            dl->AddImageRounded(
                reinterpret_cast<ImTextureID>(g_AvatarSrv),
                avA, avB,
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32_WHITE, avatarS * 0.5f);
            nameX += avatarS + avatarGap;
        }

        dl->AddText({nameX, headlineAt.y},
                    ToImU32(acc), displayName.c_str());
        PopFont();

        ImGui::Dummy({0, leadSz.y + 10.0f});

        if (!g.licenseKey.empty()) {
            char sub[96];
            if (g.licenseKey.size() >= 10) {
                std::snprintf(sub, sizeof(sub),
                               "Key %.4s…%s",
                               g.licenseKey.c_str(),
                               g.licenseKey.c_str() + g.licenseKey.size() - 4);
            } else {
                std::snprintf(sub, sizeof(sub),
                               "Key %s", g.licenseKey.c_str());
            }
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(0.65f, 0.65f, 0.74f, 0.95f));
            ImGui::TextUnformatted(sub);
            ImGui::PopStyleColor();
            PopFont();
        }

        {
            struct Chip {
                const char* label;
                bool        hasDot;
                Color       dotColor;
            };
            char activeBuf[24];
            std::snprintf(activeBuf, sizeof(activeBuf),
                           "%d ACTIVE", activeN);
            const Chip chips[] = {
                { "HWID OK",   true,  Color(34, 197, 94)  },
                { "v14.2",     false, Color(0, 0, 0)      },
                { activeBuf,   true,  acc                  },
            };

            PushBodyFont();
            const float chipH    = 26.0f;
            const float chipPadX = 11.0f;
            const float chipGap  = 8.0f;
            const float dotSpace = 14.0f;
            float totalW = 0.0f;
            for (const auto& c : chips) {
                const ImVec2 ts = ImGui::CalcTextSize(c.label);
                totalW += ts.x + chipPadX * 2.0f
                       + (c.hasDot ? dotSpace : 0.0f);
            }
            totalW += chipGap * (sizeof(chips) / sizeof(chips[0]) - 1);
            PopFont();

            float chipX = rightEdge - totalW;
            const float chipY = cursor0.y - 2.0f;

            for (const auto& c : chips) {
                PushBodyFont();
                const ImVec2 ts = ImGui::CalcTextSize(c.label);
                PopFont();
                const float w = ts.x + chipPadX * 2.0f
                              + (c.hasDot ? dotSpace : 0.0f);
                const ImVec2 a = { chipX, chipY };
                const ImVec2 b = { chipX + w, chipY + chipH };

                dl->AddRectFilled(a, b,
                    IM_COL32(255, 255, 255, 16), chipH * 0.5f);
                dl->AddRect(a, b,
                    IM_COL32(255, 255, 255, 28), chipH * 0.5f, 0, 1.0f);

                if (c.hasDot) {
                    const float dotR = 3.5f;
                    const ImVec2 dotC = { a.x + chipPadX + dotR,
                                           a.y + chipH * 0.5f };

                    dl->AddCircleFilled(dotC, dotR + 2.5f,
                        IM_COL32(c.dotColor.r, c.dotColor.g,
                                  c.dotColor.b, 70), 14);
                    dl->AddCircleFilled(dotC, dotR,
                        IM_COL32(c.dotColor.r, c.dotColor.g,
                                  c.dotColor.b, 255), 14);
                }

                PushBodyFont();
                dl->AddText({ a.x + chipPadX
                                + (c.hasDot ? dotSpace : 0.0f),
                              a.y + (chipH - ts.y) * 0.5f },
                            IM_COL32(225, 225, 235, 240),
                            c.label);
                PopFont();

                chipX += w + chipGap;
            }
        }

        if (!g.entsError.empty()) {
            ImGui::Dummy({0, 8});
            PushBodyFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Style::warning));
            ImGui::TextUnformatted(g.entsError.c_str());
            ImGui::PopStyleColor();
            PopFont();
        }

        ImGui::Dummy({0, 26});

        {
            const ImVec2 secAt = ImGui::GetCursorScreenPos();
            PushBigFont();
            const ImVec2 ts = ImGui::CalcTextSize("Your Library");
            dl->AddText({secAt.x, secAt.y},
                        IM_COL32(230, 220, 246, 255), "Your Library");
            PopFont();

            PushBodyFont();
            char hint[48];
            std::snprintf(hint, sizeof(hint),
                           "%d %s · drag to rearrange",
                           activeN,
                           activeN == 1 ? "game" : "games");
            const ImVec2 hintSz = ImGui::CalcTextSize(hint);
            dl->AddText({ rightEdge - hintSz.x,
                          secAt.y + (ts.y - hintSz.y) * 0.5f },
                        IM_COL32(160, 160, 178, 220), hint);
            PopFont();

            ImGui::Dummy({0, ts.y + 8.0f});

            const ImVec2 ruleAt = ImGui::GetCursorScreenPos();
            const float ruleY = ruleAt.y - 2.0f;
            const float ruleLen = 280.0f;
            dl->AddRectFilledMultiColor(
                { secAt.x,           ruleY },
                { secAt.x + ruleLen, ruleY + 1.5f },
                IM_COL32(acc.r, acc.g, acc.b, 200),
                IM_COL32(acc.r, acc.g, acc.b, 0),
                IM_COL32(acc.r, acc.g, acc.b, 0),
                IM_COL32(acc.r, acc.g, acc.b, 200));

            dl->AddLine({ secAt.x + ruleLen + 8.0f, ruleY + 0.5f },
                        { rightEdge,                ruleY + 0.5f },
                        IM_COL32(255, 255, 255, 18), 1.0f);

            ImGui::Dummy({0, 18.0f});
        }

        const ImVec2 areaOrigin = ImGui::GetCursorPos();
        const ImVec2 areaSize   = ImGui::GetContentRegionAvail();
        DrawGameGrid(areaOrigin, areaSize);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    constexpr ImVec2 kDetailDstSize = { 640.0f, 480.0f };

    static std::string FormatTimeLeft(int64_t secs) {
        if (secs < 0)  return "Lifetime — never expires";
        if (secs == 0) return "Expired";
        const int days  = (int)(secs / 86400);
        const int hours = (int)((secs % 86400) / 3600);
        if (days >= 30) {
            const int months = days / 30;
            return std::string("Expires in ") + std::to_string(months)
                 + (months == 1 ? " month" : " months");
        }
        if (days >= 1) {
            std::string out = std::string("Expires in ")
                             + std::to_string(days)
                             + (days == 1 ? " day" : " days");
            if (hours > 0) {
                out += ", " + std::to_string(hours)
                     + (hours == 1 ? " hour" : " hours");
            }
            return out;
        }
        if (hours >= 1) {
            return std::string("Expires in ") + std::to_string(hours)
                 + (hours == 1 ? " hour" : " hours");
        }
        const int mins = (int)(secs / 60);
        return std::string("Expires in ") + std::to_string((mins < 1 ? 1 : mins))
             + (mins == 1 ? " minute" : " minutes");
    }

    static void DriveDetailModal() {
        const float target = (g.detailModalEnt >= 0) ? 1.0f : 0.0f;

        g.detailModalAnim = EaseTo(g.detailModalAnim, target, 6.0f);
    }

    enum class InjectStatus { Idle, Running, Done, Failed };

    enum class InjectPhase {
        None,
        Cleaning,
        HvProbe,
        HvInstalling,
        DriverProbe,
        DriverInstalling,
        SpooferRunning,

        RestartPrompt,
        WaitForGame,
        Fetching,
        Starting,
        Complete,
    };

    using HvInstallHookFn     = bool(*)(std::string& err);
    using HvRebootHookFn      = void(*)();

    using MapperInstallHookFn = bool(*)(std::string& err);

    using DriverProbeHookFn   = bool(*)();
    inline static HvInstallHookFn     g_hvInstallHook     = nullptr;
    inline static HvRebootHookFn      g_hvRebootHook      = nullptr;
    inline static MapperInstallHookFn g_mapperInstallHook = nullptr;
    inline static DriverProbeHookFn   g_driverProbeHook   = nullptr;

    inline static std::string g_runtimeMode = "mapper";

    enum class UpdatePhase {
        None,
        Checking,
        LatestVersion,
        Announce,
        Downloading,
        Failed
    };
    struct UpdateState {
        UpdatePhase phase       = UpdatePhase::None;
        DWORD       phaseStart  = 0;
        int         progress    = -1;
        float       easedFill   = 0.0f;
        float       overlayAnim = 0.0f;
        std::string err;
    };
    inline static UpdateState g_upd;

    using InjectHookFn = bool(*)(const char* gameId, std::string& err);
    inline static InjectHookFn g_injectHook = nullptr;

    struct InjectLogLine;
    inline static std::atomic<int>         g_installStatus{ 0 };
    inline static std::string              g_installError;
    inline static std::mutex               g_injPendingLogMu;

    inline static std::atomic<int>         g_injectStatus{ 0 };
    inline static std::string              g_injectError;

    struct InjectLogLine {
        std::string text;

        int kind = 0;
    };

    inline static std::deque<InjectLogLine> g_injPendingLog;

    struct InjectionState {
        InjectStatus status        = InjectStatus::Idle;
        InjectPhase  phase         = InjectPhase::None;
        DWORD        phaseTick     = 0;
        DWORD        gameWaitStart = 0;
        DWORD        lastHeartbeat = 0;
        int          progress      = 0;
        std::string  stage         = "Idle";
        std::string  forGame;
        std::vector<InjectLogLine> log;
    };
    inline static InjectionState g_inj;

    using SpooferHookFn = bool(*)(bool enable, std::string& err);
    inline static SpooferHookFn       g_spooferHook  = nullptr;
    inline static std::atomic<bool>   g_spooferOn    { false };
    inline static std::atomic<bool>   g_spooferBusy  { false };
    inline static float               g_spooferAnim  = 0.0f;
    inline static float               g_spooferGlow  = 0.0f;
    inline static std::string         g_spooferErr;

    static bool ProbeHypervisor() {
        auto probe = [](uint32_t primary, uint32_t secondary) -> bool {
            auto encode = [&](uint32_t call_type) -> uint32_t {
                return primary | (call_type << 16) | (secondary << 20);
            };
            int r1[4] = { 0, 0, 0, 0 };
            int r2[4] = { 0, 0, 0, 0 };
            __cpuidex(r1, (int)primary, (int)encode(9));
            __cpuidex(r2, (int)primary, (int)encode(12));

            if (r1[0] == 0 && r1[1] == 0 && r1[2] == 0 && r1[3] == 0 &&
                r2[0] == 0 && r2[1] == 0 && r2[2] == 0 && r2[3] == 0) {
                return false;
            }

            if (r1[0] == r2[0] && r1[1] == r2[1] &&
                r1[2] == r2[2] && r1[3] == r2[3]) {
                return false;
            }
            return true;
        };

        if (probe(0x4E47, 0x7F)) return true;
        if (probe(0xA057, 0x79)) return true;
        return false;
    }

    static const std::vector<std::wstring>& GameProcessNames(const std::string& gameId) {
        static const std::vector<std::wstring> kDbd = {
            L"DeadByDaylight-Win64-Shipping.exe",
            L"DeadByDaylight-EGS-Shipping.exe",
            L"DeadByDaylight-WinGDK-Shipping.exe",
            L"DeadByDaylight.exe",
        };
        static const std::vector<std::wstring> kApex = {
            L"r5apex.exe", L"r5apex_dx12.exe",
        };
        static const std::vector<std::wstring> kRust = {
            L"RustClient.exe",
        };
        static const std::vector<std::wstring> kEmpty;
        if (gameId == "dbd")  return kDbd;
        if (gameId == "apex") return kApex;
        if (gameId == "rust") return kRust;
        return kEmpty;
    }

    static bool IsGameRunning(const std::vector<std::wstring>& wanted) {
        if (wanted.empty()) return false;
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        bool found = false;
        if (::Process32FirstW(snap, &pe)) {
            do {
                for (const auto& name : wanted) {
                    if (::_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                        found = true;
                        break;
                    }
                }
            } while (!found && ::Process32NextW(snap, &pe));
        }
        ::CloseHandle(snap);
        return found;
    }

    static void PushPendingLog(const std::string& s, int kind) {
        std::lock_guard<std::mutex> lk(g_injPendingLogMu);
        g_injPendingLog.push_back({ s, kind });
    }

    static void DoHvInstallWorker() {
        g_installStatus.store(0);
        g_installError.clear();

        PushPendingLog("[hv]     Mounting EFI System Partition…", 0);

        bool ok = false;
        std::string err;
        if (g_hvInstallHook) {

            ok = g_hvInstallHook(err);
        } else {

            for (int i = 0; i < 6; ++i) ::Sleep(450);
            ok = true;
        }

        if (ok) {
            PushPendingLog("[hv]     Dependencies installed.",                            1);
            PushPendingLog("[hv]     Restart your PC, then re-launch the loader.",        0);
            PushPendingLog("[hv]     After reboot the HV is live and offscript will run.", 0);
            g_installStatus.store(1);
        } else {
            g_installError = err.empty() ? "Install failed." : err;
            PushPendingLog("[hv]     " + g_installError, 2);
            g_installStatus.store(2);
        }
    }

    static void DoDriverInstallWorker() {
        g_installStatus.store(0);
        g_installError.clear();

        PushPendingLog("[drv]    Fetching mapper package…", 0);

        bool ok = false;
        std::string err;
        if (g_mapperInstallHook) {
            ok = g_mapperInstallHook(err);
        } else {

            for (int i = 0; i < 4; ++i) ::Sleep(450);
            ok = true;
        }

        if (ok) {
            PushPendingLog("[drv]    Kernel driver armed.", 1);
            g_installStatus.store(1);
        } else {
            g_installError = err.empty() ? "Driver install failed." : err;
            PushPendingLog("[drv]    " + g_installError, 2);
            g_installStatus.store(2);
        }
    }

    inline static std::atomic<int>  g_spooferRunStatus{ 0 };
    inline static std::string       g_spooferRunError;

    static void DoSpooferWorker() {
        g_spooferRunStatus.store(0);
        g_spooferRunError.clear();
        g_spooferBusy.store(true, std::memory_order_release);

        PushPendingLog("[spoof]  Arming spoofer…", 0);

        bool ok = false;
        std::string err;
        if (g_spooferHook) {
            ok = g_spooferHook(true, err);
        } else {

            ::Sleep(900);
            ok = true;
            err = "(no spoofer hook wired)";
        }

        if (ok) {
            if (err.empty()) {
                PushPendingLog("[spoof]  Spoofer armed.", 1);
            } else {

                PushPendingLog(("[spoof]  Spoofer armed. " + err).c_str(), 1);
            }
            g_spooferRunStatus.store(1);
        } else {
            g_spooferRunError = err.empty() ? "Spoofer arm failed." : err;
            PushPendingLog(("[spoof]  " + g_spooferRunError).c_str(), 2);
            g_spooferRunStatus.store(2);
        }

        g_spooferBusy.store(false, std::memory_order_release);
    }

    static void DoInjectWorker(std::string gameId) {
        g_injectStatus.store(0);
        g_injectError.clear();

        bool ok = false;
        std::string err;
        if (g_injectHook) {

            ok = g_injectHook(gameId.c_str(), err);
        } else {

            PushPendingLog("[net]    Downloading package…", 0);
            ::Sleep(1200);
            PushPendingLog("[crypto] Package verified.",    1);
            ::Sleep(400);
            PushPendingLog("[inject] Starting cheat…",      0);
            ::Sleep(600);
            ok = true;
        }

        if (ok) {
            PushPendingLog("[done]   Injection complete.", 1);
            g_injectStatus.store(1);
        } else {
            g_injectError = err.empty() ? "Injection failed." : err;
            PushPendingLog("[err]    " + g_injectError, 2);
            g_injectStatus.store(2);
        }
    }

    static bool DisableEnabledStartupApps() {
        struct Pair { HKEY root; const wchar_t* run; const wchar_t* approved; };
        static const Pair kPairs[] = {
            { HKEY_CURRENT_USER,
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run" },
            { HKEY_LOCAL_MACHINE,
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run" },
            { HKEY_LOCAL_MACHINE,
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32" },
        };
        bool flipped = false;
        for (const auto& p : kPairs) {
            HKEY hRun = nullptr;
            if (RegOpenKeyExW(p.root, p.run, 0, KEY_READ, &hRun) != ERROR_SUCCESS) continue;
            HKEY hApp = nullptr;
            RegCreateKeyExW(p.root, p.approved, 0, nullptr, 0,
                            KEY_READ | KEY_WRITE, nullptr, &hApp, nullptr);
            for (DWORD i = 0; ; ++i) {
                wchar_t name[512]; DWORD nameLen = 512;
                LONG r = RegEnumValueW(hRun, i, name, &nameLen,
                                        nullptr, nullptr, nullptr, nullptr);
                if (r == ERROR_NO_MORE_ITEMS) break;
                if (r != ERROR_SUCCESS) continue;
                bool enabled = true;
                if (hApp) {
                    BYTE blob[12] = {};
                    DWORD blobLen = sizeof(blob);
                    if (RegQueryValueExW(hApp, name, nullptr, nullptr,
                                         blob, &blobLen) == ERROR_SUCCESS &&
                        blobLen >= 1) {
                        enabled = ((blob[0] & 1) == 0);
                    }
                }
                if (enabled && hApp) {
                    BYTE off[12] = { 0x03, 0,0,0, 0,0,0,0, 0,0,0,0 };
                    if (RegSetValueExW(hApp, name, 0, REG_BINARY,
                                       off, sizeof(off)) == ERROR_SUCCESS) {
                        flipped = true;
                    }
                }
            }
            if (hApp)  RegCloseKey(hApp);
            RegCloseKey(hRun);
        }

        struct Folder { HKEY root; const wchar_t* approved; };
        static const Folder kFolders[] = {
            { HKEY_CURRENT_USER,
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder" },
            { HKEY_LOCAL_MACHINE,
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder" },
        };
        for (const auto& f : kFolders) {
            HKEY h = nullptr;
            if (RegOpenKeyExW(f.root, f.approved, 0,
                              KEY_READ | KEY_WRITE, &h) != ERROR_SUCCESS) continue;
            for (DWORD i = 0; ; ++i) {
                wchar_t name[512]; DWORD nameLen = 512;
                LONG r = RegEnumValueW(h, i, name, &nameLen,
                                        nullptr, nullptr, nullptr, nullptr);
                if (r == ERROR_NO_MORE_ITEMS) break;
                if (r != ERROR_SUCCESS) continue;
                BYTE blob[12] = {};
                DWORD blobLen = sizeof(blob);
                if (RegQueryValueExW(h, name, nullptr, nullptr,
                                     blob, &blobLen) != ERROR_SUCCESS) continue;
                if (blobLen >= 1 && (blob[0] & 1) == 0) {
                    blob[0] |= 0x01;
                    if (RegSetValueExW(h, name, 0, REG_BINARY,
                                       blob, blobLen) == ERROR_SUCCESS) {
                        flipped = true;
                    }
                }
            }
            RegCloseKey(h);
        }
        return flipped;
    }

    static void StartInjection(const std::string& gameId) {
        if (DisableEnabledStartupApps()) {
            g_inj = {};
            g_inj.status   = InjectStatus::Failed;
            g_inj.phase    = InjectPhase::None;
            g_inj.forGame  = gameId;
            g_inj.stage    = "Restart PC";
            g_inj.progress = 0;
            g_inj.log.push_back({
                "[sys]    Startup apps were enabled. Disabled them — please restart your PC, then try again.", 2 });
            return;
        }

        g_inj = {};
        g_inj.status     = InjectStatus::Running;
        g_inj.phase      = InjectPhase::Cleaning;
        g_inj.phaseTick  = ::GetTickCount();
        g_inj.forGame    = gameId;
        g_inj.stage      = "Cleaning environment";
        g_inj.progress   = 4;
        g_inj.log.push_back({ "[boot]   Cleaning environment…", 0 });

        g_installStatus.store(0);
        g_installError.clear();
        g_injectStatus.store(0);
        g_injectError.clear();
        {
            std::lock_guard<std::mutex> lk(g_injPendingLogMu);
            g_injPendingLog.clear();
        }
    }

    static void UpdateInjection() {
        if (g_inj.status != InjectStatus::Running) return;
        const DWORD now     = ::GetTickCount();
        const DWORD elapsed = now - g_inj.phaseTick;

        {
            std::lock_guard<std::mutex> lk(g_injPendingLogMu);
            while (!g_injPendingLog.empty()) {
                g_inj.log.push_back(g_injPendingLog.front());
                g_injPendingLog.pop_front();
            }
        }

        auto enter = [&](InjectPhase next, const std::string& stage,
                          const char* line, int kind, int prog) {
            g_inj.phase     = next;
            g_inj.phaseTick = now;
            g_inj.stage     = stage;
            g_inj.progress  = prog;
            if (line) g_inj.log.push_back({ line, kind });
        };

        switch (g_inj.phase) {

        case InjectPhase::Cleaning:

            if (elapsed >= 350) {

                const bool useHv = (g_inj.forGame == "offscript")
                                || (g_runtimeMode == "hv");
                if (useHv) {
                    enter(InjectPhase::HvProbe, "Verifying hypervisor",
                          "[hv]     Verifying hypervisor is active…", 0, 12);
                } else {
                    enter(InjectPhase::DriverProbe, "Verifying kernel driver",
                          "[drv]    Verifying kernel driver is armed…", 0, 12);
                }
            }
            break;

        case InjectPhase::HvProbe: {

            if (elapsed < 350) break;
            const bool active = ProbeHypervisor();
            if (active) {
                enter(InjectPhase::WaitForGame, "Waiting for game",
                       "[hv]     HyperV is active.", 1, 24);

                g_inj.log.push_back({ "[wait]   Waiting for game…", 0 });
                g_inj.gameWaitStart = now;
                g_inj.lastHeartbeat = now;
            } else {

                g_inj.log.push_back({
                    "[hv]     HyperV not detected.", 2 });
                g_inj.log.push_back({
                    "[hv]     Installing dependencies…", 0 });
                g_inj.phase     = InjectPhase::HvInstalling;
                g_inj.phaseTick = now;
                g_inj.stage     = "Installing dependencies";
                g_inj.progress  = 28;
                std::thread(DoHvInstallWorker).detach();
            }
        } break;

        case InjectPhase::HvInstalling: {
            const int st = g_installStatus.load();
            if (st == 1) {

                g_inj.phase     = InjectPhase::RestartPrompt;
                g_inj.phaseTick = now;
                g_inj.stage     = "Restart PC";
                g_inj.progress  = 100;
                break;
            }
            if (st == 2) {
                g_inj.log.push_back({
                    "[hv]     Install failed. Aborting.", 2 });
                g_inj.status   = InjectStatus::Failed;
                g_inj.stage    = "Install failed";
                break;
            }

            const float u   = (elapsed / 15000.0f);
            const float pct = u > 1.0f ? 1.0f : u;
            g_inj.progress  = 28 + (int)((78 - 28) * pct);
        } break;

        case InjectPhase::DriverProbe: {

            if (elapsed < 350) break;
            const bool armed = g_driverProbeHook ? g_driverProbeHook() : false;
            if (armed) {
                enter(InjectPhase::WaitForGame, "Waiting for game",
                       "[drv]    Kernel driver is armed.", 1, 24);
                g_inj.log.push_back({ "[wait]   Waiting for game…", 0 });
                g_inj.gameWaitStart = now;
                g_inj.lastHeartbeat = now;
            } else {
                g_inj.log.push_back({ "[drv]    Kernel driver not armed.", 2 });
                g_inj.log.push_back({ "[drv]    Installing kernel driver…", 0 });
                g_inj.phase     = InjectPhase::DriverInstalling;
                g_inj.phaseTick = now;
                g_inj.stage     = "Installing kernel driver";
                g_inj.progress  = 28;
                std::thread(DoDriverInstallWorker).detach();
            }
        } break;

        case InjectPhase::DriverInstalling: {
            const int st = g_installStatus.load();
            if (st == 1) {

                if (g_spooferOn.load(std::memory_order_relaxed)) {
                    enter(InjectPhase::SpooferRunning, "Arming spoofer",
                           "[drv]    Kernel driver armed.", 1, 80);
                    std::thread(DoSpooferWorker).detach();
                } else {
                    enter(InjectPhase::WaitForGame, "Waiting for game",
                           "[drv]    Kernel driver armed.", 1, 100);
                    g_inj.log.push_back({ "[wait]   Waiting for game…", 0 });
                    g_inj.gameWaitStart = now;
                    g_inj.lastHeartbeat = now;
                }
                break;
            }
            if (st == 2) {
                g_inj.log.push_back({
                    "[drv]    Install failed. Aborting.", 2 });
                g_inj.status   = InjectStatus::Failed;
                g_inj.stage    = "Install failed";
                break;
            }

            const float u   = (elapsed / 6000.0f);
            const float pct = u > 1.0f ? 1.0f : u;
            g_inj.progress  = 28 + (int)((78 - 28) * pct);
        } break;

        case InjectPhase::SpooferRunning: {

            const int st = g_spooferRunStatus.load();
            if (st == 1 || st == 2) {
                if (st == 2) {

                    g_spooferOn.store(false, std::memory_order_release);
                    g_inj.log.push_back({
                        "[spoof]  Spoofer skipped, continuing inject.", 2 });
                }
                enter(InjectPhase::WaitForGame, "Waiting for game",
                       "[wait]   Waiting for game…", 0, 100);
                g_inj.gameWaitStart = now;
                g_inj.lastHeartbeat = now;
                break;
            }

            const float u   = (elapsed / 5000.0f);
            const float pct = u > 1.0f ? 1.0f : u;
            g_inj.progress  = 80 + (int)((95 - 80) * pct);
        } break;

        case InjectPhase::RestartPrompt:

            break;

        case InjectPhase::WaitForGame: {

            if (elapsed < 600) break;
            g_inj.phaseTick = now;
            const auto& names = GameProcessNames(g_inj.forGame);
            if (IsGameRunning(names)) {
                enter(InjectPhase::Fetching, "Fetching package",
                       "[wait]   Game detected.", 1, 56);

                std::thread(DoInjectWorker, g_inj.forGame).detach();
                break;
            }

            if (now - g_inj.lastHeartbeat >= 10000) {
                g_inj.lastHeartbeat = now;
                const DWORD waited = (now - g_inj.gameWaitStart) / 1000;
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                               "[wait]   Still waiting for game… (%lus elapsed)",
                               (unsigned long)waited);
                g_inj.log.push_back({ buf, 0 });
            }

            g_inj.progress = 38;
        } break;

        case InjectPhase::Fetching: {

            const int st = g_injectStatus.load();
            if (st == 1) {
                g_inj.phase    = InjectPhase::Complete;
                g_inj.stage    = "Complete";
                g_inj.progress = 100;
                g_inj.status   = InjectStatus::Done;
                break;
            }
            if (st == 2) {
                g_inj.status   = InjectStatus::Failed;
                g_inj.stage    = "Injection failed";
                break;
            }

            const float u   = (elapsed / 4000.0f);
            const float pct = u > 1.0f ? 1.0f : u;
            g_inj.progress  = 56 + (int)((95 - 56) * pct);
        } break;

        case InjectPhase::Starting:

            break;

        case InjectPhase::Complete:
        case InjectPhase::None:
            break;
        }
    }

    static void DrawCardDetailModal() {

        static int s_lastEnt = -1;
        if (g.detailModalEnt >= 0 &&
            g.detailModalEnt < (int)g.ents.size()) {
            s_lastEnt = g.detailModalEnt;
        }
        if (s_lastEnt < 0 || s_lastEnt >= (int)g.ents.size()) return;
        if (g.detailExpandAnim < 0.001f) return;

        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const Entitlement& ent = g.ents[s_lastEnt];

        {
            const float modalTarget = (g.detailModalEnt >= 0) ? 1.0f : 0.0f;
            g.detailModalAnim = EaseTo(g.detailModalAnim, modalTarget, 5.0f);
        }
        const float expandU = g.detailExpandAnim;
        const float fadeU   = g.detailModalAnim;

        const float panelW = kDetailPanelW * expandU;
        if (panelW < 2.0f) return;
        const float padOuter = 18.0f;
        const ImVec2 a = { disp.x - panelW + padOuter,
                            kTopBarH + padOuter };
        const ImVec2 b = { disp.x - padOuter,
                            disp.y - padOuter };
        const ImVec2 sz = { b.x - a.x, b.y - a.y };
        if (sz.x < 4.0f || sz.y < 4.0f) return;
        constexpr float r = 16.0f;

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##detailpanel", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        const float divX = disp.x - panelW;
        dl->AddRectFilled(
            { divX, kTopBarH },
            { disp.x, disp.y },
            IM_COL32(0, 0, 0, (BYTE)(255.0f * 0.62f * expandU)));

        dl->AddLine({divX, kTopBarH + 8.0f}, {divX, disp.y - 8.0f},
                    IM_COL32(255, 255, 255, (BYTE)(40 * expandU)), 1.0f);

        for (int i = 4; i >= 0; --i) {
            const float infl = (i + 1) * 4.0f;
            const float yOff = 3.0f + (float)i * 1.5f;
            const float fall = 1.0f - (float)i / 5.0f;
            const BYTE  alpha = (BYTE)(80.0f * fall * fall * expandU);
            if (alpha == 0) continue;
            dl->AddRectFilled(
                { a.x - infl, a.y - infl + yOff },
                { b.x + infl, b.y + infl + yOff },
                IM_COL32(0, 0, 0, alpha),
                r + infl);
        }

        dl->AddRectFilled(a, b, IM_COL32(22, 17, 36, 255), r);

        dl->AddRectFilledMultiColor(
            { a.x, a.y },
            { b.x, a.y + 60.0f },
            IM_COL32(255, 255, 255, (BYTE)(14 * fadeU)),
            IM_COL32(255, 255, 255, (BYTE)(14 * fadeU)),
            IM_COL32(255, 255, 255, 0),
            IM_COL32(255, 255, 255, 0));

        const ImVec2 mp = ImGui::GetIO().MousePos;

        const float xBtnS = 28.0f;
        const ImVec2 xA = { b.x - xBtnS - 12.0f, a.y + 12.0f };
        const ImVec2 xB = { xA.x + xBtnS,        xA.y + xBtnS };
        const bool xHov = expandU >= 0.95f &&
                          mp.x >= xA.x && mp.x <= xB.x &&
                          mp.y >= xA.y && mp.y <= xB.y;
        const BYTE xBg = (BYTE)((xHov ? 80 : 30) * fadeU);
        dl->AddRectFilled(xA, xB, IM_COL32(255, 255, 255, xBg), xBtnS * 0.5f);
        const float xCx = (xA.x + xB.x) * 0.5f;
        const float xCy = (xA.y + xB.y) * 0.5f;
        const float xS  = 6.5f;
        const ImU32 xCol = IM_COL32(255, 255, 255, (BYTE)(220 * fadeU));
        dl->AddLine({xCx - xS, xCy - xS}, {xCx + xS, xCy + xS}, xCol, 2.0f);
        dl->AddLine({xCx - xS, xCy + xS}, {xCx + xS, xCy - xS}, xCol, 2.0f);
        if (xHov && ImGui::IsMouseClicked(0)) {
            g.detailModalEnt = -1;
        }

        const float detailRaw = (fadeU - 0.30f) / 0.65f;
        float detailFade = detailRaw < 0.0f ? 0.0f
                            : (detailRaw > 1.0f ? 1.0f : detailRaw);
        detailFade = detailFade * detailFade * (3.0f - 2.0f * detailFade);
        const float detailSlide = (1.0f - detailFade) * 14.0f;

        const float padX = 24.0f;
        const float headerY = a.y + 28.0f + detailSlide;

        {
            PushBigFont();
            const std::string title = GameDisplay(ent);
            dl->AddText({a.x + padX, headerY},
                        IM_COL32(255, 255, 255, (BYTE)(255 * detailFade)),
                        title.c_str());
            PopFont();
        }

        const bool runForThis = (g_inj.forGame == ent.game) &&
                                 (g_inj.status != InjectStatus::Idle);
        InjectStatus dispStatus = runForThis ? g_inj.status
                                                : InjectStatus::Idle;
        std::string  dispStage  = runForThis ? g_inj.stage : "Ready";

        const bool awaitingRestart = runForThis &&
                                       g_inj.phase == InjectPhase::RestartPrompt;
        {
            PushBodyFont();
            const float statusY = headerY + 34.0f;
            const float dotR = 6.0f;
            const ImVec2 dotC = { a.x + padX + dotR, statusY + 9.0f };

            Color dotColor = Color(140, 130, 170);
            if (awaitingRestart) {

                dotColor = Color(34, 197, 94);
            } else if (dispStatus == InjectStatus::Running) {
                dotColor = Style::accentColor;
            } else if (dispStatus == InjectStatus::Done) {
                dotColor = Color(34, 197, 94);
            } else if (dispStatus == InjectStatus::Failed) {
                dotColor = Color(220, 38, 38);
            }

            float halo = 1.0f;
            if (dispStatus == InjectStatus::Running && !awaitingRestart) {
                const float t = (float)ImGui::GetTime();
                halo = 0.70f + 0.30f * (0.5f + 0.5f * sinf(t * 4.5f));
            }
            const BYTE dotA = (BYTE)(255 * detailFade);
            const BYTE haloA = (BYTE)(80 * halo * detailFade);
            dl->AddCircleFilled(dotC, dotR + 4.0f,
                IM_COL32(dotColor.r, dotColor.g, dotColor.b, haloA), 20);
            dl->AddCircleFilled(dotC, dotR,
                IM_COL32(dotColor.r, dotColor.g, dotColor.b, dotA), 20);

            std::string statusText;
            if      (awaitingRestart)                      statusText = "Hypervisor installed — restart required";
            else if (dispStatus == InjectStatus::Idle)    statusText = "Ready — press Inject to begin";
            else if (dispStatus == InjectStatus::Running) statusText = "Running: " + dispStage;
            else if (dispStatus == InjectStatus::Done)    statusText = "Injected — game launching";
            else                                           statusText = "Failed — see log";
            dl->AddText({a.x + padX + dotR * 2 + 12.0f, statusY},
                        IM_COL32(225, 225, 235, (BYTE)(240 * detailFade)),
                        statusText.c_str());
            PopFont();
        }

        {
            PushBodyFont();
            const float subY = headerY + 62.0f;
            const std::string sub = FormatTimeLeft(ent.seconds_left);
            dl->AddText({a.x + padX, subY},
                        IM_COL32(140, 130, 170, (BYTE)(200 * detailFade)),
                        sub.c_str());
            PopFont();
        }

        const float logTop      = headerY + 100.0f;
        const float btnH        = 50.0f;
        const float barH        = 8.0f;

        const float spooferH    = 0.0f;
        const float spooferGap  = 0.0f;
        const float btnTopY     = b.y - btnH - 24.0f + detailSlide;
        const float barTopYConst = btnTopY - 18.0f - barH;
        const float spooferTopY = barTopYConst - spooferGap - spooferH;
        const float logBottom   = spooferTopY - 14.0f;
        const ImVec2 logA = { a.x + padX,           logTop };
        const ImVec2 logB = { b.x - padX,           logBottom };
        if (logB.y - logA.y > 40.0f) {

            dl->AddRectFilled(logA, logB,
                IM_COL32(14, 14, 22, (BYTE)(255 * detailFade)),
                10.0f);
            dl->AddRect(logA, logB,
                IM_COL32(255, 255, 255, (BYTE)(18 * detailFade)),
                10.0f, 0, 1.0f);

            ImGui::SetCursorScreenPos({logA.x + 12.0f, logA.y + 10.0f});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("##injlog",
                ImVec2(logB.x - logA.x - 24.0f, logB.y - logA.y - 20.0f),
                false,
                ImGuiWindowFlags_NoBackground);

            PushBodyFont();
            if (g_inj.forGame != ent.game || g_inj.log.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(0.50f, 0.50f, 0.58f, 0.70f * detailFade));
                ImGui::TextWrapped(
                    "Loader idle.  Click INJECT below to start the "
                    "driver download, mapping, and game-window wait.");
                ImGui::PopStyleColor();
            } else {
                for (const auto& line : g_inj.log) {
                    ImVec4 col;
                    const char* glyph;
                    switch (line.kind) {
                    case  1: col = ImVec4(0.30f, 0.85f, 0.50f, detailFade);
                             glyph = "[+] ";   break;
                    case  2: col = ImVec4(0.95f, 0.65f, 0.25f, detailFade);
                             glyph = "[!] ";   break;
                    case -1: col = ImVec4(0.66f, 0.62f, 0.95f, detailFade);
                             glyph = "[.] ";   break;
                    default: col = ImVec4(0.78f, 0.78f, 0.86f, 0.92f * detailFade);
                             glyph = "    ";   break;
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::TextUnformatted(glyph);
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextWrapped("%s", line.text.c_str());
                    ImGui::PopStyleColor();
                }

                if (g_inj.status == InjectStatus::Running)
                    ImGui::SetScrollHereY(1.0f);
            }
            PopFont();
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        if (false) {
            const float pillW    = 36.0f;
            const float pillH    = spooferH;
            const float labelGap = 8.0f;
            const char* label    = "Spoofer";

            const ImVec2 pillA = { b.x - padX - pillW, spooferTopY };
            const ImVec2 pillB = { b.x - padX,         spooferTopY + pillH };

            const bool stateOn = g_spooferOn.load(std::memory_order_relaxed);

            const float target = stateOn ? 1.0f : 0.0f;
            g_spooferAnim += (target - g_spooferAnim) * 0.30f;
            const float rawT = (g_spooferAnim < 0.0f) ? 0.0f
                             : (g_spooferAnim > 1.0f) ? 1.0f : g_spooferAnim;
            const float t = 1.0f - (1.0f - rawT) * (1.0f - rawT) * (1.0f - rawT);
            g_spooferGlow *= 0.88f;

            const float radius   = pillH * 0.5f;
            const float thumbR   = radius - 3.0f;
            const float thumbCX  = pillA.x + radius + (pillW - 2.0f * radius) * t;
            const ImVec2 thumbCenter = { thumbCX, pillA.y + pillH * 0.5f };

            const Color& acc = Style::accentColor;

            PushBodyFont();
            const ImVec2 lbSize = ImGui::CalcTextSize(label);
            const ImVec2 lbPos  = { pillA.x - labelGap - lbSize.x,
                                    pillA.y + (pillH - lbSize.y) * 0.5f - 1.0f };
            const ImU32 lbCol = stateOn
                ? IM_COL32(235, 236, 244, (BYTE)(245 * detailFade))
                : IM_COL32(165, 168, 180, (BYTE)(215 * detailFade));
            dl->AddText(lbPos, lbCol, label);
            PopFont();

            const ImVec2 rowA = { lbPos.x - 4.0f, pillA.y - 2.0f };
            const ImVec2 rowB = { pillB.x,        pillB.y + 2.0f };
            const bool rowHov = expandU >= 0.95f &&
                                mp.x >= rowA.x && mp.x <= rowB.x &&
                                mp.y >= rowA.y && mp.y <= rowB.y;

            const auto lerpB = [&](int off, int on) -> BYTE {
                return (BYTE)((1.0f - t) * off + t * on);
            };
            const BYTE trR = lerpB(48, acc.r);
            const BYTE trG = lerpB(50, acc.g);
            const BYTE trB = lerpB(58, acc.b);
            const BYTE trA = (BYTE)((rowHov ? 230 : 200) * detailFade);

            dl->AddRectFilled(pillA, pillB,
                IM_COL32(trR, trG, trB, trA), radius);

            if (!stateOn) {
                dl->AddRectFilledMultiColor(
                    { pillA.x + 1.0f, pillA.y + 1.0f },
                    { pillB.x - 1.0f, pillA.y + pillH * 0.45f },
                    IM_COL32(0, 0, 0, (BYTE)(55 * detailFade)),
                    IM_COL32(0, 0, 0, (BYTE)(55 * detailFade)),
                    IM_COL32(0, 0, 0, 0),
                    IM_COL32(0, 0, 0, 0));
            }

            dl->AddRect(pillA, pillB,
                IM_COL32(255, 255, 255, (BYTE)((rowHov ? 55 : 30) * detailFade)),
                radius, 0, 1.0f);

            dl->AddCircleFilled({ thumbCenter.x, thumbCenter.y + 1.0f },
                thumbR + 0.5f,
                IM_COL32(0, 0, 0, (BYTE)(85 * detailFade)));
            dl->AddCircleFilled(thumbCenter, thumbR,
                IM_COL32(245, 246, 250, (BYTE)(255 * detailFade)));
            dl->AddCircle(thumbCenter, thumbR,
                IM_COL32(0, 0, 0, (BYTE)(70 * detailFade)),
                24, 1.0f);

            if (g_spooferGlow > 0.02f) {
                dl->AddCircleFilled(thumbCenter, thumbR + 4.0f * g_spooferGlow,
                    IM_COL32(acc.r, acc.g, acc.b,
                             (BYTE)(80 * g_spooferGlow * detailFade)));
            }

            if (g_spooferBusy.load(std::memory_order_relaxed)) {
                const float now = (float)ImGui::GetTime();
                const float a0 = now * 5.0f;
                const float a1 = a0 + 2.2f;
                dl->PathClear();
                dl->PathArcTo(thumbCenter, thumbR + 2.0f, a0, a1, 18);
                dl->PathStroke(IM_COL32(acc.r, acc.g, acc.b,
                                        (BYTE)(230 * detailFade)),
                               0, 1.6f);
            }

            if (rowHov && ImGui::IsMouseClicked(0) &&
                !g_spooferBusy.load(std::memory_order_relaxed))
            {
                const bool newState = !stateOn;
                g_spooferOn.store(newState, std::memory_order_release);
                g_spooferGlow = 1.0f;
            }
        }

        {
            const float barTopY = barTopYConst;
            const ImVec2 barA = { a.x + padX, barTopY };
            const ImVec2 barB = { b.x - padX, barTopY + barH };

            dl->AddRectFilled(barA, barB,
                IM_COL32(255, 255, 255, (BYTE)(18 * detailFade)),
                barH * 0.5f);

            const float pct = (float)g_inj.progress / 100.0f;
            if (pct > 0.0f) {
                const Color& acc = (awaitingRestart || dispStatus == InjectStatus::Done)
                    ? Color(34, 197, 94)
                    : (dispStatus == InjectStatus::Failed
                       ? Color(220, 38, 38)
                       : Style::accentColor);
                dl->AddRectFilled(barA,
                    { barA.x + (barB.x - barA.x) * pct, barB.y },
                    IM_COL32(acc.r, acc.g, acc.b, (BYTE)(255 * detailFade)),
                    barH * 0.5f);
            }
        }

        const ImVec2 btnA = { a.x + padX, btnTopY };
        const ImVec2 btnB = { b.x - padX, btnTopY + btnH };
        const bool btnHov = expandU >= 0.95f &&
                            mp.x >= btnA.x && mp.x <= btnB.x &&
                            mp.y >= btnA.y && mp.y <= btnB.y;

        const char* btnLbl   = "INJECT";
        bool        btnClick = false;
        Color       btnCol   = Style::accentColor;
        if (awaitingRestart) {

            btnLbl = "RESTART NOW";
            btnCol = Color(34, 197, 94);
        } else if (dispStatus == InjectStatus::Running) {
            btnLbl = "RUNNING…";
            btnCol = Color(64, 64, 72);
        } else if (dispStatus == InjectStatus::Done) {
            btnLbl = "LAUNCH GAME";
            btnCol = Color(34, 197, 94);
        }

        const bool btnEnabled = (dispStatus != InjectStatus::Running) ||
                                 awaitingRestart;

        {
            const float lift = (btnHov && btnEnabled) ? 1.10f : 1.0f;
            const BYTE br = (BYTE)(((std::min)(255.0f, btnCol.r * lift)) * detailFade);
            const BYTE bg = (BYTE)(((std::min)(255.0f, btnCol.g * lift)) * detailFade);
            const BYTE bb = (BYTE)(((std::min)(255.0f, btnCol.b * lift)) * detailFade);
            const BYTE ba = (BYTE)(255 * detailFade);
            dl->AddRectFilled(btnA, btnB,
                IM_COL32(br, bg, bb, ba), 10.0f);
            dl->AddRectFilledMultiColor(
                { btnA.x, btnA.y },
                { btnB.x, btnA.y + btnH * 0.55f },
                IM_COL32(255, 255, 255, (BYTE)(40 * detailFade)),
                IM_COL32(255, 255, 255, (BYTE)(40 * detailFade)),
                IM_COL32(255, 255, 255, 0),
                IM_COL32(255, 255, 255, 0));
            PushBigFont();
            const ImVec2 ls = ImGui::CalcTextSize(btnLbl);
            dl->AddText({ (btnA.x + btnB.x) * 0.5f - ls.x * 0.5f,
                          (btnA.y + btnB.y) * 0.5f - ls.y * 0.5f },
                        IM_COL32(255, 255, 255, (BYTE)(255 * detailFade)),
                        btnLbl);
            PopFont();
        }
        if (btnHov && btnEnabled && ImGui::IsMouseClicked(0)) {
            btnClick = true;
        }
        if (btnClick) {
            if (awaitingRestart) {

                Log("Restart requested.");
                if (g_hvRebootHook) {
                    g_hvRebootHook();
                } else {
                    g_inj = {};
                }
            } else if (dispStatus == InjectStatus::Idle) {
                StartInjection(ent.game);
                Log("Inject requested for " + GameDisplay(ent));
            } else if (dispStatus == InjectStatus::Done) {
                Log("Launch requested for " + GameDisplay(ent));

            }
        }

        if (expandU >= 0.05f && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g.detailModalEnt = -1;
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    static void DrawUpdateOverlay() {

        const float target = (g_upd.phase == UpdatePhase::None) ? 0.0f : 1.0f;
        g_upd.overlayAnim  = EaseTo(g_upd.overlayAnim, target, 7.0f);
        if (g_upd.overlayAnim < 0.005f && target == 0.0f) return;

        const DWORD now = ::GetTickCount();
        if (g_upd.phase == UpdatePhase::Announce &&
            now - g_upd.phaseStart > 800) {
            g_upd.phase = UpdatePhase::Downloading;
            g_upd.phaseStart = now;
        }

        if (g_upd.phase == UpdatePhase::LatestVersion &&
            now - g_upd.phaseStart > 1200) {
            g_upd.phase = UpdatePhase::None;
        }

        const float pctTarget = (g_upd.progress < 0) ? 0.0f
                              : (g_upd.progress / 100.0f);
        g_upd.easedFill = EaseTo(g_upd.easedFill, pctTarget, 9.0f);

        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const float  u    = g_upd.overlayAnim;

        const float  ut   = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##updateoverlay", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled({0, 0}, disp,
                           IM_COL32(0, 0, 0, (BYTE)(180.0f * ut)));

        const float cardW = (std::min)(380.0f, disp.x - 40.0f);
        const float cardH = (g_upd.phase == UpdatePhase::Failed) ? 200.0f : 230.0f;
        const float slide = 24.0f * (1.0f - ut);
        const float cardX = (disp.x - cardW) * 0.5f;
        const float cardY = (disp.y - cardH) * 0.5f + slide;
        const ImVec2 a = { cardX,         cardY };
        const ImVec2 b = { cardX + cardW, cardY + cardH };
        constexpr float r = 14.0f;

        for (int i = 4; i >= 0; --i) {
            const float infl = (i + 1) * 4.5f;
            const float yOff = 3.0f + (float)i * 2.0f;
            const float fall = 1.0f - (float)i / 5.0f;
            const BYTE  alpha = (BYTE)(90.0f * fall * fall * ut);
            if (alpha == 0) continue;
            dl->AddRectFilled(
                { a.x - infl, a.y - infl + yOff },
                { b.x + infl, b.y + infl + yOff },
                IM_COL32(0, 0, 0, alpha), r + infl);
        }

        dl->AddRectFilled(a, b,
            IM_COL32(22, 22, 32, (BYTE)(255 * ut)), r);
        dl->AddRectFilledMultiColor(
            { a.x, a.y }, { b.x, a.y + 60.0f },
            IM_COL32(255, 255, 255, (BYTE)(18 * ut)),
            IM_COL32(255, 255, 255, (BYTE)(18 * ut)),
            IM_COL32(255, 255, 255, 0),
            IM_COL32(255, 255, 255, 0));
        dl->AddRect(a, b,
            IM_COL32(255, 255, 255, (BYTE)(28 * ut)), r, 0, 1.0f);

        const float t      = (float)ImGui::GetTime();
        const float iconCx = (a.x + b.x) * 0.5f;
        const float iconCy = a.y + 56.0f;
        const float iconR  = 26.0f;

        Color acc = Style::accentColor;
        if      (g_upd.phase == UpdatePhase::Failed)        acc = Color(220,  38,  38);
        else if (g_upd.phase == UpdatePhase::LatestVersion) acc = Color( 34, 197,  94);
        const BYTE aFull   = (BYTE)(255 * ut);

        const float pulse  = 0.5f + 0.5f * sinf(t * 3.0f);
        const float haloR  = iconR + 7.0f + 3.0f * pulse;
        dl->AddCircle({iconCx, iconCy}, haloR,
            IM_COL32(acc.r, acc.g, acc.b, (BYTE)(48 * ut)),
            32, 2.0f);

        dl->AddCircleFilled({iconCx, iconCy}, iconR,
            IM_COL32(28, 28, 40, aFull), 32);

        const bool wantRing = g_upd.phase == UpdatePhase::Checking    ||
                              g_upd.phase == UpdatePhase::Announce    ||
                              g_upd.phase == UpdatePhase::Downloading;
        if (wantRing) {
            dl->AddCircle({iconCx, iconCy}, iconR,
                IM_COL32(255, 255, 255, (BYTE)(28 * ut)), 48, 2.5f);
            if (g_upd.phase == UpdatePhase::Downloading && g_upd.progress >= 0) {

                const float a0 = -3.14159265f * 0.5f;
                const float a1 = a0 + 2.0f * 3.14159265f * g_upd.easedFill;
                if (a1 > a0 + 0.01f) {
                    dl->PathArcTo({iconCx, iconCy}, iconR, a0, a1, 48);
                    dl->PathStroke(
                        IM_COL32(acc.r, acc.g, acc.b, aFull), 0, 2.5f);
                }
            } else {

                const float a0 = t * 3.0f;
                const float a1 = a0 + 1.2f;
                dl->PathArcTo({iconCx, iconCy}, iconR, a0, a1, 32);
                dl->PathStroke(
                    IM_COL32(acc.r, acc.g, acc.b, aFull), 0, 2.5f);
            }
        }

        if (g_upd.phase == UpdatePhase::Failed) {
            const float s = 8.0f;
            const ImU32 c = IM_COL32(255, 255, 255, aFull);
            dl->AddLine({iconCx - s, iconCy - s}, {iconCx + s, iconCy + s}, c, 2.5f);
            dl->AddLine({iconCx - s, iconCy + s}, {iconCx + s, iconCy - s}, c, 2.5f);
        } else if (g_upd.phase == UpdatePhase::LatestVersion) {

            const ImU32 c = IM_COL32(255, 255, 255, aFull);
            dl->AddLine({iconCx - 8.0f, iconCy + 0.0f},
                         {iconCx - 2.0f, iconCy + 6.0f}, c, 3.0f);
            dl->AddLine({iconCx - 2.0f, iconCy + 6.0f},
                         {iconCx + 9.0f, iconCy - 6.0f}, c, 3.0f);
        } else if (g_upd.phase == UpdatePhase::Checking) {

        } else {

            const float bob = 1.8f * pulse;
            const float gx  = iconCx;
            const float gy  = iconCy + bob;
            const ImU32 c   = IM_COL32(255, 255, 255, aFull);
            dl->AddLine({gx, gy - 9.0f}, {gx, gy + 4.0f}, c, 2.5f);
            dl->AddLine({gx - 5.0f, gy - 1.0f}, {gx, gy + 5.0f}, c, 2.5f);
            dl->AddLine({gx + 5.0f, gy - 1.0f}, {gx, gy + 5.0f}, c, 2.5f);
            dl->AddLine({gx - 8.0f, gy + 10.0f}, {gx + 8.0f, gy + 10.0f}, c, 2.0f);
        }

        const char* headline = nullptr;
        const char* subline  = nullptr;
        switch (g_upd.phase) {
            case UpdatePhase::Checking:
                headline = "Checking for updates";
                subline  = "Hold tight, this only takes a moment.";
                break;
            case UpdatePhase::LatestVersion:
                headline = "You're up to date";
                subline  = "Latest version installed.";
                break;
            case UpdatePhase::Announce:
                headline = "Update available";
                subline  = "Preparing to download...";
                break;
            case UpdatePhase::Downloading:
                headline = "Downloading update";
                subline  = "Don't close the loader.";
                break;
            case UpdatePhase::Failed:
                headline = "Update failed";
                subline  = g_upd.err.empty() ? "Continuing with current build."
                                              : g_upd.err.c_str();
                break;
            case UpdatePhase::None: break;
        }

        if (headline) {
            PushBigFont();
            const ImVec2 hSz = ImGui::CalcTextSize(headline);
            dl->AddText({iconCx - hSz.x * 0.5f, iconCy + 50.0f},
                        IM_COL32(255, 255, 255, aFull), headline);
            PopFont();
        }
        if (subline) {
            PushBodyFont();
            const ImVec2 sSz = ImGui::CalcTextSize(subline);
            dl->AddText({iconCx - sSz.x * 0.5f, iconCy + 78.0f},
                        IM_COL32(170, 160, 200, (BYTE)(240 * ut)),
                        subline);
            PopFont();
        }

        if (g_upd.phase == UpdatePhase::Downloading) {
            const float barH    = 8.0f;
            const float barPad  = 30.0f;
            const float barTopY = b.y - 38.0f;
            const ImVec2 barA = { a.x + barPad,         barTopY };
            const ImVec2 barB = { b.x - barPad,         barTopY + barH };

            dl->AddRectFilled(barA, barB,
                IM_COL32(40, 40, 52, aFull), barH * 0.5f);

            const float fillW = (barB.x - barA.x) * g_upd.easedFill;
            if (fillW > 1.0f) {
                dl->AddRectFilled(
                    barA, { barA.x + fillW, barB.y },
                    IM_COL32(acc.r, acc.g, acc.b, aFull),
                    barH * 0.5f);

                dl->AddRectFilledMultiColor(
                    barA, { barA.x + fillW, barA.y + barH * 0.5f },
                    IM_COL32(255, 255, 255, (BYTE)(60 * ut)),
                    IM_COL32(255, 255, 255, (BYTE)(60 * ut)),
                    IM_COL32(255, 255, 255, 0),
                    IM_COL32(255, 255, 255, 0));
            }

            char pctBuf[16];
            const int displayPct = g_upd.progress < 0 ? 0 : g_upd.progress;
            std::snprintf(pctBuf, sizeof(pctBuf), "%d%%", displayPct);
            PushBodyFont();
            const ImVec2 pctSz = ImGui::CalcTextSize(pctBuf);
            dl->AddText({barB.x - pctSz.x, barTopY - pctSz.y - 6.0f},
                        IM_COL32(225, 225, 235, aFull), pctBuf);
            PopFont();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

}

namespace gui {

    void ShowUpdateCheck();
    void HideUpdateOverlay();

    void Init() {
        ApplyDarkTheme();

        g.sessionStart = NowSec();

        #ifndef LOADER_BUILD_ID
        ShowUpdateCheck();
        g.bootCheckDemoStartSec = NowSec();
        #endif

        Log("Loader started.");
    }

    void Render() {

        DriveWindowSize();
        UpdateContactPhase();
        UpdateWelcomePhase();

        #ifndef LOADER_BUILD_ID
        if (g.bootCheckDemoStartSec > 0.0 &&
            g_upd.phase == UpdatePhase::Checking &&
            NowSec() - g.bootCheckDemoStartSec > 1.5) {
            HideUpdateOverlay();
            g.bootCheckDemoStartSec = -1.0;
        }
        #endif

        switch (g.phase) {
        case Phase::Login:
            DrawLoginScreen();
            break;
        case Phase::ContactingServer:
            DrawContactingScreen();
            break;
        case Phase::WelcomeBack:
            DrawWelcomeScreen();
            break;
        case Phase::Home: {

            const ImVec2 disp = ImGui::GetIO().DisplaySize;
            DrawWallpaperBackground(ImGui::GetBackgroundDrawList(),
                                     {0, 0}, disp, 0.0f);

            DrawSidebar();
            DrawTopBar();
            DrawHomeScreen();

            DrawEnvPanel();

            DrawSettingsPanel();

            DriveDetailModal();
            UpdateInjection();
            DrawCardDetailModal();
            break;
        }
        }

        DrawUpdateOverlay();
    }

    void Shutdown() {
        for (auto& [id, tex] : g_TexCache) {
            if (tex.srv) tex.srv->Release();
        }
        g_TexCache.clear();
        ReleaseWallpaper();
        ReleaseAvatar();
    }

    void SetHvInstallHook(bool (*fn)(std::string&)) { g_hvInstallHook = fn; }
    void SetHvRebootHook (void (*fn)())             { g_hvRebootHook  = fn; }

    void SetMapperInstallHook(bool (*fn)(std::string&)) { g_mapperInstallHook = fn; }
    void SetDriverProbeHook  (bool (*fn)())             { g_driverProbeHook   = fn; }

    void SetSpooferHook      (bool (*fn)(bool, std::string&)) { g_spooferHook = fn; }

    void SetRuntimeMode(const std::string& mode) {
        g_runtimeMode = (mode == "mapper") ? "mapper" : "hv";
    }

    void SetInjectHook(bool (*fn)(const char*, std::string&)) {
        g_injectHook = fn;
    }

    void ShowUpdateCheck() {

        g_upd.phase      = UpdatePhase::Checking;
        g_upd.phaseStart = ::GetTickCount();
        g_upd.progress   = -1;
        g_upd.easedFill  = 0.0f;
        g_upd.err.clear();
    }

    void BeginUpdateDownload() {
        g_upd.phase       = UpdatePhase::Announce;
        g_upd.phaseStart  = ::GetTickCount();
        g_upd.progress    = 0;
        g_upd.easedFill   = 0.0f;
        g_upd.err.clear();
    }

    void SetProgress(int pct) {
        if (g_upd.phase == UpdatePhase::Announce && pct >= 0) {
            g_upd.phase = UpdatePhase::Downloading;
            g_upd.phaseStart = ::GetTickCount();
        }
        if (g_upd.phase == UpdatePhase::Downloading ||
            g_upd.phase == UpdatePhase::Announce) {
            g_upd.progress = pct;
        }
    }

    void SetUpdateError(const char* msg) {
        g_upd.phase      = UpdatePhase::Failed;
        g_upd.phaseStart = ::GetTickCount();
        g_upd.err        = msg ? msg : "Update failed.";
    }

    void HideUpdateOverlay() {
        if (g_upd.phase == UpdatePhase::Checking) {
            g_upd.phase      = UpdatePhase::LatestVersion;
            g_upd.phaseStart = ::GetTickCount();
            g_upd.progress   = -1;
            g_upd.easedFill  = 0.0f;
        } else {
            g_upd.phase = UpdatePhase::None;
        }
    }

    void GetLoginKey(std::string& out) {
        out = g.licenseKey;
    }

    void SignInSucceeded(int64_t expirySecs, const std::string& gamesCsv) {
        auto& pa = ::PendingAuth();
        std::lock_guard<std::mutex> lk(pa.mu);
        pa.result  = 1;
        pa.expiry  = expirySecs;
        pa.games   = gamesCsv;
        pa.message.clear();
    }

    void SignInFailed(const std::string& message) {
        auto& pa = ::PendingAuth();
        std::lock_guard<std::mutex> lk(pa.mu);
        pa.result  = 2;
        pa.message = message;
    }

    void PushInjectLog(const char* msg, int kind) {
        if (!msg) return;

        std::lock_guard<std::mutex> lk(g_injPendingLogMu);
        g_injPendingLog.push_back({ msg, kind });
    }

    void ShowPairPanel(const std::string& pair_code,
                       const std::string& verify_url,
                       int expires_in) {
        std::lock_guard<std::mutex> lk(g.pairMu);
        g.pair.active      = true;
        g.pair.pair_code   = pair_code;
        g.pair.verify_url  = verify_url;
        g.pair.expires_in  = expires_in > 0 ? expires_in : 600;
        g.pair.startedTick = ::GetTickCount();
        g.pair.status_text = "Waiting for browser approval...";
        g.pair.finished    = false;
        g.pair.succeeded   = false;
    }

    void SetPairPanelStatus(const std::string& text) {
        std::lock_guard<std::mutex> lk(g.pairMu);
        g.pair.status_text = text;
    }

    void SetPairPanelDone(bool succeeded) {
        std::lock_guard<std::mutex> lk(g.pairMu);
        g.pair.finished  = true;
        g.pair.succeeded = succeeded;
        g.pair.status_text = succeeded
            ? "Device paired."
            : "Pairing timed out. Try again.";
    }

    void ClearPairPanel() {
        std::lock_guard<std::mutex> lk(g.pairMu);
        g.pair.active    = false;
        g.pair.finished  = false;
        g.pair.succeeded = false;
        g.pair.pair_code.clear();
        g.pair.verify_url.clear();
        g.pair.status_text.clear();
    }

    bool IsPairPanelDone(bool& succeeded_out) {
        std::lock_guard<std::mutex> lk(g.pairMu);
        if (!g.pair.finished) return false;
        succeeded_out = g.pair.succeeded;
        return true;
    }

}
