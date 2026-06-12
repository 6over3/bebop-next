#include "bebopc_dir.h"

#include <stdio.h>

#include "bebopc_utils.h"

#ifdef BEBOPC_WINDOWS
#include <sys/stat.h>
#define stat _stat64
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

void _bebopc_get_ext(bebopc_file_t* file)
{
  bebopc_char_t* period = bebopc_strrchr(file->name, BEBOPC_STRING('.'));
  if (period == NULL) {
    file->extension = &(file->name[bebopc_strlen(file->name)]);
  } else {
    file->extension = period + 1;
  }
}

int BEBOPC_CDECL _bebopc_file_cmp(const void* a, const void* b)
{
  const bebopc_file_t* fa = (const bebopc_file_t*)a;
  const bebopc_file_t* fb = (const bebopc_file_t*)b;
  if (fa->is_dir != fb->is_dir) {
    return -(fa->is_dir - fb->is_dir);
  }
  return bebopc_strncmp(fa->name, fb->name, BEBOPC_FILENAME_MAX);
}

bebopc_error_code_t bebopc_dir_open(bebopc_dir_t* dir, bebopc_ctx_t* ctx, const bebopc_char_t* path)
{
#ifdef BEBOPC_WINDOWS
  bebopc_char_t path_buf[BEBOPC_PATH_MAX];
#endif
  bebopc_char_t* pathp;
  bebopc_error_code_t err = BEBOPC_ERR_IO;

  if (dir == NULL || path == NULL || bebopc_strlen(path) == 0) {
    errno = EINVAL;
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_INVALID_ARG, "invalid directory path");
    }
    return BEBOPC_ERR_INVALID_ARG;
  }
  if (bebopc_strlen(path) + BEBOPC_PATH_EXTRA >= BEBOPC_PATH_MAX) {
    errno = ENAMETOOLONG;
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_INVALID_ARG, "path too long");
    }
    return BEBOPC_ERR_INVALID_ARG;
  }

  dir->ctx = ctx;
  dir->_files = NULL;
#ifdef BEBOPC_WINDOWS
  dir->_h = INVALID_HANDLE_VALUE;
#else
  dir->_d = NULL;
#endif
  bebopc_dir_close(dir);

  bebopc_strcpy(dir->path, path);

  pathp = &dir->path[bebopc_strlen(dir->path) - 1];
  while (pathp != dir->path && (*pathp == BEBOPC_STRING('\\') || *pathp == BEBOPC_STRING('/'))) {
    *pathp = BEBOPC_STRING('\0');
    pathp--;
  }

#ifdef BEBOPC_WINDOWS
  bebopc_strcpy(path_buf, dir->path);
  bebopc_strcat(path_buf, BEBOPC_STRING("\\*"));
#if (defined WINAPI_FAMILY) && (WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP)
  dir->_h = FindFirstFileEx(path_buf, FindExInfoStandard, &dir->_f, FindExSearchNameMatch, NULL, 0);
#else
  dir->_h = FindFirstFile(path_buf, &dir->_f);
#endif
  if (dir->_h == INVALID_HANDLE_VALUE) {
    errno = ENOENT;
    err = BEBOPC_ERR_NOT_FOUND;
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_NOT_FOUND, "directory not found");
    }
#else
  dir->_d = bebopc_opendir(path);
  if (dir->_d == NULL) {
    if (errno == ENOENT) {
      err = BEBOPC_ERR_NOT_FOUND;
      if (ctx) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_NOT_FOUND, "directory not found");
      }
    } else if (errno == EACCES) {
      err = BEBOPC_ERR_PERMISSION;
      if (ctx) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_PERMISSION, "permission denied");
      }
    } else {
      err = BEBOPC_ERR_IO;
      if (ctx) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "failed to open directory");
      }
    }
#endif
    goto bail;
  }

  dir->has_next = true;
#ifndef BEBOPC_WINDOWS
  dir->_e = bebopc_readdir(dir->_d);
  if (dir->_e == NULL) {
    dir->has_next = false;
  }
#endif

  return BEBOPC_OK;

bail:
  bebopc_dir_close(dir);
  return err;
}

