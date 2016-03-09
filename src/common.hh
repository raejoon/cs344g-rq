#ifndef COMMON_HH
#define COMMON_HH

#include <bitset>
#include <chrono>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>

#include "socket.hh"

/**
 * The recommended setting of parameter Al in rfc6330 is 32.
 */
typedef uint32_t Alignment;

/**
 * Number of bytes per alignment.
 */
#define ALIGNMENT_SIZE sizeof(Alignment)

#define MAX_FILENAME_LEN 64

/**
 * Configure the size of a symbol as the maximum number of bytes that doesn't
 * result in IP fragmentation. It must be a multiple of the ALIGNMENT_SIZE.
 */
constexpr size_t SYMBOL_SIZE = (1400 / ALIGNMENT_SIZE) * ALIGNMENT_SIZE;

/**
 * The maximum number of blocks is 256, which can be fit into a uint8_t integer.
 */
#define MAX_BLOCKS 256

/**
 * The maximum number of symbols in a source block is 56403.
 */
#define MAX_SYMBOLS_PER_BLOCK 56403

/**
 * Given our choice of SYMBOL_SIZE to avoid IP fragmentation, the maximum size
 * of the file we can support is 20214835200 bytes, or ~20GBs.
 */
constexpr uint64_t MAX_FILE_SIZE = MAX_BLOCKS * MAX_SYMBOLS_PER_BLOCK *
        SYMBOL_SIZE;

/**
 * Initial value of the repair symbol transmission interval. It must be set
 * to a relatively small number because the sender will only get a more
 * accurate estimated value from the receiver once it has decoded the first
 * block. If we set this initial value too high and we are under a high packet
 * loss environment, the sender may end up using this initial value even after
 * all the source symbols have been transmitted and none of the blocks has been
 * successfully decoded by the receiver.
 *
 * Right now, we conservatively assume the packet loss rate p is 10%. So the
 * initial interval is set to 1( = (1-p)/p).
 */
#define INIT_REPAIR_SYMBOL_INTERVAL 9

constexpr size_t NUM_ALIGN_PER_SYMBOL = SYMBOL_SIZE / ALIGNMENT_SIZE;

typedef std::array<Alignment, NUM_ALIGN_PER_SYMBOL> RaptorQSymbol;

typedef RaptorQ::Encoder<Alignment*, Alignment*> RaptorQEncoder;

typedef RaptorQ::Decoder<Alignment*, Alignment*> RaptorQDecoder;

typedef RaptorQ::Symbol_Iterator<Alignment*, Alignment*> RaptorQSymbolIterator;

const static std::chrono::duration<int64_t, std::milli> HEARTBEAT_INTERVAL =
        std::chrono::milliseconds(50);

typedef std::lock_guard<std::mutex> Guard;

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

/**
 * TODO(YilongL)
 */
template<typename T>
std::unique_ptr<T> receive(DCCPSocket* socket)
{
    char* datagram = socket->recv();
    return std::unique_ptr<T>(reinterpret_cast<T*>(datagram));
}

template<typename T>
std::unique_ptr<T> receive(UDPSocket* socket)
{
    UDPSocket::received_datagram datagram = socket->recv();
    if (datagram.recvlen <= 0) {
        return nullptr;
    }
    return std::unique_ptr<T>(reinterpret_cast<T*>(datagram.payload));
}

/**
 * Serialize an object in its wire format and send it with the socket.
 *
 * \return
 *      True if the object is successfully sent; False otherwise.
 */
template<typename T, typename... Args>
int sendInWireFormat(DCCPSocket* socket,
                      Args&&... args)
{
    char raw[sizeof(T)];
    new(raw) T(static_cast<Args&&>(args)...);
    return socket->send(raw, sizeof(T));
}

template<typename T, typename... Args>
void sendInWireFormat(UDPSocket* socket,
                      Args&&... args)
{
    char raw[sizeof(T)];
    new(raw) T(static_cast<Args&&>(args)...);
    socket->sendbytes(raw, sizeof(T));
}

/**
 * A bit mask of 256 bits.
 */
struct Bitmask256 {

    std::bitset<64> bitset[4];

    std::mutex mutex;

    Bitmask256()
        : bitset {0, 0, 0, 0}
        , mutex()
    {}

    explicit
    Bitmask256(const uint64_t bitmask[4])
        : bitset {bitmask[0], bitmask[1], bitmask[2], bitmask[3]}
        , mutex()
    {}

    /**
     * Performs bitwise OR operation with another bit mask.
     */
    void bitwiseOr(const Bitmask256& other)
    {
        Guard _(mutex);
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
        Guard _(mutex);
        return downCast<uint8_t>(bitset[0].count() + bitset[1].count()
                + bitset[2].count() + bitset[3].count());
    }

    /**
     * Sets the n-th bit in the bit mask.
     */
    void set(uint8_t n, bool value = true)
    {
        Guard _(mutex);
        bitset[n / 64].set(n % 64, value);
    }

    /**
     * Sets first N bits in the bit mask to be 1;
     */
    void setFirstN(uint8_t n) {
        for (uint8_t i = 0; i < n; i++) {
            set(i);
        }
    }

    /**
     * Tests the n-th bit in the bit mask.
     */
    bool test(uint8_t n)
    {
        Guard _(mutex);
        return bitset[n / 64].test(n % 64);
    }

    std::array<std::bitset<64>, 4> toBitsetArray() {
        Guard _(mutex);
        return std::array<std::bitset<64>, 4>{
                bitset[0], bitset[1], bitset[2], bitset[3]};
    }
};

template<typename Alignment>
class FileWrapper {
  public:
    FileWrapper(const std::string& pathname)
        : fd(open(pathname.c_str(), O_RDONLY))
        , fileName(pathname.substr(pathname.find_last_of("/\\") + 1))
        , fileSize(getFileSize(pathname))
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
        return start + paddedSize / ALIGNMENT_SIZE;
    }

    const char*
    name() const
    {
        return fileName.c_str();
    }

    size_t size() const
    {
        return fileSize;
    }

  private:

    static size_t
    getFileSize(const std::string& pathname) {
        struct stat statBuf;
        stat(pathname.c_str(), &statBuf);
        return statBuf.st_size;
    }

    static size_t
    getPaddedSize(size_t size) {
        if (size % ALIGNMENT_SIZE == 0) {
            return size;
        } else {
            return size + ALIGNMENT_SIZE - size % ALIGNMENT_SIZE;
        }
    }

    int fd;

    const std::string fileName;

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
