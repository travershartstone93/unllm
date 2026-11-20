#pragma once
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>
namespace bricks::archives {
using Strings = std::vector<std::string>;
inline void checked(int rc, archive *a) {
  if (rc != ARCHIVE_OK)
    throw std::runtime_error(archive_error_string(a)
                                 ? archive_error_string(a)
                                 : "Archive operation failed");
}
template <class F> void visit(const char *path, F f) {
  std::unique_ptr<archive, decltype(&archive_read_free)> a(archive_read_new(),
                                                           archive_read_free);
  if (!a)
    throw std::bad_alloc();
  checked(archive_read_support_filter_all(a.get()), a.get());
  checked(archive_read_support_format_all(a.get()), a.get());
  checked(archive_read_open_filename(a.get(), path, 32768), a.get());
  archive_entry *e = nullptr;
  int rc;
  while ((rc = archive_read_next_header(a.get(), &e)) == ARCHIVE_OK)
    f(a.get(), e);
  if (rc != ARCHIVE_EOF)
    checked(rc, a.get());
  checked(archive_read_close(a.get()), a.get());
}
template <int Type = 0> Strings names(const char *p) {
  Strings out;
  visit(p, [&](archive *, archive_entry *e) {
    if (Type == 0 || archive_entry_filetype(e) == Type)
      out.emplace_back(archive_entry_pathname(e));
  });
  return out;
}
inline std::vector<std::int64_t> sizes(const char *p) {
  std::vector<std::int64_t> out;
  visit(p, [&](archive *, archive_entry *e) {
    if (archive_entry_filetype(e) == AE_IFREG) {
      if (!archive_entry_size_is_set(e))
        throw std::runtime_error("Unknown member size");
      out.push_back(archive_entry_size(e));
    }
  });
  return out;
}
inline Strings formats(const char *p) {
  Strings out;
  visit(p, [&](archive *a, archive_entry *) {
    if (out.empty())
      out.emplace_back(archive_format_name(a));
  });
  return out;
}
inline void extract(const char *p, const std::string &member, const char *out) {
  QSaveFile file(QString::fromUtf8(out));
  if (!file.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot open destination");
  bool found = false;
  visit(p, [&](archive *a, archive_entry *e) {
    if (member != archive_entry_pathname(e) ||
        archive_entry_filetype(e) != AE_IFREG)
      return;
    if (found)
      throw std::runtime_error("Ambiguous member");
    found = true;
    std::array<char, 32768> buf;
    la_ssize_t n;
    while ((n = archive_read_data(a, buf.data(), buf.size())) > 0)
      if (file.write(buf.data(), n) != n)
        throw std::runtime_error("Write failed");
    if (n < 0)
      throw std::runtime_error("Archive read failed");
  });
  if (!found || !file.commit())
    throw std::runtime_error("Member missing or write failed");
}
template <int Format> void pack(const char *p, const char *dest) {
  QFile src(QString::fromUtf8(p));
  QSaveFile out(QString::fromUtf8(dest));
  if (!QFileInfo(src).isFile() || !src.open(QIODevice::ReadOnly) ||
      !out.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot open archive paths");
  std::unique_ptr<archive, decltype(&archive_write_free)> a(archive_write_new(),
                                                            archive_write_free);
  if (!a)
    throw std::bad_alloc();
  checked(archive_write_set_format(a.get(), Format), a.get());
  if (Format == ARCHIVE_FORMAT_RAW)
    checked(archive_write_add_filter_gzip(a.get()), a.get());
  checked(archive_write_open_fd(a.get(), out.handle()), a.get());
  std::unique_ptr<archive_entry, decltype(&archive_entry_free)> e(
      archive_entry_new(), archive_entry_free);
  if (!e)
    throw std::bad_alloc();
  archive_entry_set_pathname(e.get(),
                             QFileInfo(src).fileName().toUtf8().constData());
  archive_entry_set_filetype(e.get(), AE_IFREG);
  archive_entry_set_perm(e.get(), 0644);
  archive_entry_set_size(e.get(), src.size());
  checked(archive_write_header(a.get(), e.get()), a.get());
  std::array<char, 32768> buf;
  qint64 n;
  while ((n = src.read(buf.data(), buf.size())) > 0)
    if (archive_write_data(a.get(), buf.data(), n) != n)
      throw std::runtime_error("Archive write failed");
  if (n < 0)
    throw std::runtime_error("Read failed");
  checked(archive_write_close(a.get()), a.get());
  if (!out.commit())
    throw std::runtime_error("Commit failed");
}
} // namespace bricks::archives
