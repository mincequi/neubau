#include "common/Timer.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "shelly/ShellyDiscovery.hpp"
#include "webapp/WebAppService.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

void printShellyDevices() {
    std::cout << "Discovering Shelly devices...\n" << std::flush;
    auto discovery =
        std::make_shared<neubau::shelly::ShellyDiscovery>();
    auto count = std::make_shared<std::size_t>(0);
    static_cast<void>(discovery->discover().collect(
            [count](const neubau::shelly::ShellyThing& thing) {
                std::cout << thing;
                ++*count;
            },
            [discovery](std::exception_ptr error) {
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception& exception) {
                    std::cerr << "Shelly discovery failed: "
                              << exception.what() << '\n';
                }
            },
            [discovery, count] {
                std::cout << "Discovered " << *count << " Shelly device"
                          << (*count == 1 ? "" : "s") << ".\n"
                          << std::flush;
            }));
}

class PeriodicModbusDiscovery {
public:
    explicit PeriodicModbusDiscovery(std::string cidr)
        : _cidr{std::move(cidr)} {
        scan();
        static_cast<void>(
            _timer.epochAlignedTicks(std::chrono::minutes{1})
                .collect([this](const auto&) { scan(); }));
    }

    ~PeriodicModbusDiscovery() {
        _timer.stop();
        if (_active) {
            _active->stop();
        }
    }

private:
    void scan() {
        if (_active) {
            return;
        }
        _active = std::make_shared<neubau::modbus::ModbusDiscovery>(
            neubau::modbus::ModbusDiscoveryOptions{
            .cidrs = {_cidr},
            .unitIds = {1},
            .connectTimeout = std::chrono::milliseconds{150},
            .responseTimeout = std::chrono::milliseconds{300},
            .maxConcurrency = 32,
        });

        auto output = std::make_shared<std::ostringstream>();
        auto count = std::make_shared<std::size_t>(0);
        static_cast<void>(_active->discover().collect(
            [output, count](const neubau::modbus::ModbusThing& thing) {
                *output << thing;
                ++*count;
            },
            [this](std::exception_ptr error) {
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception& exception) {
                    std::cerr << "Modbus discovery failed: "
                              << exception.what() << '\n';
                }
                _active.reset();
            },
            [this, output, count] {
                *output << "Modbus scan of " << _cidr << " found "
                        << *count << " device"
                        << (*count == 1 ? "" : "s") << ".\n";
                std::cout << output->str() << std::flush;
                _active.reset();
            }));
    }

    std::string _cidr;
    neubau::common::Timer _timer;
    std::shared_ptr<neubau::modbus::ModbusDiscovery> _active;
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
