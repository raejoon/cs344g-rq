#ifndef TUB_HH
#define TUB_HH

#include <vector>

#include "util.hh"

template<typename T>
class Tub
{
public:
    Tub(char* data)
    {
        std::memcpy(raw, data, sizeof(T));
    }

    ~Tub() {
        get()->~T();
    }

    T* get() {
        return reinterpret_cast<T*>(raw);
    }

    T*
    operator->() {
        return get();
    }

    size_t size() {
        return sizeof(T);
    }

private:
    char raw[sizeof(T)];

};

#endif /* TUB_HH */
