#if defined(__linux__) || defined(__ANDROID__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#if defined(__APPLE__)
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include "bebopc_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BEBOPC_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <bcrypt.h>
#include <windows.h>
#pragma comment(lib, "bcrypt.lib")
#include <direct.h>
#include <io.h>
#include <profileapi.h>
#include <time.h>
#elif defined(BEBOPC_LINUX)
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/random.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#elif defined(BEBOPC_MACOS)
#include <Security/SecRandom.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <mach/mach_time.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#elif defined(BEBOPC_BSD)
#include <limits.h>
#include <sys/random.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

char* bebopc_strdup(const char* s)
{
  if (s == NULL) {
#ifdef EINVAL
    errno = EINVAL;
#endif
    return NULL;
  }

#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
  return strdup(s);
#elif defined(_WIN32)
  return _strdup(s);
#else
  size_t siz = strlen(s) + 1;
  char* y = malloc(siz);
  if (y != NULL) {
    memcpy(y, s, siz);
  } else {
#ifdef ENOMEM
    errno = ENOMEM;
#endif
  }
  return y;
#endif
}

char* bebopc_strndup(const char* s, size_t n)
{
  if (s == NULL) {
#ifdef EINVAL
    errno = EINVAL;
#endif
    return NULL;
  }

#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
  return strndup(s, n);
#else
  size_t length = 0;
  while (length < n && s[length] != '\0') {
    length++;
  }

  char* y = malloc(length + 1);
  if (y != NULL) {
    memcpy(y, s, length);
    y[length] = '\0';
  } else {
#ifdef ENOMEM
    errno = ENOMEM;
#endif
  }
  return y;
#endif
}

size_t bebopc_strncpy(char* dest, const char* src, size_t dest_size)
{
  if (!dest || dest_size == 0) {
#ifdef EINVAL
    errno = EINVAL;
#endif
    return 0;
  }
  if (!src) {
    dest[0] = '\0';
    return 0;
  }
#ifdef _WIN32
  errno_t err = strcpy_s(dest, dest_size, src);
  if (err != 0) {
    dest[0] = '\0';
    return 0;
  }
  return strlen(dest);
#else
  strncpy(dest, src, dest_size - 1);
  dest[dest_size - 1] = '\0';
  return strlen(dest);
#endif
}

bool bebopc_streq(const char* s1, const char* s2)
{
  if (s1 == s2) {
    return true;
  }
  if (s1 == NULL || s2 == NULL) {
    return false;
  }
  return strcmp(s1, s2) == 0;
}

bool bebopc_strcaseeq(const char* s1, const char* s2)
{
  if (s1 == s2) {
    return true;
  }
  if (s1 == NULL || s2 == NULL) {
    return false;
  }

#ifdef _WIN32
  return _stricmp(s1, s2) == 0;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  return strcasecmp(s1, s2) == 0;
#elif defined(__linux__) || defined(__ANDROID__)
  return strcasecmp(s1, s2) == 0;
#else
  const unsigned char* p1 = (const unsigned char*)s1;
  const unsigned char* p2 = (const unsigned char*)s2;

  while (*p1 && *p2) {
    unsigned char c1 = (*p1 >= 'A' && *p1 <= 'Z') ? (*p1 + 32) : *p1;
    unsigned char c2 = (*p2 >= 'A' && *p2 <= 'Z') ? (*p2 + 32) : *p2;

    if (c1 != c2) {
      return false;
    }
    p1++;
    p2++;
  }

  return *p1 == *p2;
#endif
}

void* bebopc_memdup(const void* src, size_t size)
{
  if (!src || size == 0) {
    return NULL;
  }
  void* copy = malloc(size);
  if (copy) {
    memcpy(copy, src, size);
  }
  return copy;
}

void bebopc_files_free(const char** files, uint32_t count)
{
  if (!files) {
    return;
  }
  for (uint32_t i = 0; i < count; i++) {
    free((char*)(uintptr_t)files[i]);
  }
  free((char**)(uintptr_t)files);
}

