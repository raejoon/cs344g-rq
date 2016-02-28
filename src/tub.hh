#ifndef TUB_HH
#define TUB_HH

#include <vector>

#include "util.hh"

template<typename T>
class Tub
{
public:
    Tub() : occupied(false) {}

    Tub(char* data)
        : occupied(true)
    {
        std::memcpy(raw, data, sizeof(T));
    }

    ~Tub() {
        destroy();
    }

    template<typename... Args>
    T* construct(Args&&... args) {
        destroy();
        new(raw) T(static_cast<Args&&>(args)...);
        occupied = true;
        return get();
    }

    void destroy() {
        if (occupied) {
            get()->~T();
            occupied = false;
        }
    }

    T* get() {
        return occupied ? reinterpret_cast<T*>(raw) : nullptr;
    }

    T*
    operator->() {
        return get();
    }

    size_t size() {
        return sizeof(T);
    }

private:
    bool occupied;

    char raw[sizeof(T)];

};

#endif /* TUB_HH */
