#pragma once

#include <hv/EventLoop.h>

#include <functional>

namespace neubau::common {

class Reactor {
public:
    class RunScope {
    public:
        RunScope(const RunScope&) = delete;
        RunScope& operator=(const RunScope&) = delete;
        RunScope(RunScope&& other) noexcept;
        RunScope& operator=(RunScope&& other) noexcept;
        ~RunScope();

    private:
        friend class Reactor;

        explicit RunScope(bool active) noexcept;

        bool _active{};
    };

    [[nodiscard]] static hv::EventLoopPtr loop();
    // Enter the shared loop lifecycle when an external runner owns loop().
    [[nodiscard]] static RunScope enterRun();
    // Whether this process-wide loop has already been entered.
    [[nodiscard]] static bool hasRun() noexcept;
    static void setLoop(hv::EventLoopPtr loop);
    static void run();
    // Invokes onStarted on the running lazy-initialized loop.
    static void run(std::function<void(hv::EventLoopPtr)> onStarted);
    static void stop();
};

} // namespace neubau::common