bebopc_error_code_t bebopc_dir_open_sorted(
    bebopc_dir_t* dir, bebopc_ctx_t* ctx, const bebopc_char_t* path
)
{
  size_t n_files = 0;
  bebopc_error_code_t err;

  err = bebopc_dir_open(dir, ctx, path);
  if (err != BEBOPC_OK) {
    return err;
  }

  while (dir->has_next) {
    n_files++;
    err = bebopc_dir_next(dir);
    if (err != BEBOPC_OK) {
      goto bail;
    }
  }
  bebopc_dir_close(dir);

  if (n_files == 0) {
    return BEBOPC_OK;
  }

  err = bebopc_dir_open(dir, ctx, path);
  if (err != BEBOPC_OK) {
    return err;
  }

  dir->n_files = 0;
  dir->_files = (bebopc_file_t*)malloc(sizeof(bebopc_file_t) * n_files);
  if (dir->_files == NULL) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate file list");
    }
    err = BEBOPC_ERR_OUT_OF_MEMORY;
    goto bail;
  }

  while (dir->has_next) {
    bebopc_file_t* p_file;
    dir->n_files++;

    p_file = &dir->_files[dir->n_files - 1];
    err = bebopc_dir_readfile(dir, p_file);
    if (err != BEBOPC_OK) {
      goto bail;
    }

    err = bebopc_dir_next(dir);
    if (err != BEBOPC_OK) {
      goto bail;
    }

    if (dir->n_files == n_files) {
      break;
    }
  }

  qsort(dir->_files, dir->n_files, sizeof(bebopc_file_t), _bebopc_file_cmp);

  return BEBOPC_OK;

bail:
  bebopc_dir_close(dir);
  return err;
}

void bebopc_dir_close(bebopc_dir_t* dir)
{
  if (dir == NULL) {
    return;
  }

  memset(dir->path, 0, sizeof(dir->path));
  dir->has_next = false;
  dir->n_files = 0;

  free(dir->_files);
  dir->_files = NULL;

#ifdef BEBOPC_WINDOWS
  if (dir->_h != INVALID_HANDLE_VALUE) {
    FindClose(dir->_h);
  }
  dir->_h = INVALID_HANDLE_VALUE;
#else
  if (dir->_d) {
    bebopc_closedir(dir->_d);
  }
  dir->_d = NULL;
  dir->_e = NULL;
#endif
}

