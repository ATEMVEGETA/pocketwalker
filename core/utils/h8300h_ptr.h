#pragma once
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>

// provides an h8/300h big endian byte swap ptr for u16 and u32
// holy moly this is ugly

template <std::integral T>
class h8300h_ptr
{
public:
    struct ref
    {
        h8300h_ptr* p;

        operator T() const { return std::byteswap(*p->ptr); }

        ref& operator=(T val)
        {
            *p = val;
            return *this;
        }

        ref& operator++()
        {
            *p = (T)(std::byteswap(*p->ptr) + 1);
            return *this;
        }

        T operator++(int)
        {
            T old = std::byteswap(*p->ptr);
            ++*this;
            return old;
        }

        ref& operator--()
        {
            *p = (T)(std::byteswap(*p->ptr) - 1);
            return *this;
        }

        T operator--(int)
        {
            T old = std::byteswap(*p->ptr);
            --*this;
            return old;
        }
    };

    h8300h_ptr() : ptr(nullptr)
    {
    }

    h8300h_ptr(std::nullptr_t) : ptr(nullptr)
    {
    }

    explicit h8300h_ptr(T* ptr) : ptr(ptr)
    {
    }

    ref operator*() { return ref{this}; }
    T operator*() const { return std::byteswap(*ptr); }

    h8300h_ptr& operator=(T val)
    {
        *ptr = std::byteswap(val);
        return *this;
    }

    h8300h_ptr& operator+=(T val)
    {
        *this = **this + val;
        return *this;
    }

    h8300h_ptr& operator-=(T val)
    {
        *this = **this - val;
        return *this;
    }

    h8300h_ptr& operator++()
    {
        *this = **this + 1;
        return *this;
    }

    bool operator==(T val) const { return **this == val; }
    bool operator>=(T val) const { return **this >= val; }
    bool operator<=(T val) const { return **this <= val; }
    bool operator>(T val) const { return **this > val; }
    bool operator<(T val) const { return **this < val; }

    T* ptr;
};
