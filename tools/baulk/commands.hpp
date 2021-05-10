//
#ifndef BAULK_COMMANDS_HPP
#define BAULK_COMMANDS_HPP
#include <string_view>
#include <vector>

namespace baulk::commands {
using argv_t = std::vector<std::wstring_view>;
void Usage();
int cmd_help(const argv_t &argv);
//
int cmd_install(const argv_t &argv);
int cmd_list(const argv_t &argv);
int cmd_search(const argv_t &argv);
int cmd_uninstall(const argv_t &argv);
int cmd_update(const argv_t &argv);
int cmd_upgrade(const argv_t &argv);
int cmd_update_and_upgrade(const argv_t &argv);
int cmd_freeze(const argv_t &argv);
int cmd_unfreeze(const argv_t &argv);
//
int cmd_b3sum(const argv_t &argv);
int cmd_sha256sum(const argv_t &argv);
//
int cmd_cleancache(const argv_t &argv);
//
int cmd_bucket(const argv_t &argv);
// archive
int cmd_unzip(const argv_t &argv);
int cmd_untar(const argv_t &argv);
} // namespace baulk::commands

#endif