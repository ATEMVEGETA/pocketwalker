#pragma once

#ifdef __ANDROID__
#include <cstdio>
#else
#include <format>
#include <print>
#endif
#include <cstdlib>

class Log
{
public:
#ifdef __ANDROID__
    template <typename... Args>
    static void Info(const char* fmt, Args&&...)
    {
#ifndef NDEBUG
        std::fprintf(stdout, "[INFO] %s\n", fmt);
#endif
    }

    template <typename... Args>
    static void Warn(const char* fmt, Args&&...)
    {
#ifndef NDEBUG
        std::fprintf(stderr, "[WARN] %s\n", fmt);
#endif
    }

    template <typename... Args>
    static void Error(const char* fmt, Args&&...)
    {
#ifndef NDEBUG
        std::fprintf(stderr, "[ERROR] %s\n", fmt);
#endif
    }

    template <typename... Args>
    static void Fatal(const char* fmt, Args&&...)
    {
#ifndef NDEBUG
        std::fprintf(stderr, "[FATAL] %s\n", fmt);
#endif
        std::abort();
    }
#else
    template <typename... Args>
    static void Info(std::format_string<Args...> fmt, Args&&... args)
    {
#ifndef NDEBUG
        std::println("[INFO] {}", std::format(fmt, std::forward<Args>(args)...));
#endif
    }

    template <typename... Args>
    static void Warn(std::format_string<Args...> fmt, Args&&... args)
    {
#ifndef NDEBUG
        std::println(stderr, "[WARN] {}", std::format(fmt, std::forward<Args>(args)...));
#endif
    }

    template <typename... Args>
    static void Error(std::format_string<Args...> fmt, Args&&... args)
    {
#ifndef NDEBUG
        std::println(stderr, "[ERROR] {}", std::format(fmt, std::forward<Args>(args)...));
#endif
    }

    template <typename... Args>
    static void Fatal(std::format_string<Args...> fmt, Args&&... args)
    {
#ifndef NDEBUG
        std::println(stderr, "[FATAL] {}", std::format(fmt, std::forward<Args>(args)...));
#endif
        std::abort();
    }
#endif
};
