//
#include <bela/path.hpp>
#include <xml.hpp>
#include <jsonex.hpp>
#include <version.hpp>
#include "baulk.hpp"
#include "bucket.hpp"
#include "fs.hpp"
#include "net.hpp"
#include "decompress.hpp"

namespace baulk::bucket {
std::optional<std::wstring> BucketNewest(std::wstring_view bucketurl,
                                         bela::error_code &ec) {
  auto rss = bela::StringCat(bucketurl, L"/commits/master.atom");
  baulk::DbgPrint(L"Fetch RSS %s", rss);
  auto resp = baulk::net::RestGet(rss, ec);
  if (!resp) {
    return std::nullopt;
  }
  auto doc = baulk::xml::parse_string(resp->body, ec);
  if (!doc) {
    return std::nullopt;
  }
  // first entry child
  auto entry = doc->child("feed").child("entry");
  std::string_view id{entry.child("id").text().as_string()};
  if (auto pos = id.find('/'); pos != std::string_view::npos) {
    return std::make_optional(bela::ToWide(id.substr(pos + 1)));
  }
  ec = bela::make_error_code(1, L"bucket invaild id: ", bela::ToWide(id));
  return std::nullopt;
}

bool BucketUpdate(std::wstring_view bucketurl, std::wstring_view name,
                  std::wstring_view id, bela::error_code &ec) {
  // https://github.com/baulk/bucket/archive/master.zip
  auto master = bela::StringCat(bucketurl, L"/archive/", id, L".zip");
  auto outdir = bela::StringCat(baulk::BaulkRoot(), L"\\",
                                baulk::BucketsDirName, L"\\temp");
  if (!bela::PathExists(outdir) && !baulk::fs::MakeDir(outdir, ec)) {
    return false;
  }
  auto outfile = baulk::net::WinGet(master, outdir, true, ec);
  if (!outfile) {
    return false;
  }
  auto deleter = bela::finally([&] {
    bela::error_code ec_;
    baulk::fs::PathRemove(*outfile, ec_);
  });
  auto decompressdir = bela::StringCat(outdir, L"\\", name);
  if (!baulk::zip::Decompress(*outfile, decompressdir, ec)) {
    return false;
  }
  baulk::standard::Regularize(decompressdir);
  auto bucketdir = bela::StringCat(baulk::BaulkRoot(), L"\\",
                                   baulk::BucketsDirName, L"\\", name);
  if (bela::PathExists(bucketdir)) {
    baulk::fs::PathRemove(bucketdir, ec);
  }
  if (MoveFileW(decompressdir.data(), bucketdir.data()) != TRUE) {
    ec = bela::make_system_error_code();
    return false;
  }
  return true;
}

bool PackageInstalled(std::wstring_view pkglock, baulk::Package &pkg) {
  FILE *fd = nullptr;
  if (auto en = _wfopen_s(&fd, pkglock.data(), L"rb"); en != 0) {
    auto ec = bela::make_stdc_error_code(en);
    bela::FPrintF(stderr, L"unable load %s error: %s\n", pkglock, ec.message);
    return false;
  }
  auto closer = bela::finally([&] { fclose(fd); });
  try {
    auto j = nlohmann::json::parse(fd);
    pkg.version = bela::ToWide(j["version"].get<std::string_view>());
    pkg.bucket = bela::ToWide(j["bucket"].get<std::string_view>());
    if (auto it = j.find("weights"); it != j.end()) {
      it.value().get_to(pkg.weights);
    }
  } catch (const std::exception &e) {
    bela::FPrintF(stderr, L"unable decode metadata. error: %s\n", e.what());
    return false;
  }
  return true;
}

std::optional<baulk::Package> PackageMeta(std::wstring_view pkgmeta,
                                          bela::error_code &ec) {
  if (!bela::PathExists(pkgmeta)) {
    return std::nullopt;
  }
  FILE *fd = nullptr;
  if (auto en = _wfopen_s(&fd, pkgmeta.data(), L"rb"); en != 0) {
    ec = bela::make_stdc_error_code(en);
    return std::nullopt;
  }
  auto closer = bela::finally([&] { fclose(fd); });
  baulk::Package pkg;
  try {
    auto j = nlohmann::json::parse(fd);
    baulk::json::JsonAssignor ja(j);
    pkg.description = ja.get("description");
    pkg.version = ja.get("version");
    pkg.extension = ja.get("extension");
    // load version
#ifdef _M_X64
    if (auto it = j.find("url64"); it != j.end()) {
      pkg.url = bela::ToWide(it.value().get<std::string_view>());
      if (auto h = j.find("url64.hash"); h != j.end()) {
        pkg.checksum = bela::ToWide(it.value().get<std::string_view>());
      }
    } else if (auto it = j.find("url"); it != j.end()) {
      pkg.url = bela::ToWide(it.value().get<std::string_view>());
      if (auto h = j.find("url.hash"); h != j.end()) {
        pkg.checksum = bela::ToWide(it.value().get<std::string_view>());
      }
    }
// AMD64 code
#else
    if (auto it = j.find("url"); it != j.end()) {
      pkg.url = bela::ToWide(it.value().get<std::string_view>());
      if (auto h = j.find("url.hash"); h != j.end()) {
        pkg.checksum = bela::ToWide(it.value().get<std::string_view>());
      }
    }
#endif
    if (pkg.url.empty()) {
      ec = bela::make_error_code(1, pkgmeta, L" not yet ported.");
      return std::nullopt;
    }
    if (auto it = j.find("links"); it != j.end()) {
      for (const auto &o : it.value()) {
        pkg.links.emplace_back(bela::ToWide(o.get<std::string_view>()));
      }
    }
    if (auto it = j.find("launchers"); it != j.end()) {
      for (const auto &o : it.value()) {
        pkg.launchers.emplace_back(bela::ToWide(o.get<std::string_view>()));
      }
    }
  } catch (const std::exception &e) {
    ec = bela::make_error_code(1, L"parse package meta json: ",
                               bela::ToWide(e.what()));
    return std::nullopt;
  }
  return std::make_optional(std::move(pkg));
}

bool PackageIsUpdatable(std::wstring_view pkgname, std::wstring_view path,
                        baulk::Package &pkg) {
  if (!PackageInstalled(path, pkg)) {
    return false;
  }
  // initialize version from installed version
  baulk::version::version pkgversion(pkg.version);
  bool hasnewest{false};
  auto bucketsDir =
      bela::StringCat(baulk::BaulkRoot(), L"\\", baulk::BucketsDirName);
  for (const auto &bk : baulk::BaulkBuckets()) {
    auto pkgmeta =
        bela::StringCat(bucketsDir, L"\\", bk.name, L"\\", pkgname, L".json");
    bela::error_code ec;
    auto pkgN = PackageMeta(pkgmeta, ec);
    if (!pkgN) {
      if (ec) {
        bela::FPrintF(stderr, L"Parse package: %s %s error: %s\n", pkgname,
                      pkgmeta, ec.message);
      }
      continue;
    }
    pkgN->bucket = bk.name;
    pkgN->weights = bk.weights;
    baulk::version::version newversion(pkgN->version);
    if (newversion > pkgversion ||
        (newversion == pkgversion && pkg.version != pkgN->version &&
         pkg.weights < pkgN->weights)) {
      pkg = std::move(*pkgN);
      hasnewest = true;
      continue;
    }
  }
  return hasnewest;
}

} // namespace baulk::bucket
