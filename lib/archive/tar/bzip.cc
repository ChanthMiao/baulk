//
#include "bzip.hpp"

namespace baulk::archive::tar::bzip {
Reader::~Reader() {
  if (bzs != nullptr) {
    BZ2_bzDecompressEnd(bzs);
    baulk::mem::deallocate(bzs);
  }
}
bool Reader::Initialize(bela::error_code &ec) {
  bzs = baulk::mem::allocate<bz_stream>();
  memset(bzs, 0, sizeof(bz_stream));
  bzs->bzalloc = baulk::mem::allocate_bz;
  bzs->bzfree = baulk::mem::deallocate_simple;
  if (auto bzerr = BZ2_bzDecompressInit(bzs, 0, 0); bzerr != BZ_OK) {
    ec = bela::make_error_code(ErrExtractGeneral, L"BZ2_bzDecompressInit error");
    return false;
  }
  out.grow(outsize);
  in.grow(insize);
  return true;
}

bool Reader::decompress(bela::error_code &ec) {
  if (ret == BZ_STREAM_END) {
    ec = bela::make_error_code(bela::ErrEnded, L"bzip stream end");
    return false;
  }
  for (;;) {
    if (bzs->avail_out != 0 || pickBytes == 0) {
      auto n = r->Read(in.data(), in.capacity(), ec);
      if (n <= 0) {
        return false;
      }
      pickBytes += static_cast<int64_t>(n);
      bzs->next_in = reinterpret_cast<char *>(in.data());
      bzs->avail_in = static_cast<uint32_t>(n);
    }
    bzs->avail_out = static_cast<int>(out.capacity());
    bzs->next_out = reinterpret_cast<char *>(out.data());
    ret = BZ2_bzDecompress(bzs);
    switch (ret) {
    case BZ_DATA_ERROR:
      [[fallthrough]];
    case BZ_MEM_ERROR:
      ec = bela::make_error_code(ErrExtractGeneral, L"bzlib error ", ret);
      return false;
    default:
      break;
    }
    auto have = outsize - bzs->avail_out;
    out.size() = have;
    out.pos() = 0;
    if (have != 0) {
      break;
    }
  }
  return true;
}

ssize_t Reader::Read(void *buffer, size_t len, bela::error_code &ec) {
  if (out.pos() == out.size()) {
    if (!decompress(ec)) {
      return -1;
    }
  }
  auto minsize = (std::min)(len, out.size() - out.pos());
  memcpy(buffer, out.data() + out.pos(), minsize);
  out.pos() += minsize;
  return minsize;
}

bool Reader::Discard(int64_t len, bela::error_code &ec) {
  while (len > 0) {
    if (out.pos() == out.size()) {
      if (!decompress(ec)) {
        return false;
      }
    }
    // seek position
    auto minsize = (std::min)(static_cast<size_t>(len), out.size() - out.pos());
    out.pos() += minsize;
    len -= minsize;
  }
  return true;
}

// Avoid multiple memory copies
bool Reader::WriteTo(const Writer &w, int64_t filesize, int64_t &extracted, bela::error_code &ec) {
  while (filesize > 0) {
    if (out.pos() == out.size()) {
      if (!decompress(ec)) {
        return false;
      }
    }
    auto minsize = (std::min)(static_cast<size_t>(filesize), out.size() - out.pos());
    auto p = out.data() + out.pos();
    out.pos() += minsize;
    filesize -= minsize;
    extracted += minsize;
    if (!w(p, minsize, ec)) {
      return false;
    }
  }
  return true;
}

} // namespace baulk::archive::tar::bzip