bebopc_error_code_t bebopc_dir_next(bebopc_dir_t* dir)
{
  if (dir == NULL) {
    errno = EINVAL;
    return BEBOPC_ERR_INVALID_ARG;
  }
  if (!dir->has_next) {
    errno = ENOENT;
    return BEBOPC_ERR_NOT_FOUND;
  }

#ifdef BEBOPC_WINDOWS
  if (FindNextFile(dir->_h, &dir->_f) == 0)
#else
  dir->_e = bebopc_readdir(dir->_d);
  if (dir->_e == NULL)
#endif
  {
    dir->has_next = false;
#ifdef BEBOPC_WINDOWS
    if (GetLastError() != ERROR_SUCCESS && GetLastError() != ERROR_NO_MORE_FILES) {
      bebopc_dir_close(dir);
      errno = EIO;
      return BEBOPC_ERR_IO;
    }
#endif
  }

  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_dir_readfile(const bebopc_dir_t* dir, bebopc_file_t* file)
{
  const bebopc_char_t* filename;

  if (dir == NULL || file == NULL) {
    errno = EINVAL;
    return BEBOPC_ERR_INVALID_ARG;
  }
#ifdef BEBOPC_WINDOWS
  if (dir->_h == INVALID_HANDLE_VALUE)
#else
  if (dir->_e == NULL)
#endif
  {
    errno = ENOENT;
    return BEBOPC_ERR_NOT_FOUND;
  }

#ifdef BEBOPC_WINDOWS
  filename = dir->_f.cFileName;
#else
  filename = dir->_e->d_name;
#endif

  if (bebopc_strlen(dir->path) + bebopc_strlen(filename) + 1 + BEBOPC_PATH_EXTRA >= BEBOPC_PATH_MAX)
  {
    errno = ENAMETOOLONG;
    return BEBOPC_ERR_INVALID_ARG;
  }
  if (bebopc_strlen(filename) >= BEBOPC_FILENAME_MAX) {
    errno = ENAMETOOLONG;
    return BEBOPC_ERR_INVALID_ARG;
  }

  bebopc_strcpy(file->path, dir->path);
  if (bebopc_strcmp(dir->path, BEBOPC_STRING("/")) != 0) {
    bebopc_strcat(file->path, BEBOPC_STRING("/"));
  }
  bebopc_strcpy(file->name, filename);
  bebopc_strcat(file->path, filename);

#ifndef BEBOPC_WINDOWS
#ifdef __MINGW32__
  if (_tstat(file->path, &file->_s) == -1)
#else
  // lstat unconditionally: symlinked directories must not report is_dir, or
  // the glob walk follows symlink loops.
  if (lstat(file->path, &file->_s) == -1)
#endif
  {
    if (dir->ctx) {
      BEBOPC_ERROR(&dir->ctx->errors, BEBOPC_ERR_IO, "stat failed: %s", file->path);
    }
    return BEBOPC_ERR_IO;
  }
#endif

  _bebopc_get_ext(file);

#ifdef BEBOPC_WINDOWS
  file->is_dir = !!(dir->_f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
  file->is_reg = !!(dir->_f.dwFileAttributes & FILE_ATTRIBUTE_NORMAL)
      || (!(dir->_f.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)
          && !(dir->_f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
#ifdef FILE_ATTRIBUTE_INTEGRITY_STREAM
          !(dir->_f.dwFileAttributes & FILE_ATTRIBUTE_INTEGRITY_STREAM) &&
#endif
#ifdef FILE_ATTRIBUTE_NO_SCRUB_DATA
          !(dir->_f.dwFileAttributes & FILE_ATTRIBUTE_NO_SCRUB_DATA) &&
#endif
          !(dir->_f.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE)
          && !(dir->_f.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY));
#else
  file->is_dir = S_ISDIR(file->_s.st_mode);
  file->is_reg = S_ISREG(file->_s.st_mode);
#endif

  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_dir_readfile_n(const bebopc_dir_t* dir, bebopc_file_t* file, size_t i)
{
  if (dir == NULL || file == NULL) {
    errno = EINVAL;
    return BEBOPC_ERR_INVALID_ARG;
  }
  if (i >= dir->n_files) {
    errno = ENOENT;
    return BEBOPC_ERR_NOT_FOUND;
  }

  memcpy(file, &dir->_files[i], sizeof(bebopc_file_t));
  _bebopc_get_ext(file);

  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_dir_open_subdir_n(bebopc_dir_t* dir, size_t i)
{
  bebopc_char_t path[BEBOPC_PATH_MAX];
  bebopc_ctx_t* ctx;
  bebopc_error_code_t err;

  if (dir == NULL) {
    errno = EINVAL;
    return BEBOPC_ERR_INVALID_ARG;
  }
  if (i >= dir->n_files || !dir->_files[i].is_dir) {
    errno = ENOENT;
    return BEBOPC_ERR_NOT_FOUND;
  }

  bebopc_strcpy(path, dir->_files[i].path);
  ctx = dir->ctx;
  bebopc_dir_close(dir);

  err = bebopc_dir_open_sorted(dir, ctx, path);
  if (err != BEBOPC_OK) {
    return err;
  }

  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_file_open(
    bebopc_file_t* file, bebopc_ctx_t* ctx, const bebopc_char_t* path
)
{
  bebopc_dir_t dir;
  bebopc_error_code_t result = BEBOPC_OK;
  bool found = false;
  bebopc_char_t dir_name_buf[BEBOPC_PATH_MAX];
  bebopc_char_t file_name_buf[BEBOPC_PATH_MAX];
  bebopc_char_t* dir_name;
  bebopc_char_t* base_name;
#ifdef BEBOPC_WINDOWS
  bebopc_char_t drive_buf[BEBOPC_PATH_MAX];
  bebopc_char_t ext_buf[BEBOPC_FILENAME_MAX];
#endif

  if (file == NULL || path == NULL || bebopc_strlen(path) == 0) {
    errno = EINVAL;
    return BEBOPC_ERR_INVALID_ARG;
  }
  if (bebopc_strlen(path) + BEBOPC_PATH_EXTRA >= BEBOPC_PATH_MAX) {
    errno = ENAMETOOLONG;
    return BEBOPC_ERR_INVALID_ARG;
  }

#ifdef BEBOPC_WINDOWS
#if ((defined _MSC_VER) && (_MSC_VER >= 1400))
  errno = _tsplitpath_s(
      path,
      drive_buf,
      BEBOPC_DRIVE_MAX,
      dir_name_buf,
      BEBOPC_FILENAME_MAX,
      file_name_buf,
      BEBOPC_FILENAME_MAX,
      ext_buf,
      BEBOPC_FILENAME_MAX
  );
#else
  _tsplitpath(path, drive_buf, dir_name_buf, file_name_buf, ext_buf);
#endif
  if (errno) {
    return BEBOPC_ERR_IO;
  }

#ifdef _UNICODE
  if (drive_buf[0] == L'\xFEFE') {
    drive_buf[0] = L'\0';
  }
  if (dir_name_buf[0] == L'\xFEFE') {
    dir_name_buf[0] = L'\0';
  }
#endif

  if (drive_buf[0] == BEBOPC_STRING('\0') && dir_name_buf[0] == BEBOPC_STRING('\0')) {
    bebopc_strcpy(dir_name_buf, BEBOPC_STRING("."));
  }
  bebopc_strcat(drive_buf, dir_name_buf);
  dir_name = drive_buf;
  bebopc_strcat(file_name_buf, ext_buf);
  base_name = file_name_buf;
#else
  bebopc_strcpy(dir_name_buf, path);
  dir_name = dirname(dir_name_buf);
  bebopc_strcpy(file_name_buf, path);
  base_name = basename(file_name_buf);
#endif

#ifdef BEBOPC_WINDOWS
  if (bebopc_strlen(base_name) == 0)
#else
  if (bebopc_strcmp(base_name, BEBOPC_STRING("/")) == 0)
#endif
  {
    memset(file, 0, sizeof(*file));
    file->is_dir = true;
    file->is_reg = false;
    bebopc_strcpy(file->path, dir_name);
    file->extension = file->path + bebopc_strlen(file->path);
    return BEBOPC_OK;
  }

  result = bebopc_dir_open(&dir, ctx, dir_name);
  if (result != BEBOPC_OK) {
    return result;
  }

  while (dir.has_next) {
    result = bebopc_dir_readfile(&dir, file);
    if (result != BEBOPC_OK) {
      goto bail;
    }
    if (bebopc_strcmp(file->name, base_name) == 0) {
      found = true;
      break;
    }
    bebopc_dir_next(&dir);
  }

  if (!found) {
    result = BEBOPC_ERR_NOT_FOUND;
    errno = ENOENT;
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_NOT_FOUND, "file not found");
    }
  }

bail:
  bebopc_dir_close(&dir);
  return result;
}

bool bebopc_path_is_absolute(const bebopc_char_t* path)
{
  if (!path || !*path) {
    return false;
  }
#ifdef BEBOPC_WINDOWS
  if (path[0] == BEBOPC_STRING('\\') || path[0] == BEBOPC_STRING('/')) {
    return true;
  }
  if (((path[0] >= BEBOPC_STRING('A') && path[0] <= BEBOPC_STRING('Z'))
       || (path[0] >= BEBOPC_STRING('a') && path[0] <= BEBOPC_STRING('z')))
      && path[1] == BEBOPC_STRING(':'))
  {
    return true;
  }
  return false;
#else
  return path[0] == '/';
#endif
}

bool bebopc_file_exists(const bebopc_char_t* path)
{
  if (!path) {
    return false;
  }
#ifdef BEBOPC_WINDOWS
#ifdef _UNICODE
  return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
#else
  return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#endif
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

bool bebopc_file_is_dir(const bebopc_char_t* path)
{
  if (!path) {
    return false;
  }
#ifdef BEBOPC_WINDOWS
#ifdef _UNICODE
  DWORD attr = GetFileAttributesW(path);
#else
  DWORD attr = GetFileAttributesA(path);
#endif
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  if (stat(path, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
#endif
}

bool bebopc_file_is_file(const bebopc_char_t* path)
{
  if (!path) {
    return false;
  }
#ifdef BEBOPC_WINDOWS
#ifdef _UNICODE
  DWORD attr = GetFileAttributesW(path);
#else
  DWORD attr = GetFileAttributesA(path);
#endif
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  if (stat(path, &st) != 0) {
    return false;
  }
  return S_ISREG(st.st_mode);
#endif
}

static const char* _bebopc_path_ext(const char* name)
{
  const char* dot = NULL;
  for (const char* p = name; *p; p++) {
    if (*p == '.') {
      dot = p;
    }
  }
  return dot ? dot + 1 : name + strlen(name);
}

char* bebopc_path_join(const char* dir, const char* file)
{
  if (!dir || !file) {
    return NULL;
  }

  size_t dir_len = strlen(dir);
  size_t file_len = strlen(file);

  while (dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\')) {
    dir_len--;
  }

  size_t total = dir_len + 1 + file_len + 1;
  char* result = (char*)malloc(total);
  if (!result) {
    return NULL;
  }

  memcpy(result, dir, dir_len);
#ifdef BEBOPC_WINDOWS
  result[dir_len] = '\\';
#else
  result[dir_len] = '/';
#endif
  memcpy(result + dir_len + 1, file, file_len);
  result[dir_len + 1 + file_len] = '\0';

  return result;
}

char* bebopc_path_dirname(const char* path)
{
  if (!path) {
    return NULL;
  }

  size_t len = strlen(path);
  if (len == 0) {
    return bebopc_strdup(".");
  }

  while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
    len--;
  }
  while (len > 0 && path[len - 1] != '/' && path[len - 1] != '\\') {
    len--;
  }
  if (len == 0) {
    return bebopc_strdup(".");
  }
  while (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
    len--;
  }

  return bebopc_strndup(path, len);
}

const char* bebopc_path_basename(const char* path)
{
  if (!path) {
    return NULL;
  }
  const char* last = path;
  for (const char* p = path; *p; p++) {
    if (*p == '/' || *p == '\\') {
      last = p + 1;
    }
  }
  return last;
}

const char* bebopc_path_extension(const char* path)
{
  const char* name = bebopc_path_basename(path);
  if (!name) {
    return NULL;
  }
  return _bebopc_path_ext(name);
}

char* bebopc_path_normalize(const char* path)
{
  if (!path) {
    return NULL;
  }

  size_t len = strlen(path);
  char* result = (char*)malloc(len + 1);
  if (!result) {
    return NULL;
  }

  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    char c = path[i];
#ifdef BEBOPC_WINDOWS
    if (c == '/') {
      c = '\\';
    }
#else
    if (c == '\\') {
      c = '/';
    }
#endif
    if ((c == '/' || c == '\\') && j > 0 && (result[j - 1] == '/' || result[j - 1] == '\\')) {
      continue;
    }
    result[j++] = c;
  }

  if (j > 1 && (result[j - 1] == '/' || result[j - 1] == '\\')) {
    j--;
  }

  result[j] = '\0';
  return result;
}

char* bebopc_path_realpath(const char* path)
{
  if (!path) {
    return NULL;
  }

#ifdef BEBOPC_WINDOWS
  char buf[BEBOPC_PATH_MAX];
  DWORD len = GetFullPathNameA(path, BEBOPC_PATH_MAX, buf, NULL);
  if (len == 0 || len >= BEBOPC_PATH_MAX) {
    return NULL;
  }
  return bebopc_strdup(buf);
#else
  return realpath(path, NULL);
#endif
}

char* bebopc_path_absolute(bebopc_ctx_t* ctx, const char* path)
{
  if (!path) {
    return NULL;
  }

#ifdef BEBOPC_WINDOWS
  char buf[BEBOPC_PATH_MAX];
  DWORD len = GetFullPathNameA(path, BEBOPC_PATH_MAX, buf, NULL);
  if (len == 0 || len >= BEBOPC_PATH_MAX) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "failed to get absolute path");
    }
    return NULL;
  }
  return bebopc_strndup(buf, len);
#else
  char* resolved = realpath(path, NULL);
  if (!resolved) {
    if (ctx) {
      if (errno == ENOENT) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_NOT_FOUND, "path not found");
      } else {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "failed to resolve path");
      }
    }
    return NULL;
  }
  return resolved;
