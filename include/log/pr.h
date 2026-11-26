#ifndef LOG_PR_H
#define LOG_PR_H

#include <err.h>
#include <stdio.h>

/* "file:line: func() level: msg"  */
#define pr_fmt(lvl, fmt, ...)                                                 \
  "%s:%d: %s() " lvl ": " fmt, __FILE_NAME__, __LINE__,                       \
      __func__ __VA_OPT__ (, ) __VA_ARGS__

/* stderr is locked so lines from different threads dont mix  */
#define pr_warn(...)                                                          \
  do                                                                          \
    {                                                                         \
      flockfile (stderr);                                                     \
      warn (pr_fmt ("warning", __VA_ARGS__));                                 \
      funlockfile (stderr);                                                   \
    }                                                                         \
  while (0)
#define pr_warnx(...)                                                         \
  do                                                                          \
    {                                                                         \
      flockfile (stderr);                                                     \
      warnx (pr_fmt ("warning", __VA_ARGS__));                                \
      funlockfile (stderr);                                                   \
    }                                                                         \
  while (0)
#define pr_err(eval, ...)                                                     \
  do                                                                          \
    {                                                                         \
      flockfile (stderr);                                                     \
      err (eval, pr_fmt ("error", __VA_ARGS__));                              \
    }                                                                         \
  while (0)
#define pr_errx(eval, ...)                                                    \
  do                                                                          \
    {                                                                         \
      flockfile (stderr);                                                     \
      errx (eval, pr_fmt ("error", __VA_ARGS__));                             \
    }                                                                         \
  while (0)

#ifdef DEBUG
#define pr_debug(...)                                                         \
  do                                                                          \
    {                                                                         \
      flockfile (stderr);                                                     \
      warn (pr_fmt ("debug", __VA_ARGS__));                                   \
      funlockfile (stderr);                                                   \
    }                                                                         \
  while (0)
#define pr_debugx(...)                                                        \
  do                                                                          \
    {                                                                         \
      flockfile (stderr);                                                     \
      warnx (pr_fmt ("debug", __VA_ARGS__));                                  \
      funlockfile (stderr);                                                   \
    }                                                                         \
  while (0)
#else
#define pr_debug(...)                                                         \
  do                                                                          \
    {                                                                         \
      if (0) /* check params on compilation  */                               \
        warn (pr_fmt ("debug", __VA_ARGS__));                                 \
    }                                                                         \
  while (0)
#define pr_debugx(...) pr_debug (__VA_ARGS__)
#endif

#endif /* LOG_PR_H  */
