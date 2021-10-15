#ifndef BAULK_FS_HPP
#define BAULK_FS_HPP
#include <bela/base.hpp>
#include <bela/fs.hpp>
#include <optional>
#include <string_view>
#include <filesystem>

namespace baulk::fs {

bool IsExecutablePath(std::wstring_view p);
std::optional<std::wstring> FindExecutablePath(std::wstring_view p);

inline std::wstring FileName(std::wstring_view p) {
  return std::filesystem::path(p).filename().wstring();
}

inline bool MakeDir(std::wstring_view path, bela::error_code &ec) {
  std::error_code e;
  if (std::filesystem::exists(path, e)) {
    return true;
  }
  if (!std::filesystem::create_directories(path, e)) {
    ec = bela::from_std_error_code(e);
    return false;
  }
  return true;
}

inline bool MakeParentDir(std::wstring_view path, bela::error_code &ec) {
  std::error_code e;
  std::filesystem::path p(path);
  auto parent = p.parent_path();
  if (std::filesystem::exists(parent, e)) {
    return true;
  }
  if (!std::filesystem::create_directories(parent, e)) {
    ec = bela::from_std_error_code(e);
    return false;
  }
  return true;
}

inline bool SymLink(std::wstring_view _To, std::wstring_view NewLink, bela::error_code &ec) {
  std::error_code e;
  std::filesystem::create_symlink(_To, NewLink, e);
  if (e) {
    ec = bela::from_std_error_code(e);
    return false;
  }
  return true;
}
std::optional<std::wstring> UniqueSubdirectory(std::wstring_view dir);
bool FlatPackageInitialize(std::wstring_view dir, std::wstring_view dest, bela::error_code &ec);
std::optional<std::wstring> BaulkMakeTempDir(bela::error_code &ec);
} // namespace baulk::fs

#endif