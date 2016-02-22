#ifndef BUFFER_HH
#define BUFFER_HH

#include <vector>

class Buffer
{
public:
    const static size_t MAX_BUF_LEN = 65536;

    Buffer() : length(0) {};

    Buffer(char* data, size_t length)
        : length(length)
    {
        std::memcpy(buf, data, length);
    }

    void clear() {
        length = 0;
    }

    template<typename T, typename... Args>
    T* emplaceAppend(Args&&... args) {
        void* allocation = buf + length;
        length += sizeof(T);
        new(allocation) T(static_cast<Args&&>(args)...);
        return static_cast<T*>(allocation);
    }

    template<typename T>
    T* append() {
        void* allocation = buf + length;
        length += sizeof(T);
        return static_cast<T*>(allocation);
    }

    template<typename T>
    const T*
    get(size_t offset) {
        return reinterpret_cast<const T*>(buf + offset);
    }

    size_t size() {
        return length;
    }

    const char* c_str() {
        return buf;
    }

private:

    char buf[MAX_BUF_LEN];

    size_t length;

};

#endif /* BUFFER_HH */
