#pragma once

#include <iostream>
#include <cstring>

extern bool logging;

// Extract the basename from the full filename
inline const char *
get_basename (const char *path) noexcept
{
  const char *base = strrchr (path, '/');
  return base ? base + 1 : path;
}

#define LOG(...)                                                                       \
  do                                                                                   \
    {                                                                                  \
      if (logging)                                                                     \
        {                                                                              \
          std::cout << basename (__FILE__) << ":" << __LINE__ << " - " << __VA_ARGS__; \
        }                                                                              \
  } while (0)