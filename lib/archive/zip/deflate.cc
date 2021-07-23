///
#include "zipinternal.hpp"
#include <zlib.h>

namespace baulk::archive::zip {
// DEFLATE
// https://github.com/madler/zlib/blob/master/examples/zpipe.c#L92
bool Reader::decompressDeflate(const File &file, const Writer &w, bela::error_code &ec) const {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  if (auto zerr = inflateInit2(&zs, -MAX_WBITS); zerr != Z_OK) {
    ec = bela::make_error_code(ErrGeneral, bela::ToWide(zError(zerr)));
    return false;
  }
  auto closer = bela::finally([&] { inflateEnd(&zs); });
  Buffer out(outsize);
  Buffer in(insize);
  int64_t uncsize = 0;
  auto csize = file.compressedSize;
  int ret = Z_OK;
  uint32_t crc32val = 0;
  while (csize != 0) {
    auto minsize = (std::min)(csize, static_cast<uint64_t>(insize));
    if (!fd.ReadFull({in.data(), static_cast<size_t>(minsize)}, ec)) {
      return false;
    }
    zs.avail_in = static_cast<int>(minsize);
    if (zs.avail_in == 0) {
      break;
    }
    zs.next_in = in.data();
    do {
      zs.avail_out = static_cast<int>(outsize);
      zs.next_out = out.data();
      ret = ::inflate(&zs, Z_NO_FLUSH);
      switch (ret) {
      case Z_NEED_DICT:
        ret = Z_DATA_ERROR;
        [[fallthrough]];
      case Z_DATA_ERROR:
        [[fallthrough]];
      case Z_MEM_ERROR:
        ec = bela::make_error_code(ret, bela::ToWide(zError(ret)));
        return false;
      default:
        break;
      }
      auto have = outsize - zs.avail_out;
      crc32val = crc32_fast(out.data(), have, crc32val);
      if (!w(out.data(), have)) {
        ec = bela::make_error_code(ErrCanceled, L"canceled");
        return false;
      }
    } while (zs.avail_out == 0);
    csize -= minsize;
    if (ret == Z_STREAM_END) {
      break;
    }
  }
  if (crc32val != file.crc32sum) {
    ec = bela::make_error_code(ErrGeneral, L"crc32 want ", file.crc32sum, L" got ", crc32val, L" not match");
    return false;
  }
  return true;
}
} // namespace baulk::archive::zip