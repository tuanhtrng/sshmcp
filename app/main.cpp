#include <sshmcp/config.hpp>
#include <sshmcp/mcp.hpp>
#include <sshmcp/subprocess.hpp>
#include <sshmcp/tools.hpp>

#include <cstdlib>
#include <iostream>
#include <print>
#include <string>
#include <utility>
#include <vector>

auto main(int argc, char** argv) -> int {
    auto args = std::vector<std::string>{};
    for (auto i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    auto const config = sshmcp::load_config(
        [](char const* name) -> char const* {
            return std::getenv(name);
        },
        args);
    if (!config) {
        std::println(stderr, "sshmcp: {}",
                     config.error().message);
        return 2;
    }
    auto impl = sshmcp::platform_impl_t{config->log_level};
    impl.init_stdio();
    auto server = sshmcp::server_t<sshmcp::exec_tool_t,
                                   sshmcp::read_file_tool_t,
                                   sshmcp::write_file_tool_t>{
        sshmcp::context_t{
            .config = *config,
            .spawn =
                [&impl](sshmcp::spawn_request_t const& request) {
                    return impl.run(request);
                }}};
    return server.run(std::cin, std::cout);
}
