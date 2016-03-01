#ifndef COMMON_HH
#define COMMON_HH

#include <bitset>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "socket.hh"

/**
 * TODO: confirm it?
 * The recommended setting of parameter Al in rfc6330 is 32.
 */
typedef uint32_t Alignment;

/**
 * TODO:
 * Configure the size of a symbol as the maximum number of bytes that doesn't
 * result in IP fragmentation.
 */
#define SYMBOL_SIZE 8192

/**
 * The maximum number of blocks is 256, which can be fit into a uint8_t.
 */
#define MAX_BLOCKS 256

constexpr size_t NUM_ALIGN_PER_SYMBOL = SYMBOL_SIZE / sizeof(Alignment);

typedef std::array<Alignment, NUM_ALIGN_PER_SYMBOL> RaptorQSymbol;

typedef RaptorQ::Encoder<Alignment*, Alignment*> RaptorQEncoder;

typedef RaptorQ::Decoder<Alignment*, Alignment*> RaptorQDecoder;

typedef RaptorQ::Symbol_Iterator<Alignment*, Alignment*> RaptorQSymbolIterator;

// A macro to disallow the copy constructor and operator= functions
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName&) = delete;             \
    TypeName& operator=(const TypeName&) = delete;
#endif

/**
 * Cast one size of int down to another one.
 * Asserts that no precision is lost at runtime.
 */
template<typename Small, typename Large>
Small
downCast(const Large& large)
{
    Small small = static_cast<Small>(large);
    // The following comparison (rather than "large==small") allows
    // this method to convert between signed and unsigned values.
    assert(large-small == 0);
    return small;
}

uint32_t
generateRandom()
{
    std::srand(std::time(0));
    return std::rand();
}

size_t getPaddedSize(size_t size) {
    return size + size % sizeof(Alignment);
}

bool poll(UDPSocket* udpSocket, UDPSocket::received_datagram& datagram)
{
    try {
        datagram = udpSocket->recv();
        return true;
    } catch (const unix_error &e) {
        if (e.code().value() != EAGAIN) {
            printf("%s\n", e.what());
        }
        return false;
    }
}

/**
 * TODO
 */
template<typename T, typename... Args>
void sendInWireFormat(UDPSocket* udpSocket,
                      const Address& dest,
                      Args&&... args)
{
    char raw[sizeof(T)];
    new(raw) T(static_cast<Args&&>(args)...);
    udpSocket->sendbytesto(dest, raw, sizeof(T));
}

/**
 * A bit mask of 256 bits.
 */
struct Bitmask256 {

    std::bitset<64> bitset[4];

    Bitmask256()
        : bitset {0, 0, 0, 0}
    {}

    explicit
    Bitmask256(const uint64_t bitmask[4])
        : bitset {bitmask[0], bitmask[1], bitmask[2], bitmask[3]}
    {}

    /**
     * Performs bitwise OR operation with another bit mask.
     */
    void bitwiseOr(const Bitmask256& other)
    {
        bitset[0] |= other.bitset[0];
        bitset[1] |= other.bitset[1];
        bitset[2] |= other.bitset[2];
        bitset[3] |= other.bitset[3];
    }

    /**
     * Returns the number of bits that are set to true.
     */
    uint8_t count()
    {
        return downCast<uint8_t>(bitset[0].count() + bitset[1].count()
                + bitset[2].count() + bitset[3].count());
    }

    /**
     * Sets the n-th bit in the bit mask.
     */
    void set(uint8_t n, bool value = true)
    {
        bitset[n / 64].set(n % 64, value);
    }

    /**
     * Tests the n-th bit in the bit mask.
     */
    bool test(uint8_t n)
    {
        return bitset[n / 64].test(n % 64);
    }
};

template<typename Alignment>
class FileWrapper {
  public:
    FileWrapper(const char* filename)
        : fd(open(filename, O_RDONLY))
        , fileSize(getFileSize(filename))
        , paddedSize(getPaddedSize(fileSize))
        , start(reinterpret_cast<Alignment*>(
                    mmap(NULL, paddedSize, PROT_READ, MAP_PRIVATE, fd, 0)))
    {}

    ~FileWrapper()
    {
        if (isOpen()) {
            munmap(start, paddedSize);
        }
    }

    bool isOpen() const
    {
        return start != MAP_FAILED;
    }

    Alignment*
    begin()
    {
        return start;
    }

    Alignment*
    end()
    {
        return start + paddedSize / sizeof(Alignment);
    }

    size_t size() const
    {
        return fileSize;
    }

  private:

    static size_t
    getFileSize(const char* filename) {
        struct stat statBuf;
        stat(filename, &statBuf);
        return statBuf.st_size;
    }

    int fd;

    /**
     * The size of the file in number of bytes.
     */
    size_t fileSize;

    /**
     * The size of the file after padding.
     */
    size_t paddedSize;

    Alignment* start;

    DISALLOW_COPY_AND_ASSIGN(FileWrapper)
};

#endif /* COMMON_HH */
