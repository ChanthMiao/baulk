// baulk cli parser
#ifndef BAULK_CLI_ARGV_HPP
#define BAULK_CLI_ARGV_HPP
#include <bela/base.hpp>
#include <functional>

namespace baulk::cli {
enum HasArgs {
  required_argument, /// -i 11 or -i=xx
  no_argument,
  optional_argument /// -s --long --long=xx
};

struct option {
  option() = default;
  option(std::wstring_view n, HasArgs ha, int va) : name(n), has_args(ha), val(va) {} /// emplace_back
  std::wstring_view name;
  HasArgs has_args{no_argument};
  int val{0};
};
struct subcommands {
  std::vector<std::wstring_view> cmds;
  size_t Add(std::wstring_view cmd) {
    cmds.emplace_back(cmd);
    return cmds.size();
  }
  bool Contains(std::wstring_view cmd) const {
    for (auto c : cmds) {
      if (c == cmd) {
        return true;
      }
    }
    return false;
  }
};

using Receiver = std::function<bool(int, const wchar_t *, const wchar_t *)>;

class ParseArgv {
public:
  using StringArray = std::vector<std::wstring_view>;
  using Iter = std::vector<std::wstring_view>::iterator;
  ParseArgv(int argc, wchar_t *const *argv) {
    rawargv.reserve(argc);
    for (int i = 0; i < argc; i++) {
      rawargv.emplace_back(argv[i]);
    }
  }
  ParseArgv(const StringArray &argv) {
    // argv
    rawargv.assign(argv.begin(), argv.end());
  }
  ParseArgv(const Iter begin, const Iter end) {
    // iter
    rawargv.assign(begin, end);
  }
  ParseArgv(const ParseArgv &) = delete;
  ParseArgv &operator=(const ParseArgv &) = delete;
  ParseArgv &Add(std::wstring_view name, HasArgs a, int val) {
    options_.emplace_back(name, a, val);
    return *this;
  }
  ParseArgv &Add(std::wstring_view subcmd) {
    subcmds_.Add(subcmd);
    return *this;
  }
  bool Execute(const Receiver &v, bela::error_code &ec);
  const StringArray &Argv() const { return argv; }

private:
  std::vector<std::wstring_view> rawargv;
  size_t index{0};
  StringArray argv;
  std::vector<option> options_;
  subcommands subcmds_;
  bool parse_internal(std::wstring_view a, const Receiver &v, bela::error_code &ec);
  bool parse_internal_long(std::wstring_view a, const Receiver &v, bela::error_code &ec);
  bool parse_internal_short(std::wstring_view a, const Receiver &v, bela::error_code &ec);
};

// ---------> parse internal
inline bool ParseArgv::Execute(const Receiver &v, bela::error_code &ec) {
  const auto argc = rawargv.size();
  for (; index < argc; index++) {
    std::wstring_view a = rawargv[index];
    if (a.empty() || a.front() != '-') {
      if (subcmds_.Contains(a)) {
        for (size_t i = index; i < argc; i++) {
          argv.emplace_back(rawargv[i]);
        }
        return true;
      }
      argv.emplace_back(a);
      continue;
    }
    // parse ---
    if (!parse_internal(a, v, ec)) {
      return false;
    }
  }
  return true;
}

inline bool ParseArgv::parse_internal_short(std::wstring_view a, const Receiver &v, bela::error_code &ec) {
  int ch = -1;
  HasArgs ha = optional_argument;
  const wchar_t *oa = nullptr;
  // -x=XXX
  // -xXXX
  // -x XXX
  // -x; BOOL
  if (a[0] == '=') {
    // -=*
    ec = bela::make_error_code(bela::ErrParseBroken, L"unexpected argument '-", a, L"'");
    return false;
  }
  auto c = a[0];
  for (const auto &o : options_) {
    if (o.val == c) {
      ch = o.val;
      ha = o.has_args;
      break;
    }
  }
  if (ch == -1) {
    ec = bela::make_error_code(bela::ErrParseBroken, L"unregistered option '-", a, L"'");
    return false;
  }
  // a.size()==1 'L' short value
  if (a.size() >= 2) {
    oa = (a[1] == L'=') ? (a.data() + 2) : (a.data() + 1);
  }
  if (oa != nullptr && ha == no_argument) {
    ec = bela::make_error_code(bela::ErrParseBroken, L"option '-", a.substr(0, 1), L"' unexpected parameter: ", oa);
    return false;
  }
  if (oa == nullptr && ha == required_argument) {
    if (index + 1 >= rawargv.size()) {
      ec = bela::make_error_code(bela::ErrParseBroken, L"option '-", a, L"' missing parameter");
      return false;
    }
    oa = rawargv[index + 1].data();
    index++;
  }
  if (!v(ch, oa, a.data())) {
    ec = bela::make_error_code(bela::ErrSkipParse, L"skip parse");
    return false;
  }
  return true;
}

// Parse long option
inline bool ParseArgv::parse_internal_long(std::wstring_view a, const Receiver &v, bela::error_code &ec) {
  // --xxx=XXX
  // --xxx XXX
  // --xxx; bool
  int ch = -1;
  HasArgs ha = optional_argument;
  const wchar_t *oa = nullptr;
  auto pos = a.find(L'=');
  if (pos != std::string_view::npos) {
    if (pos + 1 >= a.size()) {
      ec = bela::make_error_code(bela::ErrParseBroken, L"unexpected argument '--", a, L"'");
      return false;
    }
    oa = a.data() + pos + 1;
    a.remove_suffix(a.size() - pos);
  }
  for (const auto &o : options_) {
    if (o.name == a) {
      ch = o.val;
      ha = o.has_args;
      break;
    }
  }
  if (ch == -1) {
    ec = bela::make_error_code(bela::ErrParseBroken, L"unregistered option '--", a, L"'");
    return false;
  }
  if (oa != nullptr && ha == no_argument) {
    ec = bela::make_error_code(bela::ErrParseBroken, L"option '--", a, L"' unexpected parameter: ", oa);
    return false;
  }
  if (oa == nullptr && ha == required_argument) {
    if (index + 1 >= rawargv.size()) {
      ec = bela::make_error_code(bela::ErrParseBroken, L"option '--", a, L"' missing parameter");
      return false;
    }
    oa = rawargv[index + 1].data();
    index++;
  }
  if (!v(ch, oa, a.data())) {
    ec = bela::make_error_code(bela::ErrSkipParse, L"skip parse");
    return false;
  }
  return true;
}

inline bool ParseArgv::parse_internal(std::wstring_view a, const Receiver &v, bela::error_code &ec) {
  if (a.size() == 1) {
    ec = bela::make_error_code(bela::ErrParseBroken, L"unexpected argument '-'");
    return false;
  }
  if (a[1] == '-') {
    return parse_internal_long(a.substr(2), v, ec);
  }
  return parse_internal_short(a.substr(1), v, ec);
}

} // namespace baulk::cli

#endif