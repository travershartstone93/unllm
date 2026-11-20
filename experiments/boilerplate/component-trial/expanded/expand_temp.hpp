#pragma once
#include "gzip_stream.hpp"
#include "provider_types.hpp"
#include <QTemporaryFile>

namespace bricks {
inline brick_data::ByteFile expand_bytes(const char *input) {
  struct Storage {
    QTemporaryFile file;
    std::string path;
  };
  auto storage = std::make_shared<Storage>();
  if (!storage->file.open())
    throw std::runtime_error("Cannot stage decompressed data");
  storage->path = storage->file.fileName().toStdString();
  storage->file.close();
  unllm::expand_gzip(input, storage->path.c_str());
  return brick_data::ByteFile(storage, &storage->path);
}
} // namespace bricks
