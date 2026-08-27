#include "modbus/ModbusDiscovery.hpp"
#include "shelly/ShellyDiscovery.hpp"
#include "webapp/WebAppService.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

void printShellyDevices() {
    neubau::shelly::ShellyDiscovery discovery;
    std::size_t count = 0;

    std::cout << "Discovering Shelly devices...\n" << std::flush;
    try {
        discovery.discover().collect(
            [&count](const neubau::shelly::ShellyThing& thing) {
                std::cout << thing;
                ++count;
            });
    } catch (const std::exception& error) {
        std::cerr << "Shelly discovery failed: " << error.what() << '\n';
        return;
    }

    std::cout << "Discovered " << count << " Shelly device"
              << (count == 1 ? "" : "s") << ".\n"
              << std::flush;
}

class PeriodicModbusDiscovery {
public:
    explicit PeriodicModbusDiscovery(std::string cidr)
        : worker_{[this, cidr = std::move(cidr)] {
            run(std::move(cidr));
        }} {}

    ~PeriodicModbusDiscovery() {
        stopping_ = true;
        wakeup_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    PeriodicModbusDiscovery(const PeriodicModbusDiscovery&) = delete;
    PeriodicModbusDiscovery& operator=(const PeriodicModbusDiscovery&) =
        delete;

private:
    void run(std::string cidr) {
        while (!stopping_) {
            scan(cidr);

            std::unique_lock lock{wakeupMutex_};
            wakeup_.wait_for(
                lock,
                std::chrono::minutes{1},
                [this] { return stopping_.load(); });
        }
    }

    void scan(const std::string& cidr) {
        neubau::modbus::ModbusDiscovery discovery{{
            .cidrs = {cidr},
            .unitIds = {1},
            .connectTimeout = std::chrono::milliseconds{150},
            .responseTimeout = std::chrono::milliseconds{300},
            .maxConcurrency = 32,
        }};

        std::ostringstream output;
        std::size_t count = 0;
        try {
            discovery.discover().collect(
                [&output, &count](const neubau::modbus::ModbusThing& thing) {
                    output << thing;
                    ++count;
                });
        } catch (const std::exception& error) {
            std::cerr << "Modbus discovery failed: " << error.what() << '\n';
            return;
        }

        output << "Modbus scan of " << cidr << " found " << count
               << " device" << (count == 1 ? "" : "s") << ".\n";
        std::cout << output.str() << std::flush;
    }

    std::atomic_bool stopping_{false};
    std::mutex wakeupMutex_;
    std::condition_variable wakeup_;
    std::thread worker_;
};

} // namespace

int main(int argc, char** argv) {
    printShellyDevices();

    const auto modbusCidr =
        neubau::modbus::ModbusDiscovery::primaryIpv4Cidr(24);
    if (!modbusCidr) {
        std::cerr << "Modbus discovery disabled: no primary IPv4 route.\n";
        return neubau::webapp::run_server(argc, argv);
    }

    std::cout << "Scanning " << *modbusCidr
              << " for Modbus TCP devices every minute.\n"
              << std::flush;
    PeriodicModbusDiscovery modbusDiscovery{*modbusCidr};
    return neubau::webapp::run_server(argc, argv);
}