#endif
}

char* bebopc_path_relative(const char* base, const char* path)
{
  if (!base || !path) {
    return NULL;
  }

  size_t base_len = bebopc_strlen(base);
  size_t path_len = bebopc_strlen(path);

  // Trailing separators on base don't count toward the boundary check.
  while (base_len > 0 && (base[base_len - 1] == '/' || base[base_len - 1] == '\\')) {
    base_len--;
  }

  if (path_len < base_len || bebopc_strncmp(path, base, base_len) != 0) {
    return bebopc_strdup(path);
  }

  // Require a separator (or exact match) at the boundary so base "/foo"
  // does not claim "/foobar".
  if (path[base_len] != '\0' && path[base_len] != '/' && path[base_len] != '\\') {
    return bebopc_strdup(path);
  }

  const char* rel = path + base_len;
  while (*rel == '/' || *rel == '\\') {
    rel++;
  }

  return bebopc_strdup(rel);
}

bool bebopc_path_is_hidden(const char* path)
{
  if (!path) {
    return false;
  }

  const char* name = bebopc_path_basename(path);
  if (name && name[0] == '.') {
    return true;
  }

#ifdef BEBOPC_WINDOWS
  DWORD attrs = GetFileAttributesA(path);
  if (attrs != INVALID_FILE_ATTRIBUTES) {
    return (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
  }
#elif defined(BEBOPC_MACOS)
  struct stat st;
  if (stat(path, &st) == 0) {
    return (st.st_flags & UF_HIDDEN) != 0;
  }
#endif

  return false;
}

bool bebopc_path_has_separator(const char* path)
{
  if (!path) {
    return false;
  }
  return bebopc_strchr(path, '/') != NULL || bebopc_strchr(path, '\\') != NULL;
}

char* bebopc_file_read(bebopc_ctx_t* ctx, const char* path, size_t* out_size)
{
  if (!path || !out_size) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_INVALID_ARG, "invalid arguments");
    }
    return NULL;
  }

  FILE* f = fopen(path, "rb");
  if (!f) {
    if (ctx) {
      if (errno == ENOENT) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_NOT_FOUND, "file not found: %s", path);
      } else if (errno == EACCES) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_PERMISSION, "permission denied: %s", path);
      } else {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "failed to open file: %s", path);
      }
    }
    return NULL;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "seek failed: %s", path);
    }
    return NULL;
  }

  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "ftell failed: %s", path);
    }
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "seek failed: %s", path);
    }
    return NULL;
  }

  char* buf = (char*)malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "allocation failed for file: %s", path);
    }
    return NULL;
  }

  size_t nread = fread(buf, 1, (size_t)size, f);
  fclose(f);

  if (nread != (size_t)size) {
    free(buf);
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "read error: %s", path);
    }
    return NULL;
  }

  buf[size] = '\0';
  *out_size = (size_t)size;
  return buf;
}

