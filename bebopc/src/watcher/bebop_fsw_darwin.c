#include "bebop_fsw.h"

#if defined(BEBOP_FSW_MACOS)

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../bebopc_dir.h"
#include "../bebopc_utils.h"
#include "bebop_fsw_internal.h"

#define FSW_STREAM_FLAGS \
  (kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer \
   | kFSEventStreamCreateFlagUseCFTypes | kFSEventStreamCreateFlagUseExtendedData)

#define FSW_EVENT_BUFFER_SIZE 256

typedef struct {
  int wd;
  char* path;
  size_t path_len;
  bool recursive;
  FSEventStreamRef stream;
  dispatch_queue_t queue;
} _fsw_darwin_watch_t;

struct _fsw_platform {
  _fsw_darwin_watch_t* watches;
  size_t watch_count;
  size_t watch_capacity;
  int next_wd;

  _fsw_raw_event_t* events;
  size_t event_capacity;
  size_t event_count;
  size_t event_head;
  size_t event_tail;

  pthread_mutex_t mutex;
  dispatch_semaphore_t semaphore;
};

static bool _fsw_buffer_push(_fsw_platform_t* p, const _fsw_raw_event_t* event)
{
  if (p->event_count >= p->event_capacity) {
    return false;
  }
  p->events[p->event_tail] = *event;
  p->event_tail = (p->event_tail + 1) % p->event_capacity;
  p->event_count++;
  return true;
}

static bool _fsw_buffer_pop(_fsw_platform_t* p, _fsw_raw_event_t* event)
{
  if (p->event_count == 0) {
    return false;
  }
  *event = p->events[p->event_head];
  p->event_head = (p->event_head + 1) % p->event_capacity;
  p->event_count--;
  return true;
}

static _fsw_darwin_watch_t* _fsw_find_watch_by_stream(
    _fsw_platform_t* p, ConstFSEventStreamRef stream
)
{
  for (size_t i = 0; i < p->watch_count; i++) {
    if (p->watches[i].stream == stream) {
      return &p->watches[i];
    }
  }
  return NULL;
}

