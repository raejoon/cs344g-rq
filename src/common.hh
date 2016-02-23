#ifndef COMMON_HH
#define COMMON_HH

#include <bitset>

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
 * The maximum bumber of symbols per block; the asymptotic complexity of time
 * and space are cubic and quadratic of this number respectively.
 */
#define MAX_SYM_PER_BLOCK 100

/**
 * The maximum size of the block that is decodable in working memory, in bytes.
 */
#define MAX_DECODABLE_BLOCK_SIZE (SYMBOL_SIZE * MAX_SYM_PER_BLOCK)

constexpr size_t NUM_ALIGN_PER_SYMBOL = SYMBOL_SIZE / sizeof(Alignment);

typedef RaptorQ::Encoder<typename std::vector<Alignment>::iterator,
        typename std::vector<Alignment>::iterator> RaptorQEncoder;

typedef RaptorQ::Decoder<typename std::vector<Alignment>::iterator,
        typename std::vector<Alignment>::iterator> RaptorQDecoder;

typedef RaptorQ::Symbol_Iterator<typename std::vector<Alignment>::iterator,
        typename std::vector<Alignment>::iterator> RaptorQSymbolIterator;

// TODO: change to std::array<Alignment, NUM_ALIGN_PER_SYMBOL>?
typedef std::vector<Alignment> RaptorQSymbol;

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
    // TODO: revise this constructor?
    FileWrapper(size_t size, std::vector<Alignment> data)
        : fileSize(size)
        , data(std::move(data))
    {}

    typename std::vector<Alignment>::iterator
    begin()
    {
        return data.begin();
    }

    typename std::vector<Alignment>::iterator
    end()
    {
        return data.end();
    }

    size_t size() const
    {
        return fileSize;
    }

  private:
    /**
     * The size of the file in number of bytes.
     */
    const size_t fileSize;

    /**
     * TODO: change it to an iterator or something s.t. we don't have to put
     * the whole file in vector.
     */
    std::vector<Alignment> data;
};

#endif /* COMMON_HH */
