#include "unllm/symbolic/process.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char **environ;

namespace unllm::native {
static void close_fd(int &fd) {
  if (fd >= 0)
    close(fd);
  fd = -1;
}
Process::Process(const std::vector<std::string> &argv, const std::string &cwd,
                 std::size_t limit)
    : limit_(limit) {
  if (argv.empty() || argv[0].empty())
    throw std::runtime_error("Missing executable");
  for (auto &a : argv)
    if (a.find('\0') != std::string::npos)
      throw Unsupported("NUL in process argument");
  static std::once_flag sigpipe;
  std::call_once(sigpipe, [] { signal(SIGPIPE, SIG_IGN); });
  int pipes[6] = {-1, -1, -1, -1, -1, -1};
  for (int i = 0; i < 6; i += 2)
    if (pipe2(pipes + i, O_CLOEXEC) < 0) {
      for (int &fd : pipes)
        close_fd(fd);
      throw std::runtime_error("Cannot create process pipes");
    }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipes[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipes[3], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipes[5], STDERR_FILENO);
  for (int fd : pipes)
    posix_spawn_file_actions_addclose(&actions, fd);
  if (!cwd.empty())
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
  posix_spawnattr_t attrs;
  posix_spawnattr_init(&attrs);
  posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attrs, 0);
  std::vector<char *> args;
  for (auto &a : argv)
    args.push_back(const_cast<char *>(a.c_str()));
  args.push_back(nullptr);
  int rc =
      posix_spawnp(&child_, args[0], &actions, &attrs, args.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attrs);
  if (rc) {
    child_ = -1;
    for (int &fd : pipes)
      close_fd(fd);
    throw std::runtime_error("Cannot start " + argv[0] + ": " +
                             std::strerror(rc));
  }
  input_ = pipes[1];
  output_ = pipes[2];
  error_ = pipes[4];
  close(pipes[0]);
  close(pipes[3]);
  close(pipes[5]);
  for (int fd : {input_, output_, error_})
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}
Process::~Process() { stop(); }
void Process::stop() noexcept {
  close_fd(input_);
  close_fd(output_);
  close_fd(error_);
  if (child_ > 0) {
    kill(-child_, SIGTERM);
    for (int i = 0; i < 20; ++i) {
      int rc = waitpid(child_, nullptr, WNOHANG);
      if (rc == child_ || (rc < 0 && errno == ECHILD)) {
        kill(-child_, SIGKILL);
        child_ = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    kill(-child_, SIGKILL);
    while (waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {
    }
    child_ = -1;
  }
}
void Process::drain(int &fd, std::string &into) {
  if (fd < 0)
    return;
  char buffer[8192];
  while (true) {
    auto n = read(fd, buffer, sizeof buffer);
    if (n > 0) {
      received_ += std::size_t(n);
      if (received_ > limit_) {
        stop();
        throw ResourceLimit("Child output budget exceeded");
      }
      into.append(buffer, n);
    } else if (n == 0) {
      close_fd(fd);
      return;
    } else if (errno == EINTR)
      continue;
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;
    else {
      stop();
      throw std::runtime_error("Child output read failed");
    }
  }
}
void Process::reset_output_budget() {
  errors_.clear();
  received_ = pending_.size();
}
void Process::pump(Deadline deadline, bool writable) {
  auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now())
                    .count();
  if (remain <= 0) {
    stop();
    throw ResourceLimit("Child process deadline exceeded");
  }
  pollfd fds[] = {{output_, POLLIN, 0},
                  {error_, POLLIN, 0},
                  {writable ? input_ : -1, POLLOUT, 0}};
  int ready = poll(fds, 3, int(std::min<int64_t>(remain, 1000)));
  if (ready < 0 && errno != EINTR) {
    stop();
    throw std::runtime_error("Child poll failed");
  }
  if (ready > 0) {
    if (fds[0].revents)
      drain(output_, pending_);
    if (fds[1].revents)
      drain(error_, errors_);
  }
}
void Process::write(const std::string &data, Deadline deadline) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    if (input_ < 0)
      throw std::runtime_error("Child input is closed");
    auto n = ::write(input_, data.data() + offset, data.size() - offset);
    if (n > 0)
      offset += n;
    else if (n < 0 && errno == EINTR)
      continue;
    else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      pump(deadline, true);
    else {
      stop();
      throw std::runtime_error("Child closed its input");
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      stop();
      throw ResourceLimit("Child input deadline exceeded");
    }
  }
}
std::string Process::line(Deadline deadline) {
  while (true) {
    auto end = pending_.find('\n');
    if (end != std::string::npos) {
      auto result = pending_.substr(0, end);
      pending_.erase(0, end + 1);
      if (!result.empty() && result.back() == '\r')
        result.pop_back();
      return result;
    }
    if (output_ < 0)
      throw std::runtime_error("Child exited before response completed: " +
                               errors_);
    pump(deadline);
  }
}
ProcessResult Process::finish(Deadline deadline) {
  close_fd(input_);
  while (output_ >= 0 || error_ >= 0)
    pump(deadline);
  int status = 0;
  while (waitpid(child_, &status, WNOHANG) == 0) {
    if (std::chrono::steady_clock::now() >= deadline) {
      stop();
      throw ResourceLimit("Child exit deadline exceeded");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  kill(-child_, SIGKILL);
  child_ = -1;
  return {WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status),
          std::exchange(pending_, {}), std::exchange(errors_, {})};
}
} // namespace unllm::native
