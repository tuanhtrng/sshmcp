#include <sshmcp/config.hpp>
#include <sshmcp/forward.hpp>
#include <sshmcp/mcp.hpp>
#include <sshmcp/session.hpp>
#include <sshmcp/subprocess.hpp>
#include <sshmcp/tools.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

auto main(int argc, char** argv) -> int {
    auto args = std::vector<std::string>{};
    for (auto i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    auto const config = sshmcp::load_config(
        [](char const* name) -> char const* { return std::getenv(name); },
        args);
    if (!config) {
        std::println(stderr, "sshmcp: {}", config.error().message);
        return 2;
    }
    auto impl = sshmcp::platform_impl_t{config->log_level};
    impl.init_stdio();
    impl.harden();
    auto manager =
        sshmcp::session_manager_t<sshmcp::platform_impl_t>{impl, *config};
    auto forwards =
        sshmcp::forward_manager_t<sshmcp::platform_impl_t>{impl, *config};
    auto server = sshmcp::server_t<
        sshmcp::exec_tool_t, sshmcp::read_file_tool_t,
        sshmcp::write_file_tool_t, sshmcp::session_close_tool_t,
        sshmcp::forward_open_tool_t, sshmcp::forward_close_tool_t,
        sshmcp::upload_file_tool_t, sshmcp::download_file_tool_t>{
        sshmcp::context_t{
            .config = *config,
            .spawn =
                [&impl](sshmcp::spawn_request_t const& request) {
                    return impl.run(request);
                },
            .session_exec =
                [&manager](std::string_view name, std::string_view command,
                           std::int64_t timeout_ms) {
                    return manager.exec(name, command, timeout_ms);
                },
            .session_close =
                [&manager](std::string_view name) {
                    return manager.close(name);
                },
            .forward_open =
                [&forwards](int local_port, std::string_view remote_host,
                            int remote_port) {
                    return forwards.open(local_port, remote_host, remote_port);
                },
            .forward_close =
                [&forwards](int local_port) {
                    return forwards.close(local_port);
                }}};
    return server.run(std::cin, std::cout);
}
