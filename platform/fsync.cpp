#include "platform/fsync.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace htj2k::platform {
namespace {

[[noreturn]] void throw_system_error(const std::string& prefix)
{
  throw std::runtime_error(prefix + ": " + std::strerror(errno));
}

}  // namespace

void fsync_file(const std::filesystem::path& path)
{
#if defined(_WIN32)
  (void)path;
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    throw_system_error("open failed for fsync_file");
  }
  const int rc = ::fsync(fd);
  const int saved_errno = errno;
  ::close(fd);
  if (rc != 0) {
    errno = saved_errno;
    throw_system_error("fsync failed for file");
  }
#endif
}

void fsync_directory(const std::filesystem::path& path)
{
#if defined(_WIN32)
  (void)path;
#else
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) {
    throw_system_error("open failed for fsync_directory");
  }
  const int rc = ::fsync(fd);
  const int saved_errno = errno;
  ::close(fd);
  if (rc != 0) {
    errno = saved_errno;
    throw_system_error("fsync failed for directory");
  }
#endif
}

}  // namespace htj2k::platform