bool bebopc_file_write(bebopc_ctx_t* ctx, const char* path, const char* data, size_t size)
{
  if (!path || !data) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_INVALID_ARG, "invalid arguments");
    }
    return false;
  }

  char* dir = bebopc_path_dirname(path);
  if (dir && strcmp(dir, ".") != 0) {
    bebopc_mkdir(ctx, dir);
    free(dir);
  } else {
    free(dir);
  }

  FILE* f = fopen(path, "wb");
  if (!f) {
    if (ctx) {
      if (errno == EACCES) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_PERMISSION, "permission denied: %s", path);
      } else {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "failed to create file: %s", path);
      }
    }
    return false;
  }

  size_t written = fwrite(data, 1, size, f);
  fclose(f);

  if (written != size) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "write error: %s", path);
    }
    return false;
  }

  return true;
}

bool bebopc_mkdir(bebopc_ctx_t* ctx, const char* path)
{
  if (!path || !*path) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_INVALID_ARG, "invalid path");
    }
    return false;
  }

  if (bebopc_file_is_dir(path)) {
    return true;
  }

  char* stack[BEBOPC_PATH_MAX / 2];
  int top = 0;

  char* current = bebopc_strdup(path);
  if (!current) {
    return false;
  }

  while (current && *current && !bebopc_file_is_dir(current)) {
    stack[top++] = current;
    char* parent = bebopc_path_dirname(current);
    if (!parent || bebopc_streq(parent, current) || bebopc_streq(parent, ".")) {
      free(parent);
      break;
    }
    current = parent;
  }

  bool success = true;
  while (top > 0) {
    char* dir = stack[--top];
#ifdef BEBOPC_WINDOWS
    if (_mkdir(dir) != 0 && errno != EEXIST) {
#else
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
#endif
      if (ctx) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_IO, "failed to create directory: %s", dir);
      }
      success = false;
    }
    free(dir);
    if (!success) {
      break;
    }
  }

  while (top > 0) {
    free(stack[--top]);
  }

  return success;
}

char* bebopc_getcwd(void)
{
  char buf[BEBOPC_PATH_MAX];
#ifdef BEBOPC_WINDOWS
  if (_getcwd(buf, BEBOPC_PATH_MAX) == NULL) {
#else
  if (getcwd(buf, BEBOPC_PATH_MAX) == NULL) {
#endif
    return NULL;
  }
  return bebopc_strdup(buf);
}
