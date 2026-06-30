#include "gui_runtime.h"

namespace gui_runtime {

namespace {

std::mutex              g_ReadyMu;
std::condition_variable g_ReadyCV;
bool                    g_ReadySignalled = false;
bool                    g_ReadyResult    = false;

std::atomic<bool>       g_WindowClosed{ false };

std::mutex              g_ReqMu;
std::condition_variable g_ReplyCV;
RequestKind             g_Pending     = RequestKind::None;
LoginReply              g_LoginReply{};
bool                    g_ReplyReady  = false;

}

void SignalWindowReady(bool ok) {
    {
        std::lock_guard<std::mutex> lk(g_ReadyMu);
        g_ReadySignalled = true;
        g_ReadyResult    = ok;
    }
    g_ReadyCV.notify_all();
}

bool WaitForWindowReady() {
    std::unique_lock<std::mutex> lk(g_ReadyMu);
    g_ReadyCV.wait(lk, [] { return g_ReadySignalled; });
    return g_ReadyResult;
}

void SignalWindowClosed() {
    g_WindowClosed.store(true);

    {
        std::lock_guard<std::mutex> lk(g_ReqMu);
        if (g_Pending != RequestKind::None) {
            g_ReplyReady = true;
        }
    }
    g_ReplyCV.notify_all();
}

bool IsWindowClosed() {
    return g_WindowClosed.load();
}

LoginReply BlockingRequestLogin() {
    std::unique_lock<std::mutex> lk(g_ReqMu);

    if (!g_ReplyReady) {
        g_LoginReply = {};
    }
    g_Pending = RequestKind::Login;
    g_ReplyCV.wait(lk, [] { return g_ReplyReady; });

    LoginReply out = g_LoginReply;
    g_Pending      = RequestKind::None;
    g_ReplyReady   = false;
    return out;
}

RequestKind CurrentRequest() {
    std::lock_guard<std::mutex> lk(g_ReqMu);
    return g_Pending;
}

void DeliverLoginReply(const LoginReply& r) {
    {
        std::lock_guard<std::mutex> lk(g_ReqMu);
        g_LoginReply = r;
        g_ReplyReady = true;
    }
    g_ReplyCV.notify_all();
}

}
