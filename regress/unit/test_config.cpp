#include "harness.hpp"

#include <sshmcp/config.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace t = sshmcp::test;

auto env_of(std::map<std::string, std::string> values) -> sshmcp::get_env_fn_t {
    return [values = std::move(values)](char const* name) -> char const* {
        auto const found = values.find(name);
        return found == values.end() ? nullptr : found->second.c_str();
    };
}

[[maybe_unused]] auto const t1 = t::add_test("target sources", [] {
    auto const args = std::vector<std::string>{};
    auto const from_env =
        sshmcp::load_config(env_of({{"SSHMCP_TARGET", "dev@host"}}), args);
    t::expect(from_env.has_value(), "env target ok");
    t::expect(from_env->target == "dev@host", "env target");

    auto const arg_list = std::vector<std::string>{"me@vps"};
    auto const from_args = sshmcp::load_config(env_of({}), arg_list);
    t::expect(from_args.has_value(), "args target ok");
    t::expect(from_args->target == "me@vps", "args target");

    auto const missing = sshmcp::load_config(env_of({}), args);
    t::expect(!missing.has_value(), "missing target fails");
});

[[maybe_unused]] auto const t2 = t::add_test("ssh exe + args", [] {
    auto const args = std::vector<std::string>{"a@b"};
    auto const config = sshmcp::load_config(
        env_of(
            {{"SSHMCP_SSH_EXE", "\"C:\\Program Files\\Python\\python\" f.py"},
             {"SSHMCP_SSH_ARGS", "-p 2222"}}),
        args);
    t::expect(config.has_value(), "loads");
    auto const want_exe =
        std::vector<std::string>{"C:\\Program Files\\Python\\python", "f.py"};
    t::expect(config->ssh_exe == want_exe, "exe split");
    auto const want_args = std::vector<std::string>{"-p", "2222"};
    t::expect(config->ssh_args == want_args, "args split");

    auto const blank =
        sshmcp::load_config(env_of({{"SSHMCP_SSH_EXE", "  "}}), args);
    t::expect(!blank.has_value(), "blank exe fails");
});

[[maybe_unused]] auto const t3 = t::add_test("log level", [] {
    auto const args = std::vector<std::string>{"a@b"};
    auto const debug =
        sshmcp::load_config(env_of({{"SSHMCP_LOG", "debug"}}), args);
    t::expect(debug->log_level == sshmcp::log_level_t::DEBUG, "debug");
    auto const off = sshmcp::load_config(env_of({{"SSHMCP_LOG", "off"}}), args);
    t::expect(off->log_level == sshmcp::log_level_t::OFF, "off");
    auto const fallback = sshmcp::load_config(env_of({}), args);
    t::expect(fallback->log_level == sshmcp::log_level_t::INFO, "default info");
});

[[maybe_unused]] auto const t4 = t::add_test("allow env", [] {
    auto const args = std::vector<std::string>{"a@b"};
    auto const config =
        sshmcp::load_config(env_of({{"SSHMCP_ALLOW", "git, make ,ls"}}), args);
    auto const want = std::vector<std::string>{"git", "make", "ls"};
    t::expect(config->allow == want, "allow parsed");
    auto const off = sshmcp::load_config(env_of({}), args);
    t::expect(off->allow.empty(), "default empty");
});

} // namespace
