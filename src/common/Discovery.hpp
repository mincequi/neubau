#pragma once

#include "common/flow.hpp"

namespace neubau::common {

template<typename Candidate>
class Discovery {
public:
    virtual ~Discovery() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual const Flow<Candidate>& candidates()
        const noexcept = 0;
};

} // namespace neubau::common
