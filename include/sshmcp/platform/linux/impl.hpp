#pragma once

#include <sshmcp/platform/posix_common.hpp>
#include <sshmcp/subprocess_base.hpp>
#include <sshmcp/types.hpp>

#include <signal.h>

#include <expected>

namespace sshmcp {

class platform_impl_t : public subprocess_base_t<platform_impl_t> {
  public:
    using subprocess_base_t<platform_impl_t>::subprocess_base_t;

    auto init_stdio_impl() -> void {
        signal(SIGPIPE, SIG_IGN);
    }

    auto spawn_impl(spawn_request_t const& request)
        -> std::expected<spawn_result_t, error_t> {
        return posix_spawn_capture(request);
    }
};

} // namespace sshmcp