static void _fsw_fsevents_callback(
    ConstFSEventStreamRef streamRef,
    void* info,
    size_t numEvents,
    void* eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[]
)
{
  (void)eventIds;

  _fsw_platform_t* p = (_fsw_platform_t*)info;
  if (!p) {
    return;
  }

  CFArrayRef paths = (CFArrayRef)eventPaths;

  pthread_mutex_lock(&p->mutex);

  _fsw_darwin_watch_t* watch = _fsw_find_watch_by_stream(p, streamRef);
  if (!watch) {
    pthread_mutex_unlock(&p->mutex);
    return;
  }

  for (size_t i = 0; i < numEvents; i++) {
    FSEventStreamEventFlags flags = eventFlags[i];

    if (flags
        & (kFSEventStreamEventFlagEventIdsWrapped | kFSEventStreamEventFlagHistoryDone
           | kFSEventStreamEventFlagMount | kFSEventStreamEventFlagUnmount
           | kFSEventStreamEventFlagRootChanged))
    {
      continue;
    }

    if (flags
        & (kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped
           | kFSEventStreamEventFlagMustScanSubDirs))
    {
      _fsw_raw_event_t ev = {
          .platform_wd = watch->wd,
          .action = BEBOP_FSW_ACTION_MODIFY,
          .flags = BEBOP_FSW_FLAG_DIR,
          .path = bebopc_strdup(watch->path),
          .old_path = NULL,
          .inode = 0
      };
      if (_fsw_buffer_push(p, &ev)) {
        dispatch_semaphore_signal(p->semaphore);
      } else {
        free(ev.path);
      }
      continue;
    }

    CFDictionaryRef pathDict = (CFDictionaryRef)CFArrayGetValueAtIndex(paths, (CFIndex)i);
    CFStringRef cfPath =
        (CFStringRef)CFDictionaryGetValue(pathDict, kFSEventStreamEventExtendedDataPathKey);
    if (!cfPath) {
      continue;
    }

    char path_buf[BEBOPC_PATH_MAX];
    if (!CFStringGetCString(cfPath, path_buf, sizeof(path_buf), kCFStringEncodingUTF8)) {
      continue;
    }

    uint64_t inode = 0;
    CFNumberRef cfInode =
        (CFNumberRef)CFDictionaryGetValue(pathDict, kFSEventStreamEventExtendedFileIDKey);
    if (cfInode) {
      CFNumberGetValue(cfInode, kCFNumberSInt64Type, &inode);
    }

    if (!watch->recursive) {
      const char* rel = path_buf + watch->path_len;
      while (*rel == '/') {
        rel++;
      }
      if (bebopc_strchr(rel, '/') != NULL) {
        continue;
      }
    }

    bebop_fsw_flags_t ev_flags = BEBOP_FSW_FLAG_NONE;
    if (flags & kFSEventStreamEventFlagItemIsDir) {
      ev_flags |= BEBOP_FSW_FLAG_DIR;
    }

    bool file_exists = bebopc_file_exists(path_buf);

    if (flags & kFSEventStreamEventFlagItemRenamed) {
      bool paired = false;
      if (i + 1 < numEvents) {
        FSEventStreamEventFlags next_flags = eventFlags[i + 1];
        if (next_flags & kFSEventStreamEventFlagItemRenamed) {
          CFDictionaryRef nextDict =
              (CFDictionaryRef)CFArrayGetValueAtIndex(paths, (CFIndex)(i + 1));
          CFNumberRef nextInode =
              (CFNumberRef)CFDictionaryGetValue(nextDict, kFSEventStreamEventExtendedFileIDKey);
          if (nextInode) {
            uint64_t next_inode = 0;
            CFNumberGetValue(nextInode, kCFNumberSInt64Type, &next_inode);
            if (next_inode == inode && inode != 0) {
              CFStringRef nextPath = (CFStringRef)CFDictionaryGetValue(
                  nextDict, kFSEventStreamEventExtendedDataPathKey
              );
              char new_path_buf[BEBOPC_PATH_MAX];
              if (nextPath
                  && CFStringGetCString(
                      nextPath, new_path_buf, sizeof(new_path_buf), kCFStringEncodingUTF8
                  ))
              {
                paired = true;

                _fsw_raw_event_t ev = {
                    .platform_wd = watch->wd,
                    .action = BEBOP_FSW_ACTION_RENAME,
                    .flags = ev_flags,
                    .path = bebopc_strdup(new_path_buf),
                    .old_path = bebopc_strdup(path_buf),
                    .inode = inode
                };
                if (_fsw_buffer_push(p, &ev)) {
                  dispatch_semaphore_signal(p->semaphore);
                } else {
                  free(ev.path);
                  free(ev.old_path);
                }
                i++;
              }
            }
          }
        }
      }

      if (!paired) {
        _fsw_raw_event_t ev = {
            .platform_wd = watch->wd,
            .action = file_exists ? BEBOP_FSW_ACTION_ADD : BEBOP_FSW_ACTION_DELETE,
            .flags = ev_flags,
            .path = bebopc_strdup(path_buf),
            .old_path = NULL,
            .inode = inode
        };
        if (_fsw_buffer_push(p, &ev)) {
          dispatch_semaphore_signal(p->semaphore);
        } else {
          free(ev.path);
        }
      }
      continue;
    }

    if ((flags & kFSEventStreamEventFlagItemCreated) && file_exists) {
      _fsw_raw_event_t ev = {
          .platform_wd = watch->wd,
          .action = BEBOP_FSW_ACTION_ADD,
          .flags = ev_flags,
          .path = bebopc_strdup(path_buf),
          .old_path = NULL,
          .inode = inode
      };
      if (_fsw_buffer_push(p, &ev)) {
        dispatch_semaphore_signal(p->semaphore);
      } else {
        free(ev.path);
      }
    }

    if (flags
        & (kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemInodeMetaMod
           | kFSEventStreamEventFlagItemFinderInfoMod | kFSEventStreamEventFlagItemChangeOwner
           | kFSEventStreamEventFlagItemXattrMod))
    {
      _fsw_raw_event_t ev = {
          .platform_wd = watch->wd,
          .action = BEBOP_FSW_ACTION_MODIFY,
          .flags = ev_flags,
          .path = bebopc_strdup(path_buf),
          .old_path = NULL,
          .inode = inode
      };
      if (_fsw_buffer_push(p, &ev)) {
        dispatch_semaphore_signal(p->semaphore);
      } else {
        free(ev.path);
      }
    }

    if ((flags & kFSEventStreamEventFlagItemRemoved) && !file_exists) {
      _fsw_raw_event_t ev = {
          .platform_wd = watch->wd,
          .action = BEBOP_FSW_ACTION_DELETE,
          .flags = ev_flags,
          .path = bebopc_strdup(path_buf),
          .old_path = NULL,
          .inode = inode
      };
      if (_fsw_buffer_push(p, &ev)) {
        dispatch_semaphore_signal(p->semaphore);
      } else {
        free(ev.path);
      }
    }
  }

  pthread_mutex_unlock(&p->mutex);
}

