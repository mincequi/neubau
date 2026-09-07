#pragma once

#include "common/Subnet.hpp"
#include "common/ThingDiscovery.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rpp/subjects/publish_subject.hpp>

namespace neubau::common {

class PortScannerSession;

struct OpenPort {
    std::string address;
    std::uint16_t port{};

    bool operator==(const OpenPort&) const = default;
};

struct PortScannerOptions {
    Subnet subnet;
    std::vector<std::uint16_t> ports;
    std::chrono::milliseconds connectTimeout{250};
    std::size_t maxConcurrency{64};
    std::size_t maxHosts{4096};
};

class PortScanner : public ThingDiscovery<OpenPort> {
public:
    explicit PortScanner(PortScannerOptions options);
    ~PortScanner() override;

    PortScanner(const PortScanner&) = delete;
    PortScanner& operator=(const PortScanner&) = delete;

    void start() override;
    void stop() override;
    [[nodiscard]] const Flow<OpenPort>& candidates()
        const noexcept override;

private:
    friend class PortScannerSession;

    // Internal state shared between PortScanner and PortScannerSession.
    // Defined here (rather than in PortScanner.cpp) because
    // PortScannerSession, declared in a separate translation unit, also
    // needs the complete type.
    struct State {
        State()
            : candidates{subject.get_observable().as_dynamic()} {}

        std::atomic_bool started{false};
        std::mutex mutex;
        std::function<void()> stopAction;
        rpp::subjects::publish_subject<OpenPort> subject;
        Flow<OpenPort> candidates;
    };

    std::shared_ptr<State> _state;
    PortScannerOptions _options;
    std::vector<std::string> _addresses;
};

} // namespace neubau::common