bool bebopc_random_bytes(void* buf, size_t size)
{
  if (!buf || size == 0) {
    return false;
  }

#ifdef _WIN32
  NTSTATUS status =
      BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  return BCRYPT_SUCCESS(status);

#elif defined(__linux__) || defined(__ANDROID__)
  ssize_t bytes_read = getrandom(buf, size, 0);
  return bytes_read == (ssize_t)size;

#elif defined(__APPLE__)
  OSStatus status = SecRandomCopyBytes(kSecRandomDefault, size, (uint8_t*)buf);
  return status == errSecSuccess;

#elif defined(__OpenBSD__)
  return getentropy(buf, size) == 0;

#elif defined(__FreeBSD__) || defined(__NetBSD__)
  arc4random_buf(buf, size);
  return true;

#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd == -1) {
    return false;
  }
  ssize_t bytes_read = read(fd, buf, size);
  close(fd);
  return bytes_read == (ssize_t)size;
#endif
}

bool bebopc_random_uint64(uint64_t* result)
{
  if (!result) {
    return false;
  }
  *result = 0;
  return bebopc_random_bytes(result, sizeof(*result));
}

bool bebopc_random_uint32(uint32_t* result)
{
  if (!result) {
    return false;
  }
  *result = 0;
  return bebopc_random_bytes(result, sizeof(*result));
}

uint64_t bebopc_timestamp_ms(void)
{
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  ticks -= 116444736000000000ULL;
  return ticks / 10000ULL;

#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__) || defined(__FreeBSD__) \
    || defined(__OpenBSD__) || defined(__NetBSD__)
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;

#else
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    return 0;
  }
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
#endif
}

uint64_t bebopc_timestamp_us(void)
{
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);

  uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  ticks -= 116444736000000000ULL;
  return ticks / 10ULL;

#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__) || defined(__FreeBSD__) \
    || defined(__OpenBSD__) || defined(__NetBSD__)
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;

#else
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    return 0;
  }
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
#endif
}

uint64_t bebopc_timestamp_ns(void)
{
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);

  uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  ticks -= 116444736000000000ULL;
  return ticks * 100ULL;

#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__) || defined(__FreeBSD__) \
    || defined(__OpenBSD__) || defined(__NetBSD__)
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

#else
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    return 0;
  }
  return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
#endif
}

uint64_t bebopc_monotonic_ns(void)
{
#ifdef _WIN32
  static LARGE_INTEGER frequency = {0};
  static bool freq_initialized = false;

  if (!freq_initialized) {
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0) {
      return 0;
    }
    freq_initialized = true;
  }

  LARGE_INTEGER counter;
  if (!QueryPerformanceCounter(&counter)) {
    return 0;
  }
  return (counter.QuadPart * 1000000000ULL) / frequency.QuadPart;

#elif defined(__APPLE__)
  static mach_timebase_info_data_t timebase = {0};
  static bool timebase_initialized = false;

  if (!timebase_initialized) {
    if (mach_timebase_info(&timebase) != KERN_SUCCESS) {
      return 0;
    }
    timebase_initialized = true;
  }

  uint64_t time = mach_absolute_time();
  return (time * timebase.numer) / timebase.denom;

#elif defined(__linux__) || defined(__ANDROID__) || defined(__FreeBSD__) || defined(__OpenBSD__) \
    || defined(__NetBSD__)
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

#else
  return bebopc_timestamp_ns();
#endif
}

uint64_t bebopc_monotonic_us(void)
{
  return bebopc_monotonic_ns() / 1000ULL;
}

uint64_t bebopc_monotonic_ms(void)
{
  return bebopc_monotonic_ns() / 1000000ULL;
}

uint64_t bebopc_elapsed_ns(uint64_t start_ns, uint64_t end_ns)
{
  if (end_ns >= start_ns) {
    return end_ns - start_ns;
  }
  return 0;
}

