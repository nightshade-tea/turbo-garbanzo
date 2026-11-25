#ifndef LOG_PR_H
#define LOG_PR_H

#include <err.h>

/* "file:line: func() level: msg"  */
#define pr_fmt(lvl, fmt, ...)                                                 \
  "%s:%d: %s() " lvl ": " fmt, __FILE_NAME__, __LINE__,                       \
      __func__ __VA_OPT__ (, ) __VA_ARGS__

#define pr_warn(...) warn (pr_fmt ("warning", __VA_ARGS__))
#define pr_warnx(...) warnx (pr_fmt ("warning", __VA_ARGS__))
#define pr_err(eval, ...) err (eval, pr_fmt ("error", __VA_ARGS__))
#define pr_errx(eval, ...) errx (eval, pr_fmt ("error", __VA_ARGS__))

#ifdef DEBUG
#define pr_debug(...) warnx (pr_fmt ("debug", __VA_ARGS__))
#else
#define pr_debug(...)                                                         \
  do                                                                          \
    {                                                                         \
      if (0) /* check params on compilation  */                               \
        warnx (pr_fmt ("debug", __VA_ARGS__));                                \
    }                                                                         \
  while (0)
#endif

#endif /* LOG_PR_H  */
