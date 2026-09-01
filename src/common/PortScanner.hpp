#pragma once

#include "common/Discovery.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neubau::common {

class PortScannerSession;

struct OpenPort {
    std::string address;
    std::uint16_t port{};

    bool operator==(const OpenPort&) const = default;
};

struct PortScannerOptions {
    std::vector<std::string> addresses;
    std::vector<std::uint16_t> ports;
    std::chrono::milliseconds connectTimeout{250};
    std::size_t maxConcurrency{64};
};

class PortScanner : public Discovery<OpenPort> {
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

    struct State;

    std::shared_ptr<State> _state;
    PortScannerOptions _options;
};

} // namespace neubau::common