_fsw_platform_t* _fsw_platform_create(void)
{
  _fsw_platform_t* p = calloc(1, sizeof(_fsw_platform_t));
  if (!p) {
    return NULL;
  }

  p->next_wd = 1;

  p->events = calloc(FSW_EVENT_BUFFER_SIZE, sizeof(_fsw_raw_event_t));
  if (!p->events) {
    free(p);
    return NULL;
  }
  p->event_capacity = FSW_EVENT_BUFFER_SIZE;

  if (pthread_mutex_init(&p->mutex, NULL) != 0) {
    free(p->events);
    free(p);
    return NULL;
  }

  p->semaphore = dispatch_semaphore_create(0);
  if (!p->semaphore) {
    pthread_mutex_destroy(&p->mutex);
    free(p->events);
    free(p);
    return NULL;
  }

  return p;
}

void _fsw_platform_destroy(_fsw_platform_t* p)
{
  if (!p) {
    return;
  }

  for (size_t i = 0; i < p->watch_count; i++) {
    _fsw_darwin_watch_t* w = &p->watches[i];
    if (w->stream) {
      FSEventStreamStop(w->stream);
      FSEventStreamInvalidate(w->stream);
      FSEventStreamRelease(w->stream);
    }
    if (w->queue) {
      dispatch_release(w->queue);
    }
    free(w->path);
  }
  free(p->watches);

  while (p->event_count > 0) {
    _fsw_raw_event_t ev;
    if (_fsw_buffer_pop(p, &ev)) {
      _fsw_raw_event_free(&ev);
    }
  }
  free(p->events);

  pthread_mutex_destroy(&p->mutex);
  if (p->semaphore) {
    dispatch_release(p->semaphore);
  }

  free(p);
}

