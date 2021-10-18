///
#include "app.hpp"
//
#include <windowsx.h> // box help
#include <CommCtrl.h>
#include <bela/path.hpp>
#include <bela/match.hpp>
#include <bela/picker.hpp>
#include <bela/env.hpp>
#include <bela/escapeargv.hpp>
#include <bela/io.hpp>
#include <jsonex.hpp>
#include <baulkenv.hpp>
#include <filesystem>

namespace baulk::dock {
constexpr const wchar_t *string_nullable(std::wstring_view str) { return str.empty() ? nullptr : str.data(); }
constexpr wchar_t *string_nullable(std::wstring &str) { return str.empty() ? nullptr : str.data(); }

std::optional<std::wstring> FindWindowsTerminal() {
  auto wt = bela::WindowsExpandEnv(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\wt.exe");
  if (!bela::PathExists(wt)) {
    return std::nullopt;
  }
  return std::make_optional(std::move(wt));
}

bool SearchBaulk(std::wstring &baulkroot, bela::error_code &ec) {
  auto exepath = bela::ExecutableFinalPathParent(ec);
  if (!exepath) {
    return false;
  }
  auto baulkexe = bela::StringCat(*exepath, L"\\baulk.exe");
  if (bela::PathExists(baulkexe)) {
    baulkroot = bela::DirName(*exepath);
    return true;
  }
  std::wstring bkroot(*exepath);
  for (size_t i = 0; i < 5; i++) {
    auto baulkexe = bela::StringCat(bkroot, L"\\bin\\baulk.exe");
    if (bela::PathExists(baulkexe)) {
      baulkroot = bkroot;
      return true;
    }
    bela::PathStripName(bkroot);
  }
  ec = bela::make_error_code(bela::ErrGeneral, L"unable found baulk.exe");
  return false;
}

inline constexpr bool DirSkipFaster(const wchar_t *dir) {
  return (dir[0] == L'.' && (dir[1] == L'\0' || (dir[1] == L'.' && dir[2] == L'\0')));
}

class Finder {
public:
  Finder() noexcept = default;
  Finder(const Finder &) = delete;
  Finder &operator=(const Finder &) = delete;
  ~Finder() noexcept {
    if (hFind != INVALID_HANDLE_VALUE) {
      FindClose(hFind);
    }
  }
  const WIN32_FIND_DATAW &FD() const { return wfd; }
  bool Ignore() const { return DirSkipFaster(wfd.cFileName); }
  bool IsDir() const { return (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
  std::wstring_view Name() const { return std::wstring_view(wfd.cFileName); }
  bool Next() { return FindNextFileW(hFind, &wfd) == TRUE; }
  bool First(std::wstring_view dir, std::wstring_view suffix, bela::error_code &ec) {
    auto d = bela::StringCat(dir, L"\\", suffix);
    hFind = FindFirstFileW(d.data(), &wfd);
    if (hFind == INVALID_HANDLE_VALUE) {
      ec = bela::make_system_error_code();
      return false;
    }
    return true;
  }

private:
  HANDLE hFind{INVALID_HANDLE_VALUE};
  WIN32_FIND_DATAW wfd;
};

bool SearchVirtualEnv(std::wstring_view lockfile, std::wstring_view pkgname, EnvNode &node) {
  bela::error_code ec;
  FILE *fd = nullptr;
  if (auto en = _wfopen_s(&fd, lockfile.data(), L"rb"); en != 0) {
    ec = bela::make_stdc_error_code(en, bela::StringCat(L"open '", lockfile, L"'"));
    return false;
  }
  auto closer = bela::finally([&] { fclose(fd); });
  constexpr std::wstring_view space = L"                ";
  try {
    auto j = nlohmann::json::parse(fd, nullptr, true, true);
    auto version = bela::encode_into<char, wchar_t>(j["version"].get<std::string_view>());
    if (auto it = j.find("venv"); it != j.end() && it.value().is_object()) {
      node.Value = pkgname;
      node.Desc = pkgname;
      baulk::json::JsonAssignor jea(it.value());
      if (auto category = jea.get("category"); !category.empty()) {
        bela::StrAppend(&node.Desc, L"(", category, L")    ", version, L"");
      }
      return true;
    }
  } catch (const std::exception &e) {
    ec = bela::make_error_code(bela::ErrGeneral, L"parse package '", lockfile, L"' json: ", bela::encode_into<char, wchar_t>(e.what()));
    return false;
  }
  return false;
}

bool MainWindow::InitializeBase(bela::error_code &ec) {
  if (!SearchBaulk(baulkroot, ec)) {
    return false;
  }
  auto locksdir = bela::StringCat(baulkroot, L"\\bin\\locks");
  Finder finder;
  if (finder.First(locksdir, L"*.json", ec)) {
    do {
      if (finder.Ignore()) {
        continue;
      }
      auto pkgname = finder.Name();
      if (!bela::EndsWithIgnoreCase(pkgname, L".json")) {
        continue;
      }
      auto lockfile = bela::StringCat(locksdir, L"\\", pkgname);
      pkgname.remove_suffix(5);
      baulk::dock::EnvNode node;
      if (SearchVirtualEnv(lockfile, pkgname, node)) {
        tables.AddVirtualEnv(std::move(node));
      }
    } while (finder.Next());
  }
  return true;
}

bool MainWindow::LoadPlacement(WINDOWPLACEMENT &placement) {
  auto posfile = bela::StringCat(baulkroot, L"\\bin\\etc\\baulk-dock.pos.json");
  FILE *fd = nullptr;
  if (auto en = _wfopen_s(&fd, posfile.data(), L"rb"); en != 0) {
    return false;
  }
  auto closer = bela::finally([&] { fclose(fd); });
  try {
    auto j = nlohmann::json::parse(fd, nullptr, true, true);
    placement.flags = j["flags"];
    placement.ptMaxPosition.x = j["ptMaxPosition.X"];
    placement.ptMaxPosition.y = j["ptMaxPosition.Y"];
    placement.ptMinPosition.x = j["ptMinPosition.X"];
    placement.ptMinPosition.y = j["ptMinPosition.Y"];
    placement.showCmd = j["showCmd"];
    placement.rcNormalPosition.bottom = j["rcNormalPosition.bottom"];
    placement.rcNormalPosition.left = j["rcNormalPosition.left"];
    placement.rcNormalPosition.right = j["rcNormalPosition.right"];
    placement.rcNormalPosition.top = j["rcNormalPosition.top"];
  } catch (const std::exception &) {
    return false;
  }
  return true;
}
void MainWindow::SavePlacement(const WINDOWPLACEMENT &placement) {
  auto etcdir = bela::StringCat(baulkroot, L"\\bin\\etc");
  std::error_code e;
  if (std::filesystem::create_directories(etcdir, e); e) {
    return;
  }
  auto posfile = bela::StringCat(etcdir, L"\\baulk-dock.pos.json");
  nlohmann::json j;
  j["flags"] = placement.flags;
  j["ptMaxPosition.X"] = placement.ptMaxPosition.x;
  j["ptMaxPosition.Y"] = placement.ptMaxPosition.y;
  j["ptMinPosition.X"] = placement.ptMinPosition.x;
  j["ptMinPosition.Y"] = placement.ptMinPosition.y;
  j["showCmd"] = placement.showCmd;
  j["rcNormalPosition.bottom"] = placement.rcNormalPosition.bottom;
  j["rcNormalPosition.left"] = placement.rcNormalPosition.left;
  j["rcNormalPosition.right"] = placement.rcNormalPosition.right;
  j["rcNormalPosition.top"] = placement.rcNormalPosition.top;
  auto s = j.dump(4);
  bela::error_code ec;
  bela::io::WriteTextAtomic(s, posfile, ec);
}

template <size_t Len = 256> std::wstring GetCwd() {
  std::wstring s;
  s.resize(Len);
  auto len = GetCurrentDirectoryW(Len, s.data());
  if (len == 0) {
    return L"";
  }
  if (len < Len) {
    s.resize(len);
    return s;
  }
  s.resize(len);
  auto nlen = GetCurrentDirectoryW(len, s.data());
  if (nlen == 0 || nlen > len) {
    return L"";
  }
  s.resize(nlen);
  return s;
}

LRESULT MainWindow::OnStartupEnv(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL &bHandled) {
  auto baulkexec = bela::StringCat(baulkroot, L"\\bin\\baulk-exec.exe");
  auto cwd = GetCwd();
  bela::EscapeArgv ea;
  if (auto wt = FindWindowsTerminal(); wt) {
    ea.Assign(*wt).Append(L"--title").Append(L"Windows Terminal \U0001F496 Baulk").Append(L"--");
  }
  ea.Append(baulkexec).Append(L"-W").Append(GetCwd());
  if (Button_GetCheck(hclang.hWnd) == BST_CHECKED) {
    ea.Append(L"--clang");
  }
  if (Button_GetCheck(hcleanenv.hWnd) == BST_CHECKED) {
    ea.Append(L"--cleanup");
  }
  if (auto index = ComboBox_GetCurSel(hvsarchbox.hWnd);
      index >= 0 && static_cast<size_t>(index) < tables.Archs.size()) {
    ea.Append(L"--vs").Append(L"-A").Append(tables.Archs[index]);
  }
  if (auto index = ComboBox_GetCurSel(hvenvbox.hWnd); index >= 0 && static_cast<size_t>(index) < tables.Envs.size()) {
    ea.Append(L"-E").Append(tables.Envs[index].Value);
  }
  ea.Append(L"winsh");
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  SecureZeroMemory(&si, sizeof(si));
  SecureZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);
  if (CreateProcessW(nullptr, ea.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si,
                     &pi) != TRUE) {
    auto ec = bela::make_system_error_code();
    bela::BelaMessageBox(m_hWnd, L"unable open Windows Terminal", ec.data(), nullptr, bela::mbs_t::FATAL);
    return -1;
  }
  CloseHandle(pi.hThread);
  SetConsoleCtrlHandler(nullptr, TRUE);
  auto closer = bela::finally([&] {
    SetConsoleCtrlHandler(nullptr, FALSE);
    CloseHandle(pi.hProcess);
  });
  return S_OK;
}
} // namespace baulk::dock