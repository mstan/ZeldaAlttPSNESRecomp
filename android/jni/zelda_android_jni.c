/* JNI entry points called by GameActivity (Java UI thread).
 *
 * Every call only stores an atomic request in android_host.c; the SDL thread
 * consumes them once per frame inside the main loop (src/main.c). Nothing here
 * touches emulator state directly. */
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>
#include "android_host.h"

/* The engine reports through stdout/stderr, which Android discards. Pipe both
 * into logcat (tag "zelda") as soon as libmain.so is loaded. */
static int s_log_pipe[2];

static void *log_pump(void *arg) {
  (void)arg;
  char buf[1024];
  size_t len = 0;
  ssize_t n;
  while ((n = read(s_log_pipe[0], buf + len, sizeof(buf) - 1 - len)) > 0) {
    len += (size_t)n;
    buf[len] = 0;
    char *start = buf, *nl;
    while ((nl = strchr(start, '\n')) != NULL) {
      *nl = 0;
      if (*start) __android_log_write(ANDROID_LOG_INFO, "zelda", start);
      start = nl + 1;
    }
    len = strlen(start);
    if (len == sizeof(buf) - 1) {   /* overlong line: flush as is */
      __android_log_write(ANDROID_LOG_INFO, "zelda", start);
      len = 0;
    } else {
      memmove(buf, start, len);
    }
  }
  return NULL;
}

__attribute__((constructor)) static void redirect_stdio_to_logcat(void) {
  if (pipe(s_log_pipe) != 0) return;
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  dup2(s_log_pipe[1], STDOUT_FILENO);
  dup2(s_log_pipe[1], STDERR_FILENO);
  pthread_t t;
  if (pthread_create(&t, NULL, log_pump, NULL) == 0) pthread_detach(t);
}

#define JNI_FN(name) Java_io_github_alttprecomp_GameActivity_##name

/* SNES pad bits, Zelda layout: B=0x1 Y=0x2 Select=0x4 Start=0x8 Up=0x10
 * Down=0x20 Left=0x40 Right=0x80 A=0x100 X=0x200 L=0x400 R=0x800. */
JNIEXPORT void JNICALL JNI_FN(nativeSetTouchButtons)(JNIEnv *env, jclass cls, jint mask) {
  (void)env; (void)cls;
  snesrecomp_android_input((uint32_t)mask);
}

JNIEXPORT void JNICALL JNI_FN(nativeSetPaused)(JNIEnv *env, jclass cls, jboolean paused) {
  (void)env; (void)cls;
  snesrecomp_android_pause(paused ? 1 : 0);
}

/* save != 0: write quick-save slot 0; save == 0: load it. */
JNIEXPORT void JNICALL JNI_FN(nativeQuickState)(JNIEnv *env, jclass cls, jboolean save) {
  (void)env; (void)cls;
  snesrecomp_android_slot(save ? 1 : 0);
}

JNIEXPORT void JNICALL JNI_FN(nativeRequestQuit)(JNIEnv *env, jclass cls) {
  (void)env; (void)cls;
  snesrecomp_android_quit();
}