int _fsw_platform_add_watch(_fsw_platform_t* p, const char* path, bool recursive)
{
  if (!p || !path) {
    return BEBOP_FSW_ERR_INVALID;
  }

  pthread_mutex_lock(&p->mutex);

  if (p->watch_count >= p->watch_capacity) {
    size_t new_cap = p->watch_capacity == 0 ? 4 : p->watch_capacity * 2;
    _fsw_darwin_watch_t* new_watches = realloc(p->watches, new_cap * sizeof(_fsw_darwin_watch_t));
    if (!new_watches) {
      pthread_mutex_unlock(&p->mutex);
      return BEBOP_FSW_ERR_NOMEM;
    }
    p->watches = new_watches;
    p->watch_capacity = new_cap;
  }

  _fsw_darwin_watch_t* w = &p->watches[p->watch_count];
  memset(w, 0, sizeof(_fsw_darwin_watch_t));

  w->wd = p->next_wd++;
  w->path = bebopc_strdup(path);
  if (!w->path) {
    pthread_mutex_unlock(&p->mutex);
    return BEBOP_FSW_ERR_NOMEM;
  }
  w->path_len = strlen(path);
  w->recursive = recursive;

  w->queue = dispatch_queue_create("bebop_fsw", DISPATCH_QUEUE_SERIAL);
  if (!w->queue) {
    free(w->path);
    pthread_mutex_unlock(&p->mutex);
    return BEBOP_FSW_ERR_SYSTEM;
  }

  CFStringRef cfPath = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
  if (!cfPath) {
    dispatch_release(w->queue);
    free(w->path);
    pthread_mutex_unlock(&p->mutex);
    return BEBOP_FSW_ERR_NOMEM;
  }

  CFArrayRef pathsToWatch =
      CFArrayCreate(kCFAllocatorDefault, (const void**)&cfPath, 1, &kCFTypeArrayCallBacks);
  CFRelease(cfPath);
  if (!pathsToWatch) {
    dispatch_release(w->queue);
    free(w->path);
    pthread_mutex_unlock(&p->mutex);
    return BEBOP_FSW_ERR_NOMEM;
  }

  FSEventStreamContext ctx = {
      .version = 0, .info = p, .retain = NULL, .release = NULL, .copyDescription = NULL
  };

  w->stream = FSEventStreamCreate(
      kCFAllocatorDefault,
      _fsw_fsevents_callback,
      &ctx,
      pathsToWatch,
      kFSEventStreamEventIdSinceNow,
      0.0,
      FSW_STREAM_FLAGS
  );
  CFRelease(pathsToWatch);

  if (!w->stream) {
    dispatch_release(w->queue);
    free(w->path);
    pthread_mutex_unlock(&p->mutex);
    return BEBOP_FSW_ERR_SYSTEM;
  }

  FSEventStreamSetDispatchQueue(w->stream, w->queue);
  if (!FSEventStreamStart(w->stream)) {
    FSEventStreamInvalidate(w->stream);
    FSEventStreamRelease(w->stream);
    dispatch_release(w->queue);
    free(w->path);
    pthread_mutex_unlock(&p->mutex);
    return BEBOP_FSW_ERR_SYSTEM;
  }

  int wd = w->wd;
  p->watch_count++;

  pthread_mutex_unlock(&p->mutex);
  return wd;
}

void _fsw_platform_remove_watch(_fsw_platform_t* p, int wd)
{
  if (!p) {
    return;
  }

  FSEventStreamRef stream = NULL;
  dispatch_queue_t queue = NULL;
  char* path = NULL;

  pthread_mutex_lock(&p->mutex);

  for (size_t i = 0; i < p->watch_count; i++) {
    if (p->watches[i].wd == wd) {
      stream = p->watches[i].stream;
      queue = p->watches[i].queue;
      path = p->watches[i].path;

      for (size_t j = i + 1; j < p->watch_count; j++) {
        p->watches[j - 1] = p->watches[j];
      }
      p->watch_count--;
      break;
    }
  }

  pthread_mutex_unlock(&p->mutex);

  // FSEventStreamInvalidate waits for an in-flight callback, and the
  // callback takes p->mutex; tearing down under the lock can deadlock.
  if (stream) {
    FSEventStreamStop(stream);
    FSEventStreamInvalidate(stream);
    FSEventStreamRelease(stream);
  }
  if (queue) {
    dispatch_release(queue);
  }
  free(path);
}

int _fsw_platform_poll(
    _fsw_platform_t* p, int timeout_ms, _fsw_raw_event_t* events, size_t max_events
)
{
  if (!p || !events || max_events == 0) {
    return BEBOP_FSW_ERR_INVALID;
  }

  dispatch_time_t timeout;
  if (timeout_ms < 0) {
    timeout = DISPATCH_TIME_FOREVER;
  } else if (timeout_ms == 0) {
    timeout = DISPATCH_TIME_NOW;
  } else {
    timeout = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ms * (int64_t)NSEC_PER_MSEC);
  }

  if (dispatch_semaphore_wait(p->semaphore, timeout) != 0) {
    return 0;
  }

  int count = 0;
  pthread_mutex_lock(&p->mutex);

  while ((size_t)count < max_events && _fsw_buffer_pop(p, &events[count])) {
    count++;

    if (dispatch_semaphore_wait(p->semaphore, DISPATCH_TIME_NOW) != 0) {
      break;
    }
  }

  pthread_mutex_unlock(&p->mutex);
  return count;
}

#endif
