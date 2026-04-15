/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#ifndef _GGPO_LINUX_H_
#define _GGPO_LINUX_H_

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

/*
 * POSIX socket type compatibility
 */
typedef int SOCKET;
#define INVALID_SOCKET  (-1)
#define SOCKET_ERROR    (-1)

/*
 * A large sentinel value used in place of Windows INFINITE timeout constant.
 */
#ifndef INFINITE
#define INFINITE        0x7fffffff
#endif

/*
 * Boolean constants.
 */
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/*
 * MAX_PATH equivalent for POSIX systems.
 */
#ifndef MAX_PATH
#define MAX_PATH        4096
#endif

/*
 * Secure string/file function shims for POSIX.
 */
inline int strcpy_s(char *dest, size_t destsz, const char *src) {
    strncpy(dest, src, destsz);
    dest[destsz - 1] = '\0';
    return 0;
}

template<size_t N>
inline int strcpy_s(char (&dest)[N], const char *src) {
    return strcpy_s(dest, N, src);
}

inline int strncat_s(char *dest, size_t destsz, const char *src, size_t count) {
    size_t dest_len = strlen(dest);
    if (dest_len >= destsz) return 0;
    size_t remaining = destsz - dest_len - 1;
    size_t n = (count < remaining) ? count : remaining;
    strncat(dest, src, n);
    return 0;
}

inline int sprintf_s(char *buf, size_t bufsz, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, bufsz, fmt, args);
    va_end(args);
    return ret;
}

inline int vsprintf_s(char *buf, size_t bufsz, const char *fmt, va_list args) {
    return vsnprintf(buf, bufsz, fmt, args);
}

inline int fopen_s(FILE **fp, const char *name, const char *mode) {
    *fp = fopen(name, mode);
    return (*fp == NULL) ? errno : 0;
}

class Platform {
public:  // types
   typedef pid_t ProcessID;

public:  // functions
   static ProcessID GetProcessID() { return getpid(); }
   static void AssertFailed(char *msg) { }
   static uint32 GetCurrentTimeMS();

   static int GetConfigInt(const char* name) {
      const char *val = getenv(name);
      if (val == NULL) {
         return 0;
      }
      return atoi(val);
   }

   static bool GetConfigBool(const char* name) {
      const char *val = getenv(name);
      if (val == NULL) {
         return false;
      }
      return atoi(val) != 0 || strcasecmp(val, "true") == 0;
   }
};

#endif
