#pragma once

#include "common/PortScanner.hpp"

#include <memory>
#include <string>
#include <vector>

namespace neubau::common {

class PortScannerSession : public std::enable_shared_from_this<PortScannerSession> {
public:
    PortScannerSession(
        std::shared_ptr<PortScanner::State> state,
        std::vector<std::string> addresses,
        PortScannerOptions options);
    ~PortScannerSession();

    void start();

private:
    struct State;
    std::unique_ptr<State> _state;

    void fill();
    void stop();
    void finish();
};

} // namespace neubau::common