uint64_t bebopc_elapsed_us(uint64_t start_ns, uint64_t end_ns)
{
  return bebopc_elapsed_ns(start_ns, end_ns) / 1000ULL;
}

uint64_t bebopc_elapsed_ms(uint64_t start_ns, uint64_t end_ns)
{
  return bebopc_elapsed_ns(start_ns, end_ns) / 1000000ULL;
}

bool bebopc_format_timestamp(uint64_t timestamp_ms, char* buffer, size_t buffer_size)
{
  if (!buffer || buffer_size < 32) {
    return false;
  }

  time_t seconds = (time_t)(timestamp_ms / 1000);
  int milliseconds = (int)(timestamp_ms % 1000);

#ifdef _WIN32
  struct tm tm_info;
  if (gmtime_s(&tm_info, &seconds) != 0) {
    return false;
  }

  snprintf(
      buffer,
      buffer_size,
      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
      tm_info.tm_year + 1900,
      tm_info.tm_mon + 1,
      tm_info.tm_mday,
      tm_info.tm_hour,
      tm_info.tm_min,
      tm_info.tm_sec,
      milliseconds
  );
#else
  struct tm* tm_info = gmtime(&seconds);
  if (!tm_info) {
    return false;
  }

  snprintf(
      buffer,
      buffer_size,
      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
      tm_info->tm_year + 1900,
      tm_info->tm_mon + 1,
      tm_info->tm_mday,
      tm_info->tm_hour,
      tm_info->tm_min,
      tm_info->tm_sec,
      milliseconds
  );
#endif

  return true;
}

void bebopc_console_init(void)
{
#ifdef BEBOPC_WINDOWS
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h_out != INVALID_HANDLE_VALUE) {
    DWORD mode = 0;
    if (GetConsoleMode(h_out, &mode)) {
      SetConsoleMode(h_out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
  }

  HANDLE h_err = GetStdHandle(STD_ERROR_HANDLE);
  if (h_err != INVALID_HANDLE_VALUE) {
    DWORD mode = 0;
    if (GetConsoleMode(h_err, &mode)) {
      SetConsoleMode(h_err, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
  }
#endif
}

bool bebopc_color_supported(void)
{
  if (getenv("NO_COLOR")) {
    return false;
  }

  const char* force_color = getenv("FORCE_COLOR");
  if (force_color && (strcmp(force_color, "1") == 0 || strcmp(force_color, "true") == 0)) {
    return true;
  }

  const char* term = getenv("TERM");
  if (term && strcmp(term, "dumb") == 0) {
    return false;
  }

#ifdef BEBOPC_WINDOWS
  HANDLE h_err = GetStdHandle(STD_ERROR_HANDLE);
  DWORD mode;
  if (!GetConsoleMode(h_err, &mode)) {
    return false;
  }
#else
  if (!isatty(STDERR_FILENO)) {
    return false;
  }
  if (!term || !*term) {
    return false;
  }
#endif

  return true;
}

char* bebopc_exe_path(void)
{
#ifdef BEBOPC_WINDOWS
  char buf[MAX_PATH];
  DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return NULL;
  }
  return bebopc_strndup(buf, len);
#elif defined(BEBOPC_MACOS)
  char buf[PATH_MAX];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) {
    return NULL;
  }
  char* resolved = realpath(buf, NULL);
  return resolved;
#elif defined(BEBOPC_LINUX)
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) {
    return NULL;
  }
  buf[len] = '\0';
  return bebopc_strndup(buf, (size_t)len);
#elif defined(BEBOPC_BSD)
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
  char buf[PATH_MAX];
  size_t size = sizeof(buf);
  if (sysctl(mib, 4, buf, &size, NULL, 0) != 0) {
    return NULL;
  }
  return bebopc_strndup(buf, size);
#else
  return NULL;
#endif
}
