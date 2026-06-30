#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

namespace gui_runtime {

void SignalWindowReady(bool ok);

bool WaitForWindowReady();

void SignalWindowClosed();
bool IsWindowClosed();

enum class RequestKind { None, Login };

struct LoginReply {
    enum class Action { SignIn, Forgot, Register, Cancelled };
    Action      action = Action::Cancelled;
    std::string email;
    std::string password;
};

LoginReply BlockingRequestLogin();

RequestKind CurrentRequest();

void DeliverLoginReply(const LoginReply& r);

}
