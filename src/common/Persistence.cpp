#include "common/Persistence.hpp"

#include <toml++/toml.hpp>

#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <variant>

namespace neubau::common {

struct Persistence::State {
    explicit State(std::filesystem::path filePath)
        : path{std::move(filePath)} {}

    std::filesystem::path path;
    mutable std::mutex mutex;
};

namespace {

void validateThingId(std::string_view id) {
    if (id.empty()) {
        throw std::invalid_argument("thing id must not be empty");
    }
}

toml::table readProperties(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return {};
    }
    try {
        return toml::parse_file(path.string());
    } catch (const toml::parse_error& error) {
        throw std::runtime_error(
            "invalid TOML persistence file: "
            + std::string{error.description()});
    }
}

detail::StoredPropertyValue readValue(
    const toml::node& node,
    PropertyKey key) {
    if (node.is_boolean()) {
        return *node.value<bool>();
    }
    if (node.is_integer()) {
        return *node.value<std::int64_t>();
    }
    if (node.is_floating_point()) {
        return *node.value<double>();
    }
    if (node.is_string()) {
        return *node.value<std::string>();
    }
    throw std::invalid_argument(
        "unsupported TOML value for property "
        + propertyName(key));
}

void writeProperties(
    const std::filesystem::path& path,
    const toml::table& properties) {
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::trunc};
        if (!output) {
            throw std::runtime_error(
                "failed to open persistence file for writing: "
                + temporary.string());
        }
        output << properties;
        output.flush();
        if (!output) {
            throw std::runtime_error(
                "failed while writing persistence file: "
                + temporary.string());
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (!error) {
        return;
    }

    // Some platforms cannot atomically rename over an existing file.
    error.clear();
    std::filesystem::copy_file(
        temporary,
        path,
        std::filesystem::copy_options::overwrite_existing,
        error);
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    if (error) {
        throw std::runtime_error(
            "failed to replace persistence file: "
            + error.message());
    }
}

} // namespace

detail::StoredPropertyValue
detail::PropertyCodec<Seconds>::encode(Seconds value) {
    return static_cast<std::int64_t>(value.count());
}

Seconds detail::PropertyCodec<Seconds>::decode(
    const StoredPropertyValue& value) {
    const auto* count = std::get_if<std::int64_t>(&value);
    if (count == nullptr
        || *count < std::numeric_limits<Seconds::rep>::min()
        || *count > std::numeric_limits<Seconds::rep>::max()) {
        throw std::invalid_argument(
            "persisted seconds value must be an integer");
    }
    return Seconds{static_cast<Seconds::rep>(*count)};
}

Persistence::Persistence()
    : Persistence{std::filesystem::path{configFilePath}} {}

Persistence::Persistence(std::filesystem::path path)
    : _state{std::make_shared<State>(std::move(path))} {
    if (_state->path.empty()) {
        throw std::invalid_argument(
            "persistence file path must not be empty");
    }
}

Persistence::~Persistence() = default;

std::optional<std::string> Persistence::restoreThingName(
    std::string_view id) const {
    validateThingId(id);

    std::scoped_lock lock{_state->mutex};
    const auto properties = readProperties(_state->path);
    const auto* things = properties.get_as<toml::table>("things");
    if (things == nullptr) {
        return std::nullopt;
    }

    const auto* thing = things->get_as<toml::table>(id);
    if (thing == nullptr) {
        return std::nullopt;
    }

    const auto* name = thing->get("name");
    if (name == nullptr) {
        return std::nullopt;
    }
    if (!name->is_string()) {
        throw std::invalid_argument(
            "persisted thing name must be a string");
    }

    return *name->value<std::string>();
}

void Persistence::saveThingName(
    std::string_view id,
    std::string_view name) {
    validateThingId(id);

    std::scoped_lock lock{_state->mutex};
    auto properties = readProperties(_state->path);

    auto* things = properties.get_as<toml::table>("things");
    if (things == nullptr) {
        properties.insert_or_assign("things", toml::table{});
        things = properties.get_as<toml::table>("things");
    }

    auto* thing = things->get_as<toml::table>(id);
    if (thing == nullptr) {
        things->insert_or_assign(std::string{id}, toml::table{});
        thing = things->get_as<toml::table>(id);
    }
    thing->insert_or_assign("name", std::string{name});

    writeProperties(_state->path, properties);
}

std::optional<detail::StoredPropertyValue> Persistence::read(
    PropertyKey key) const {
    std::scoped_lock lock{_state->mutex};
    const auto properties = readProperties(_state->path);
    const auto* node = properties.get(propertyName(key));
    if (node == nullptr) {
        return std::nullopt;
    }
    return readValue(*node, key);
}

void Persistence::write(
    PropertyKey key,
    detail::StoredPropertyValue value) {
    std::scoped_lock lock{_state->mutex};
    auto properties = readProperties(_state->path);
    const auto name = propertyName(key);
    std::visit(
        [&properties, &name](const auto& stored) {
            properties.insert_or_assign(name, stored);
        },
        value);
    writeProperties(_state->path, properties);
}

} // namespace neubau::common
