#include "node_graph_format/NodeGraph.h"
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <format>
#include <sstream>

#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define NGF_HAS_SSE2 1
#include <emmintrin.h> // SSE2
#else
#define NGF_HAS_SSE2 0
#endif

#ifdef _MSC_VER
#define NGF_FORCE_INLINE __forceinline
#else
#define NGF_FORCE_INLINE inline
#endif

namespace node_graph_format {

// Helper to get bit scan forward result as a value
NGF_FORCE_INLINE unsigned bsf(unsigned mask)
{
    return std::countr_zero(mask);
}

// ---------------------------------------------------------------------------
// NodeValue tagged union implementation
// ---------------------------------------------------------------------------

void NodeValue::destroy() noexcept
{
    switch (type) {
    case List:
        list.~vector();
        break;
    default:
        break;
    }
    type = Null;
    intVal = 0;
}

NodeValue::NodeValue(NodeValue&& other) noexcept
    : type(other.type)
{
    switch (type) {
    case Null: intVal = 0; break;
    case Bool: boolVal = other.boolVal; break;
    case Int: intVal = other.intVal; break;
    case Float: floatVal = other.floatVal; break;
    case Str:
    case Source:
        strVal = other.strVal;
        break;
    case List:
        new (&list) std::vector<NodeValue>(std::move(other.list));
        break;
    case Object:
        objectVal = other.objectVal;
        break;
    }
    other.type = Null;
    other.intVal = 0;
}

NodeValue& NodeValue::operator=(NodeValue&& other) noexcept
{
    if (this != &other) {
        destroy();
        type = other.type;
        switch (type) {
        case Null: intVal = 0; break;
        case Bool: boolVal = other.boolVal; break;
        case Int: intVal = other.intVal; break;
        case Float: floatVal = other.floatVal; break;
        case Str:
        case Source:
            strVal = other.strVal;
            break;
        case List:
            new (&list) std::vector<NodeValue>(std::move(other.list));
            break;
        case Object:
            objectVal = other.objectVal;
            break;
        }
        other.type = Null;
        other.intVal = 0;
    }
    return *this;
}

bool NodeValue::asBool(bool defaultVal) const
{
    return type == Bool ? boolVal : defaultVal;
}

int64_t NodeValue::asInt(int64_t defaultVal) const
{
    return type == Int ? intVal : defaultVal;
}

double NodeValue::asFloat(double defaultVal) const
{
    if (type == Float)
        return floatVal;
    if (type == Int)
        return static_cast<double>(intVal);
    return defaultVal;
}

std::string_view NodeValue::asString(std::string_view defaultVal) const
{
    return (type == Str || type == Source) ? strVal : defaultVal;
}

const GraphNode* NodeValue::asObject() const
{
    return type == Object ? objectVal : nullptr;
}

GraphNode* NodeValue::asObject()
{
    return type == Object ? objectVal : nullptr;
}

const std::vector<NodeValue>* NodeValue::asList() const
{
    return type == List ? &list : nullptr;
}

// ---------------------------------------------------------------------------
// Property
// ---------------------------------------------------------------------------

Property::Property(std::string_view n, NodeValue v)
    : name(n)
    , label()
    , value(std::move(v))
{
}

// ---------------------------------------------------------------------------
// GraphNode
// ---------------------------------------------------------------------------

GraphNode::~GraphNode() = default;

void GraphNode::rebuildIndex() const
{
    mPropertyIndex.clear();
    mPropertyIndex.reserve(properties.size());
    for (size_t i = 0; i < properties.size(); ++i) {
        mPropertyIndex[properties[i].name] = i;
    }
    mIndexDirty = false;
}

const Property* GraphNode::findProperty(std::string_view name) const
{
    if (mIndexDirty)
        rebuildIndex();
    auto it = mPropertyIndex.find(name);
    if (it != mPropertyIndex.end() && it->second < properties.size())
        return &properties[it->second];
    return nullptr;
}

Property* GraphNode::findProperty(std::string_view name)
{
    if (mIndexDirty)
        rebuildIndex();
    auto it = mPropertyIndex.find(name);
    if (it != mPropertyIndex.end() && it->second < properties.size())
        return &properties[it->second];
    return nullptr;
}

bool GraphNode::hasProperty(std::string_view name) const
{
    return findProperty(name) != nullptr;
}

bool GraphNode::getBool(std::string_view name, bool defaultVal) const
{
    auto* p = findProperty(name);
    return p ? p->value.asBool(defaultVal) : defaultVal;
}

int64_t GraphNode::getInt(std::string_view name, int64_t defaultVal) const
{
    auto* p = findProperty(name);
    return p ? p->value.asInt(defaultVal) : defaultVal;
}

double GraphNode::getFloat(std::string_view name, double defaultVal) const
{
    auto* p = findProperty(name);
    return p ? p->value.asFloat(defaultVal) : defaultVal;
}

std::string_view GraphNode::getString(std::string_view name, std::string_view defaultVal) const
{
    auto* p = findProperty(name);
    if (!p)
        return defaultVal;
    return p->value.asString(defaultVal);
}

const GraphNode* GraphNode::getObject(std::string_view name) const
{
    auto* p = findProperty(name);
    return p ? p->value.asObject() : nullptr;
}

const std::vector<NodeValue>* GraphNode::getList(std::string_view name) const
{
    auto* p = findProperty(name);
    return p ? p->value.asList() : nullptr;
}

const GraphNode* GraphNode::findChild(std::string_view cls) const
{
    for (auto* child : children) {
        if (child->className == cls)
            return child;
    }
    return nullptr;
}

GraphNode* GraphNode::findChild(std::string_view cls)
{
    for (auto* child : children) {
        if (child->className == cls)
            return child;
    }
    return nullptr;
}

std::vector<const GraphNode*> GraphNode::findChildren(std::string_view cls) const
{
    std::vector<const GraphNode*> result;
    for (auto* child : children) {
        if (child->className == cls)
            result.push_back(child);
    }
    return result;
}

std::vector<GraphNode*> GraphNode::findChildren(std::string_view cls)
{
    std::vector<GraphNode*> result;
    for (auto* child : children) {
        if (child->className == cls)
            result.push_back(child);
    }
    return result;
}

Property& GraphNode::addProperty(std::string_view name, NodeValue value)
{
    // If index is dirty, avoid rebuilding it just to check for duplicates
    // since push_back will invalidate it anyway. Use linear scan instead.
    if (mIndexDirty) {
        for (auto& prop : properties) {
            if (prop.name == name) {
                prop.value = std::move(value);
                return prop;
            }
        }
    } else {
        auto it = mPropertyIndex.find(name);
        if (it != mPropertyIndex.end() && it->second < properties.size()) {
            auto& prop = properties[it->second];
            prop.value = std::move(value);
            return prop;
        }
    }
    mIndexDirty = true;
    properties.emplace_back(name, std::move(value));
    return properties.back();
}

// ---------------------------------------------------------------------------
// GraphNodePool
// ---------------------------------------------------------------------------

GraphNode* GraphNodePool::create()
{
    if (mChunks.back()->used >= kChunkSize) {
        mChunks.push_back(std::make_unique<Chunk>());
    }
    auto& chunk = mChunks.back();
    auto* ptr = reinterpret_cast<GraphNode*>(&chunk->storage[sizeof(GraphNode) * chunk->used]);
    new (ptr) GraphNode();
    chunk->used++;
    return ptr;
}

std::string_view GraphNodePool::allocString(std::string_view s)
{
    mOwnedStrings.emplace_back(s);
    return std::string_view(mOwnedStrings.back());
}

void GraphNodePool::clear()
{
    for (auto& chunk : mChunks) {
        for (size_t i = 0; i < chunk->used; ++i) {
            auto* ptr = reinterpret_cast<GraphNode*>(&chunk->storage[sizeof(GraphNode) * i]);
            ptr->~GraphNode();
        }
        chunk->used = 0;
    }
    // Keep one chunk for reuse instead of clearing all
    if (mChunks.size() > 1) {
        mChunks.erase(mChunks.begin() + 1, mChunks.end());
    } else if (mChunks.empty()) {
        mChunks.push_back(std::make_unique<Chunk>());
    }
    mOwnedStrings.clear();
}

// ---------------------------------------------------------------------------
// Parser - zero-copy string_view based
// ---------------------------------------------------------------------------

namespace {

class ParseError : public std::exception {
public:
    explicit ParseError(std::string msg)
        : mMessage(std::move(msg))
    {
    }
    const char* what() const noexcept override { return mMessage.c_str(); }

private:
    std::string mMessage;
};

ParseError parseError(std::string_view source, size_t at, std::string_view expected)
{
    char token = at < source.length() ? source[at] : '?';
    std::string tokenText;
    switch (token) {
    case '\n': tokenText = "\\n"; break;
    case '\r': tokenText = "\\r"; break;
    case '\t': tokenText = "\\t"; break;
    case '"': tokenText = "\\\""; break;
    case '\\': tokenText = "\\\\"; break;
    default: tokenText.assign(1, token); break;
    }

    auto near = at < source.length() ? source.substr(at, 25) : std::string_view("");
    std::string nearText;
    nearText.reserve(near.size());
    for (char c : near) {
        switch (c) {
        case '\n': nearText += "\\n"; break;
        case '\r': nearText += "\\r"; break;
        case '\t': nearText += "\\t"; break;
        case '"': nearText += "\\\""; break;
        case '\\': nearText += "\\\\"; break;
        default: nearText += c; break;
        }
    }
    int line = 1;
    int column = 1;
    for (size_t i = 0; i < at; ++i) {
        if (source[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return ParseError(std::format("Unexpected token '{}', expected {} at line {}, column {} near \"{}\"",
        tokenText, expected, line, column, nearText));
}

int hexDigitVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Lookup table for character classification
struct CharTable {
    static constexpr uint8_t kWs = 1 << 0;
    static constexpr uint8_t kIdTerm = 1 << 1;
    static constexpr uint8_t kNumStart = 1 << 2;
    static constexpr uint8_t kNumCont = 1 << 3;

    uint8_t table[128] {};

    constexpr CharTable()
    {
        for (char c : {' ', '\t', '\n', '\r'})
            table[static_cast<unsigned char>(c)] |= kWs;
        for (char c : {' ', '\t', '\n', '\r', '=', ':', '{', '}', '[', ']', ',', '"'})
            table[static_cast<unsigned char>(c)] |= kIdTerm;
        for (char c : {'-', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'})
            table[static_cast<unsigned char>(c)] |= kNumStart;
        for (char c : {'.', 'e', 'E'})
            table[static_cast<unsigned char>(c)] |= kNumCont;
    }

    bool isWs(char c) const
    {
        auto uc = static_cast<unsigned char>(c);
        return uc < 128 && (table[uc] & kWs);
    }

    bool isIdTerm(char c) const
    {
        auto uc = static_cast<unsigned char>(c);
        return uc >= 128 || (table[uc] & kIdTerm);
    }

    bool isNumStart(char c) const
    {
        auto uc = static_cast<unsigned char>(c);
        return uc < 128 && (table[uc] & kNumStart);
    }

    bool isNumCont(char c) const
    {
        auto uc = static_cast<unsigned char>(c);
        return uc < 128 && (table[uc] & kNumCont);
    }
};

static constexpr CharTable kCharTable;

// ---------------------------------------------------------------------------
// SIMD helpers for fast character scanning (SSE2)
// ---------------------------------------------------------------------------

#if NGF_HAS_SSE2

// Find first non-whitespace char using SSE2 (16 bytes at a time)
// Returns offset of first non-ws char, or count if all whitespace
NGF_FORCE_INLINE size_t simdSkipWs(const char* data, size_t len)
{
    size_t pos = 0;
    // SSE2 mask for whitespace chars: ' ' (0x20), '\t' (0x09), '\n' (0x0A), '\r' (0x0D)
    const __m128i ws1 = _mm_set1_epi8(' ');
    const __m128i ws2 = _mm_set1_epi8('\t');
    const __m128i ws3 = _mm_set1_epi8('\n');
    const __m128i ws4 = _mm_set1_epi8('\r');

    while (pos + 16 <= len) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + pos));
        __m128i cmp1 = _mm_cmpeq_epi8(chunk, ws1);
        __m128i cmp2 = _mm_cmpeq_epi8(chunk, ws2);
        __m128i cmp3 = _mm_cmpeq_epi8(chunk, ws3);
        __m128i cmp4 = _mm_cmpeq_epi8(chunk, ws4);
        // OR all comparisons
        __m128i cmp = _mm_or_si128(_mm_or_si128(cmp1, cmp2), _mm_or_si128(cmp3, cmp4));
        unsigned mask = _mm_movemask_epi8(cmp);
        if (mask != 0xFFFF) {
            // Found non-ws char - find its position
            return pos + (size_t)bsf(~mask & 0xFFFF);
        }
        pos += 16;
    }
    return pos; // Remaining bytes handled by scalar
}

// Find first identifier-terminator char using SSE2
// Terminators: ' ', '\t', '\n', '\r', '=', ':', '{', '}', '[', ']', ',', '"'
NGF_FORCE_INLINE size_t simdFindIdTerm(const char* data, size_t len)
{
    size_t pos = 0;
    const __m128i t1 = _mm_set1_epi8(' ');
    const __m128i t2 = _mm_set1_epi8('\t');
    const __m128i t3 = _mm_set1_epi8('\n');
    const __m128i t4 = _mm_set1_epi8('\r');
    const __m128i t5 = _mm_set1_epi8('=');
    const __m128i t6 = _mm_set1_epi8(':');
    const __m128i t7 = _mm_set1_epi8('{');
    const __m128i t8 = _mm_set1_epi8('}');
    const __m128i t9 = _mm_set1_epi8('[');
    const __m128i t10 = _mm_set1_epi8(']');
    const __m128i t11 = _mm_set1_epi8(',');
    const __m128i t12 = _mm_set1_epi8('"');

    while (pos + 16 <= len) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + pos));
        // Check for bytes >= 0x80 (also terminators for identifiers)
        __m128i hibit = _mm_set1_epi8(static_cast<char>(0x80));
        __m128i cmpHi = _mm_and_si128(chunk, hibit);

        __m128i cmp = _mm_or_si128(
            _mm_or_si128(
                _mm_or_si128(_mm_cmpeq_epi8(chunk, t1), _mm_cmpeq_epi8(chunk, t2)),
                _mm_or_si128(_mm_cmpeq_epi8(chunk, t3), _mm_cmpeq_epi8(chunk, t4))
            ),
            _mm_or_si128(
                _mm_or_si128(_mm_cmpeq_epi8(chunk, t5), _mm_cmpeq_epi8(chunk, t6)),
                _mm_or_si128(_mm_cmpeq_epi8(chunk, t7), _mm_cmpeq_epi8(chunk, t8))
            )
        );
        cmp = _mm_or_si128(cmp,
            _mm_or_si128(
                _mm_or_si128(_mm_cmpeq_epi8(chunk, t9), _mm_cmpeq_epi8(chunk, t10)),
                _mm_or_si128(_mm_cmpeq_epi8(chunk, t11), _mm_cmpeq_epi8(chunk, t12))
            )
        );
        cmp = _mm_or_si128(cmp, cmpHi);

        unsigned mask = _mm_movemask_epi8(cmp);
        if (mask != 0) {
            return pos + (size_t)bsf(mask);
        }
        pos += 16;
    }
    return pos;
}

// Find first char matching target using SSE2 (for memchr replacement)
NGF_FORCE_INLINE size_t simdFindChar(const char* data, size_t len, char target)
{
    size_t pos = 0;
    const __m128i tgt = _mm_set1_epi8(target);
    while (pos + 16 <= len) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + pos));
        __m128i cmp = _mm_cmpeq_epi8(chunk, tgt);
        unsigned mask = _mm_movemask_epi8(cmp);
        if (mask != 0) {
            return pos + (size_t)bsf(mask);
        }
        pos += 16;
    }
    return pos;
}

#endif // NGF_HAS_SSE2

// Lightweight capacity estimate based on input size (avoids full pre-scan)
struct CapacityHint {
    size_t properties = 0;
    size_t children = 0;
};

NGF_FORCE_INLINE CapacityHint estimateCapacityFast(std::string_view text)
{
    CapacityHint hint;
    size_t scanLen = text.length() < 4096 ? text.length() : 4096;

#if NGF_HAS_SSE2
    size_t eqCount = 0;
    size_t braceCount = 0;
    size_t pos = 0;
    const __m128i eq = _mm_set1_epi8('=');
    const __m128i brace = _mm_set1_epi8('{');
    while (pos + 16 <= scanLen) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(text.data() + pos));
        unsigned eqMask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, eq));
        unsigned brMask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, brace));
        eqCount += std::popcount(eqMask);
        braceCount += std::popcount(brMask);
        pos += 16;
    }
    for (size_t k = pos; k < scanLen; ++k) {
        if (text[k] == '=') eqCount++;
        if (text[k] == '{') braceCount++;
    }
#else
    size_t eqCount = 0;
    size_t braceCount = 0;
    for (size_t k = 0; k < scanLen; ++k) {
        if (text[k] == '=') eqCount++;
        if (text[k] == '{') braceCount++;
    }
#endif
    // Scale up proportionally for large inputs
    if (scanLen < text.length()) {
        double scale = static_cast<double>(text.length()) / static_cast<double>(scanLen);
        eqCount = static_cast<size_t>(eqCount * scale * 1.1);
        braceCount = static_cast<size_t>(braceCount * scale * 1.1);
    }
    hint.properties = eqCount;
    hint.children = braceCount > eqCount ? braceCount - eqCount : 0;
    return hint;
}

class NodeParser {
public:
    bool parse(std::string_view text, GraphNode& root, GraphNodePool& pool, std::string& error)
    {
        s = text;
        i = 0;
        mPool = &pool;
        error.clear();
        try {
            auto hint = estimateCapacityFast(text);
            root.properties.reserve(hint.properties);
            root.children.reserve(hint.children);
            parseRoot(root);
            return true;
        } catch (const ParseError& e) {
            error = e.what();
            return false;
        }
    }

private:
    std::string_view s;
    size_t i = 0;
    GraphNodePool* mPool = nullptr;
    int mDepth = 0;

    static constexpr int kMaxDepth = 256;
    static constexpr char QUOTE = '"';
    static constexpr char SLASH = '/';
    static constexpr char BACKSLASH = '\\';
    static constexpr char OPENBRACE = '{';
    static constexpr char CLOSEBRACE = '}';
    static constexpr char COLON = ':';
    static constexpr char EQUALS = '=';
    static constexpr char AT = '@';
    static constexpr char LF = '\n';
    static constexpr char OPENSQUARE = '[';
    static constexpr char CLOSESQUARE = ']';
    static constexpr char COMMA = ',';

    NGF_FORCE_INLINE void skipWs()
    {
        while (i < s.length()) {
            char c = s[i];
            if (c == SLASH && i + 1 < s.length() && s[i + 1] == SLASH) {
                auto* ptr = static_cast<const void*>(s.data() + i);
                auto* nl = static_cast<const char*>(std::memchr(ptr, LF, s.length() - i));
                if (nl)
                    i = static_cast<size_t>(nl - s.data()) + 1;
                else
                    i = s.length();
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
#if NGF_HAS_SSE2
                size_t remaining = s.length() - i;
                if (remaining >= 16) {
                    size_t skip = simdSkipWs(s.data() + i, remaining);
                    i += skip;
                }
#endif
                while (i < s.length() && kCharTable.isWs(s[i]))
                    ++i;
                continue;
            }
            break;
        }
    }

    NGF_FORCE_INLINE void consume(char c)
    {
        skipWs();
        if (i >= s.length() || s[i] != c)
            throw parseError(s, i, std::string(1, c));
        ++i;
    }

    void parseRoot(GraphNode& root)
    {
        skipWs();
        if (i < s.length() && s[i] == OPENBRACE) {
            parseObjectBody(root);
            return;
        }

        auto start = i;
        if (i < s.length() && !kCharTable.isNumStart(s[i]) && s[i] != QUOTE && s[i] != OPENSQUARE && s[i] != AT) {
            auto name = parseIdentifier();
            skipWs();
            std::string_view quotedName;
            if (i < s.length() && s[i] == QUOTE) {
                quotedName = parseQuotedName();
                skipWs();
            }
            if (i < s.length() && s[i] == OPENBRACE && (name.empty() || name[0] != AT)) {
                root.className = name;
                root.name = quotedName;
                parseObjectBody(root);
                return;
            }
            i = start;
        }

        parseElements(root);
        skipWs();
        if (i != s.length())
            throw parseError(s, i, "end-of-input");
    }

    void parseElements(GraphNode& node)
    {
        while (true) {
            skipWs();
            if (i >= s.length() || s[i] == CLOSEBRACE)
                break;
            parseElement(node);
        }
    }

    void parseElement(GraphNode& node)
    {
        skipWs();

        // Support quoted property names: "name" = value
        std::string_view ident;
        if (i < s.length() && s[i] == QUOTE) {
            ident = parseString().asString();
        } else {
            ident = parseIdentifier();
        }

        if (ident.empty()) {
            if (i < s.length())
                throw parseError(s, i, "identifier");
            return;
        }

        skipWs();

        std::string_view quotedName;
        if (i < s.length() && s[i] == QUOTE) {
            quotedName = parseQuotedName();
            skipWs();
        }

        // Source property
        if (!ident.empty() && ident[0] == AT) {
            if (i < s.length() && s[i] == EQUALS) {
                ++i;
                skipWs();
            }
            if (i >= s.length() || s[i] != OPENBRACE)
                throw parseError(s, i, "'{' for source block");
            auto content = parseSource();
            Property prop;
            prop.name = ident;
            prop.label = quotedName;
            prop.value = NodeValue::makeSource(content);
            node.properties.push_back(std::move(prop));
            node.mIndexDirty = true;
            return;
        }

        // Child node
        if (i < s.length() && s[i] == OPENBRACE) {
            auto* child = mPool->create();
            child->className = ident;
            child->name = quotedName;
            parseObjectBody(*child);
            node.children.push_back(child);
            return;
        }

        // Property with = sign
        if (i < s.length() && s[i] == EQUALS) {
            ++i;
            Property prop;
            prop.name = ident;
            prop.value = parseValue();
            node.properties.push_back(std::move(prop));
            node.mIndexDirty = true;
            return;
        }

        // Bare quoted name as string property value: key "value" (no =)
        if (!quotedName.empty()) {
            Property prop;
            prop.name = ident;
            prop.value = NodeValue::makeString(quotedName);
            node.properties.push_back(std::move(prop));
            node.mIndexDirty = true;
            return;
        }

        if (i >= s.length() || s[i] != EQUALS)
            throw parseError(s, i, "'='");
    }

    void parseObjectBody(GraphNode& node)
    {
        if (mDepth >= kMaxDepth)
            throw parseError(s, i, "max nesting depth exceeded");
        ++mDepth;
        consume(OPENBRACE);
        parseElements(node);
        consume(CLOSEBRACE);
        --mDepth;
    }

    // Peek ahead to determine if this is an object array block or inline object.
    // Object array block: { TypeName { } TypeName { } } or { { } { } }
    // Inline object: { name = value }
    // Returns: 0 = inline object, 1 = typed object array, 2 = untyped object array
    int arrayBlockKind(std::string_view text, size_t pos)
    {
        // Skip whitespace
        while (pos < text.length() && kCharTable.isWs(text[pos]))
            ++pos;
        if (pos >= text.length() || text[pos] == CLOSEBRACE)
            return 0;

        // Check for untyped object array: starts with {
        if (text[pos] == OPENBRACE)
            return 2;

        // Check if first non-whitespace is an identifier (potential TypeName)
        size_t start = pos;
        while (pos < text.length() && !kCharTable.isIdTerm(text[pos]))
            ++pos;
        if (pos == start)
            return 0; // No identifier found

        // Skip whitespace after identifier
        while (pos < text.length() && kCharTable.isWs(text[pos]))
            ++pos;

        // Skip quoted name if present: ClassName "name"
        if (pos < text.length() && text[pos] == QUOTE) {
            ++pos;
            while (pos < text.length()) {
                if (text[pos] == BACKSLASH && pos + 1 < text.length()) {
                    pos += 2;
                    continue;
                }
                if (text[pos] == QUOTE)
                    break;
                ++pos;
            }
            if (pos >= text.length())
                return 0;
            ++pos;
            while (pos < text.length() && kCharTable.isWs(text[pos]))
                ++pos;
        }

        // Check if followed by { - this indicates ClassName { } pattern
        if (pos < text.length() && text[pos] == OPENBRACE)
            return 1;

        return 0;
    }

    NodeValue parseValue()
    {
        skipWs();
        if (i >= s.length())
            throw parseError(s, i, "value");

        char c = s[i];
        if (kCharTable.isNumStart(c))
            return parseNumber();
        if (c == OPENBRACE) {
            ++i; // consume OPENBRACE
            skipWs();

            // Peek to determine if this is object array block or inline object
            int kind = arrayBlockKind(s, i);
            if (kind == 1) {
                return parseTypedObjectArrayBlock();
            } else if (kind == 2) {
                return parseUntypedObjectArrayBlock();
            } else {
                // Inline object - parse remaining elements
                auto* obj = mPool->create();
                parseElements(*obj);
                consume(CLOSEBRACE);
                return NodeValue::makeObject(obj);
            }
        }
        if (c == OPENSQUARE)
            return parseArray();
        if (c == QUOTE)
            return parseString();

        if (c == 't' && tryKeyword("true"))
            return NodeValue::makeBool(true);
        if (c == 'f' && tryKeyword("false"))
            return NodeValue::makeBool(false);
        if (c == 'n' && tryKeyword("null"))
            return NodeValue::makeNull();

        // Bare identifier - could be TypeName { } for typed inline object
        auto ident = parseIdentifier();
        if (ident.empty())
            throw parseError(s, i, "value");

        skipWs();

        // Check for TypeName { } syntax (typed inline object)
        if (i < s.length() && s[i] == OPENBRACE) {
            auto* obj = mPool->create();
            obj->className = ident;
            parseObjectBody(*obj);
            return NodeValue::makeObject(obj);
        }

        // Otherwise treat as bare string value
        return NodeValue::makeString(ident);
    }

    // Parse typed object array block: { ClassName { } ClassName { } }
    // Note: OPENBRACE has already been consumed by parseValue()
    NodeValue parseTypedObjectArrayBlock()
    {
        auto v = NodeValue::makeList();
        v.list.reserve(8);

        while (true) {
            skipWs();
            if (i >= s.length() || s[i] == CLOSEBRACE)
                break;

            // Parse ClassName "name" { }
            auto className = parseIdentifier();
            if (className.empty())
                throw parseError(s, i, "ClassName");

            skipWs();

            std::string_view quotedName;
            if (i < s.length() && s[i] == QUOTE) {
                quotedName = parseQuotedName();
                skipWs();
            }

            // OPENBRACE must be present (not yet consumed)
            if (i >= s.length() || s[i] != OPENBRACE)
                throw parseError(s, i, "'{' for array element");

            auto* child = mPool->create();
            child->className = className;
            child->name = quotedName;
            parseObjectBody(*child); // This will consume OPENBRACE and CLOSEBRACE
            v.list.push_back(NodeValue::makeObject(child));

            skipWs();
            if (i < s.length() && s[i] == COMMA)
                ++i;
        }

        consume(CLOSEBRACE); // Consume the outer CLOSEBRACE
        return v;
    }

    // Parse untyped object array block: { { } { } }
    // Note: OPENBRACE has already been consumed by parseValue()
    NodeValue parseUntypedObjectArrayBlock()
    {
        auto v = NodeValue::makeList();
        v.list.reserve(8);

        while (true) {
            skipWs();
            if (i >= s.length() || s[i] == CLOSEBRACE)
                break;

            // Each element is { ... }
            if (i >= s.length() || s[i] != OPENBRACE)
                throw parseError(s, i, "'{' for untyped array element");

            auto* child = mPool->create();
            parseObjectBody(*child);
            v.list.push_back(NodeValue::makeObject(child));

            skipWs();
            if (i < s.length() && s[i] == COMMA)
                ++i;
        }

        consume(CLOSEBRACE); // Consume the outer CLOSEBRACE
        return v;
    }

    NodeValue parseNumber()
    {
        size_t start = i;
        bool isFloat = false;

        // SIMD fast path: scan 16 bytes at a time for number chars
        const char* base = s.data();
        size_t len = s.length();
#if NGF_HAS_SSE2
        {
            const __m128i dot = _mm_set1_epi8('.');
            const __m128i e_lo = _mm_set1_epi8('e');
            const __m128i e_hi = _mm_set1_epi8('E');
            const __m128i plus = _mm_set1_epi8('+');
            const __m128i minus = _mm_set1_epi8('-');
            const __m128i zeroMinus1 = _mm_set1_epi8('0' - 1);
            const __m128i ninePlus1 = _mm_set1_epi8('9' + 1);

            bool scanning = true;
            while (scanning && i + 16 <= len) {
                __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(base + i));
                // Check if char is digit: c >= '0' && c <= '9' (using signed gt)
                __m128i geZero = _mm_cmpgt_epi8(chunk, zeroMinus1); // c >= '0'
                __m128i leNine = _mm_cmpgt_epi8(ninePlus1, chunk);   // c <= '9'
                __m128i isDigit = _mm_and_si128(geZero, leNine);
                // Check for special chars
                __m128i isDot = _mm_cmpeq_epi8(chunk, dot);
                __m128i isE = _mm_or_si128(_mm_cmpeq_epi8(chunk, e_lo), _mm_cmpeq_epi8(chunk, e_hi));
                __m128i isSign = _mm_or_si128(_mm_cmpeq_epi8(chunk, plus), _mm_cmpeq_epi8(chunk, minus));
                // Valid number char = digit | dot | e/E | +/-
                __m128i valid = _mm_or_si128(_mm_or_si128(isDigit, isDot), _mm_or_si128(isE, isSign));
                unsigned mask = _mm_movemask_epi8(valid);
                if (mask != 0xFFFF) {
                    // Found non-number char - find its position
                    unsigned notMask = (~mask) & 0xFFFF;
                    size_t offset = (size_t)bsf(notMask);
                    // Scan the valid part for float indicators
                    for (size_t k = 0; k < offset; ++k) {
                        char c = base[i + k];
                        if (c == '.' || c == 'e' || c == 'E') isFloat = true;
                    }
                    i += offset;
                    scanning = false;
                    break;
                }
                // Check for float indicators in this chunk
                unsigned dotMask = _mm_movemask_epi8(isDot);
                unsigned eMask = _mm_movemask_epi8(isE);
                if (dotMask || eMask) isFloat = true;
                i += 16;
            }
        }
#endif
        // Scalar fallback for remaining bytes
        for (; i < s.length(); ++i) {
            char c = s[i];
            if (c >= '0' && c <= '9')
                continue;
            if (c == '.' || c == 'e' || c == 'E') {
                isFloat = true;
                continue;
            }
            if ((c == '+' || c == '-') && i > start
                && (s[i - 1] == 'e' || s[i - 1] == 'E')) {
                continue;
            }
            if (c == '-' && i == start)
                continue;
            break;
        }

        const char* data = s.data() + start;
        size_t numLen = i - start;
        const char* end = data + numLen;

        auto validateNumber = [](const char* p, const char* end, bool isFloat) {
            if (p == end)
                return false;
            if (*p == '-')
                ++p;

            bool hasDigits = false;
            while (p < end && *p >= '0' && *p <= '9') {
                hasDigits = true;
                ++p;
            }

            if (p < end && *p == '.') {
                isFloat = true;
                ++p;
                while (p < end && *p >= '0' && *p <= '9') {
                    hasDigits = true;
                    ++p;
                }
            }

            if (!hasDigits)
                return false;

            if (p < end && (*p == 'e' || *p == 'E')) {
                isFloat = true;
                ++p;
                if (p < end && (*p == '+' || *p == '-'))
                    ++p;

                bool hasExponentDigits = false;
                while (p < end && *p >= '0' && *p <= '9') {
                    hasExponentDigits = true;
                    ++p;
                }
                if (!hasExponentDigits)
                    return false;
            }

            return p == end;
        };

        if (isFloat) {
            if (!validateNumber(data, end, true))
                throw parseError(s, start, "valid float");

            double result = 0.0;
            auto [ptr, ec] = std::from_chars(data, end, result, std::chars_format::general);
            if (ec != std::errc() || ptr != end || !std::isfinite(result))
                throw parseError(s, start, "valid float");
            return NodeValue::makeFloat(result);
        }

        bool neg = false;
        const char* p = data;
        if (*p == '-') {
            neg = true;
            ++p;
        }
        if (p == end)
            throw parseError(s, start, "valid integer");

        const uint64_t limit = neg
            ? static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1
            : static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
        uint64_t value = 0;
        while (p < end) {
            if (*p < '0' || *p > '9')
                throw parseError(s, start, "valid integer");
            uint64_t digit = static_cast<uint64_t>(*p - '0');
            if (value > (limit - digit) / 10)
                throw parseError(s, start, "valid integer");
            value = value * 10 + digit;
            ++p;
        }

        if (neg) {
            if (value == limit)
                return NodeValue::makeInt(std::numeric_limits<int64_t>::min());
            return NodeValue::makeInt(-static_cast<int64_t>(value));
        }
        return NodeValue::makeInt(static_cast<int64_t>(value));
    }

    // Parse a quoted string. Returns zero-copy string_view for unescaped
    // strings, or allocates into the pool for escaped strings.
    NodeValue parseString()
    {
        // Triple-quoted multi-line string
        if (i + 2 < s.length() && s[i] == QUOTE && s[i + 1] == QUOTE && s[i + 2] == QUOTE) {
            i += 3;
            size_t start = i;
            while (i + 2 < s.length() && !(s[i] == QUOTE && s[i + 1] == QUOTE && s[i + 2] == QUOTE))
                ++i;
            size_t end = i;
            if (i + 2 >= s.length()) {
                i = s.length();
                throw parseError(s, start, "closing triple quote");
            }
            i += 3;
            return NodeValue::makeString(s.substr(start, end - start));
        }

        // Regular quoted string
        ++i; // skip opening quote
        size_t start = i;

        // SIMD fast path: scan for quote or backslash
        const char* base = s.data();
        size_t len = s.length();

#if NGF_HAS_SSE2
        while (i + 16 <= len) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(base + i));
            __m128i qMask = _mm_cmpeq_epi8(chunk, _mm_set1_epi8(QUOTE));
            __m128i bMask = _mm_cmpeq_epi8(chunk, _mm_set1_epi8(BACKSLASH));
            __m128i combined = _mm_or_si128(qMask, bMask);
            unsigned mask = _mm_movemask_epi8(combined);
            if (mask != 0) {
                size_t offset = (size_t)bsf(mask);
                char foundChar = base[i + offset];
                i += offset;
                if (foundChar == QUOTE) {
                    // Found closing quote - zero copy
                    auto result = s.substr(start, i - start);
                    ++i;
                    return NodeValue::makeString(result);
                }
                // Found backslash - need slow path
                break;
            }
            i += 16;
        }
#endif
        // Scalar fallback for remaining bytes
        while (i < len) {
            const char* data = base + i;
            size_t remaining = len - i;
            auto* quotePos = static_cast<const char*>(std::memchr(data, QUOTE, remaining));
            auto* bsPos = static_cast<const char*>(std::memchr(data, BACKSLASH, remaining));

            if (!quotePos) {
                i = len;
                throw parseError(s, start, "closing '\"'");
            }

            if (!bsPos || bsPos > quotePos) {
                // No escape before the quote - zero copy
                i = static_cast<size_t>(quotePos - base);
                auto result = s.substr(start, i - start);
                ++i;
                return NodeValue::makeString(result);
            }

            // Found a backslash before the quote - need slow path with allocation
            break;
        }

        // Slow path: process escapes into an owned string
        std::string result;
        result.reserve(s.length() - start);
        i = start;
        for (; i < s.length() && s[i] != QUOTE; ++i) {
            if (s[i] == BACKSLASH && i + 1 < s.length()) {
                ++i;
                switch (s[i]) {
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'u': {
                    if (i + 4 >= s.length())
                        throw parseError(s, i, "4 hex digits for \\u escape");
                    uint32_t cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        int v = hexDigitVal(s[i + 1 + k]);
                        if (v < 0)
                            throw parseError(s, i + 1 + k, "hex digit");
                        cp = (cp << 4) | static_cast<uint32_t>(v);
                    }
                    i += 4;
                    appendUtf8(result, cp);
                    break;
                }
                default: result += s[i]; break;
                }
            } else {
                result += s[i];
            }
        }
        if (i >= s.length())
            throw parseError(s, start, "closing '\"'");
        ++i;
        return NodeValue::makeString(mPool->allocString(result));
    }

    // Parse a quoted name (for className "name" { } syntax).
    // Returns zero-copy string_view for unescaped, pool-allocated for escaped.
    std::string_view parseQuotedName()
    {
        ++i; // skip opening quote
        size_t start = i;

        while (i < s.length()) {
            const char* data = s.data() + i;
            size_t remaining = s.length() - i;
            auto* quotePos = static_cast<const char*>(std::memchr(data, QUOTE, remaining));
            auto* bsPos = static_cast<const char*>(std::memchr(data, BACKSLASH, remaining));

            if (!quotePos) {
                i = s.length();
                throw parseError(s, start, "closing '\"' for quoted name");
            }

            if (!bsPos || bsPos > quotePos) {
                i = static_cast<size_t>(quotePos - s.data());
                auto result = s.substr(start, i - start);
                ++i;
                return result;
            }

            break;
        }

        // Slow path: escaped name
        std::string result;
        result.reserve(s.length() - start);
        i = start;
        for (; i < s.length() && s[i] != QUOTE; ++i) {
            if (s[i] == BACKSLASH && i + 1 < s.length()) {
                ++i;
                switch (s[i]) {
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'u': {
                    if (i + 4 >= s.length())
                        throw parseError(s, i, "4 hex digits for \\u escape");
                    uint32_t cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        int v = hexDigitVal(s[i + 1 + k]);
                        if (v < 0)
                            throw parseError(s, i + 1 + k, "hex digit");
                        cp = (cp << 4) | static_cast<uint32_t>(v);
                    }
                    i += 4;
                    appendUtf8(result, cp);
                    break;
                }
                default: result += s[i]; break;
                }
            } else {
                result += s[i];
            }
        }
        if (i >= s.length())
            throw parseError(s, start, "closing '\"' for quoted name");
        ++i;
        return mPool->allocString(result);
    }

    std::string_view parseSource()
    {
        ++i; // skip '{'
        size_t start = i;
        int depth = 1;
        for (; i < s.length(); ++i) {
            if (s[i] == SLASH && i + 1 < s.length() && s[i + 1] == SLASH) {
                i += 2;
                while (i < s.length() && s[i] != LF)
                    ++i;
                if (i >= s.length())
                    break;
            } else if (s[i] == SLASH && i + 1 < s.length() && s[i + 1] == '*') {
                i += 2;
                while (i + 1 < s.length() && !(s[i] == '*' && s[i + 1] == SLASH))
                    ++i;
                if (i + 1 < s.length())
                    ++i;
            } else if (s[i] == QUOTE || s[i] == '\'') {
                char quote = s[i];
                ++i;
                while (i < s.length()) {
                    if (s[i] == BACKSLASH && i + 1 < s.length()) {
                        i += 2;
                        continue;
                    }
                    if (s[i] == quote)
                        break;
                    ++i;
                }
            } else if (s[i] == OPENBRACE) {
                ++depth;
            } else if (s[i] == CLOSEBRACE) {
                --depth;
                if (depth == 0) {
                    size_t end = i;
                    ++i;
                    return s.substr(start, end - start);
                }
            }
        }
        throw parseError(s, s.length(), "'}' to close source block");
    }

    NodeValue parseArray()
    {
        if (mDepth >= kMaxDepth)
            throw parseError(s, i, "max nesting depth exceeded");
        ++mDepth;
        ++i; // skip '['
        auto v = NodeValue::makeList();
        v.list.reserve(8);
        while (true) {
            skipWs();
            if (i >= s.length())
                throw parseError(s, i, "']'");
            if (s[i] == CLOSESQUARE)
                break;
            v.list.push_back(parseValue());
            skipWs();
            if (i < s.length() && s[i] == COMMA)
                ++i;
        }
        ++i; // skip ']'
        --mDepth;
        return v;
    }

    NGF_FORCE_INLINE std::string_view parseIdentifier()
    {
        skipWs();
        if (i >= s.length())
            return {};
        size_t start = i;
#if NGF_HAS_SSE2
        size_t remaining = s.length() - i;
        if (remaining >= 16) {
            size_t skip = simdFindIdTerm(s.data() + i, remaining);
            i += skip;
        }
#endif
        while (i < s.length() && !kCharTable.isIdTerm(s[i]))
            ++i;
        return s.substr(start, i - start);
    }

    bool tryKeyword(std::string_view kw)
    {
        if (i + kw.length() > s.length())
            return false;
        if (std::memcmp(s.data() + i, kw.data(), kw.length()) != 0)
            return false;
        size_t after = i + kw.length();
        if (after < s.length() && !kCharTable.isIdTerm(s[after]) && s[after] != CLOSEBRACE && s[after] != CLOSESQUARE && s[after] != COMMA)
            return false;
        i += kw.length();
        return true;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Serializer - raw buffer based (zero std::string allocation)
// ---------------------------------------------------------------------------

namespace {

static constexpr int kMaxSerializeDepth = 256;

// Output buffer for zero-allocation serialization
struct DumpBuffer {
    char* buf = nullptr;
    char* cursor = nullptr;
    char* end = nullptr;
    size_t cap = 0;

    static constexpr size_t kInitCap = 4096;

    DumpBuffer() = default;
    ~DumpBuffer() { std::free(buf); }

    DumpBuffer(const DumpBuffer&) = delete;
    DumpBuffer& operator=(const DumpBuffer&) = delete;

    // Pre-allocate buffer based on estimated output size
    void preSize(size_t estimated)
    {
        if (estimated <= cap) return;
        size_t used = buf ? static_cast<size_t>(cursor - buf) : 0;
        auto* newBuf = static_cast<char*>(std::malloc(estimated));
        if (!newBuf) return;
        if (buf) {
            memcpy(newBuf, buf, used);
            std::free(buf);
        }
        buf = newBuf;
        cursor = buf + used;
        end = buf + estimated;
        cap = estimated;
    }

    NGF_FORCE_INLINE void ensureCapacity(size_t needed)
    {
        if (buf && cursor + needed <= end) return;
        growCapacity(needed);
    }

    void growCapacity(size_t needed)
    {
        size_t used = buf ? static_cast<size_t>(cursor - buf) : 0;
        size_t newCap = cap < kInitCap ? kInitCap : cap;
        while (newCap < used + needed) newCap *= 2;

        auto* newBuf = static_cast<char*>(std::realloc(buf, newCap));
        if (!newBuf) {
            newBuf = static_cast<char*>(std::malloc(newCap));
            if (newBuf) {
                if (buf) memcpy(newBuf, buf, used);
                std::free(buf);
            }
        }
        buf = newBuf;
        cursor = buf + used;
        end = buf + newCap;
        cap = newCap;
    }

    NGF_FORCE_INLINE void write(char c)
    {
        ensureCapacity(1);
        *cursor++ = c;
    }

    NGF_FORCE_INLINE void write(const char* data, size_t len)
    {
        ensureCapacity(len);
        memcpy(cursor, data, len);
        cursor += len;
    }

    NGF_FORCE_INLINE void writeIndent(int level)
    {
        static constexpr char kSpaces[] = "                                                                ";
        size_t total = static_cast<size_t>(level) * 4;
        while (total > 64) {
            write(kSpaces, 64);
            total -= 64;
        }
        if (total > 0) {
            write(kSpaces, total);
        }
    }

    std::string toString()
    {
        size_t len = static_cast<size_t>(cursor - buf);
        std::string result(buf, len);
        cursor = buf; // reset for potential reuse
        return result;
    }
};

// Custom integer-to-string (avoids std::to_chars overhead)
NGF_FORCE_INLINE size_t i64ToChars(int64_t val, char* buf)
{
    if (val == 0) { buf[0] = '0'; return 1; }

    bool neg = val < 0;
    uint64_t uval = neg ? static_cast<uint64_t>(-(val + 1)) + 1 : static_cast<uint64_t>(val);

    char tmp[20];
    int pos = 0;
    while (uval > 0) {
        tmp[pos++] = '0' + static_cast<char>(uval % 10);
        uval /= 10;
    }

    size_t outPos = 0;
    if (neg) buf[outPos++] = '-';
    for (int k = pos - 1; k >= 0; --k) {
        buf[outPos++] = tmp[k];
    }
    return outPos;
}

// Double-to-string with explicit float marker to preserve NodeValue::Float on round-trip.
size_t f64ToChars(double val, char* buf)
{
    if (!std::isfinite(val)) {
        memcpy(buf, "0.0", 3);
        return 3;
    }

    auto [ptr, ec] = std::to_chars(buf, buf + 64, val, std::chars_format::general);
    size_t len = 0;
    if (ec == std::errc()) {
        len = static_cast<size_t>(ptr - buf);
    } else {
        int written = snprintf(buf, 64, "%.17g", val);
        len = written > 0 ? static_cast<size_t>(written) : 0;
    }

    bool hasFloatMarker = false;
    for (size_t i = 0; i < len; ++i) {
        char c = buf[i];
        if (c == '.' || c == 'e' || c == 'E') {
            hasFloatMarker = true;
            break;
        }
    }
    if (!hasFloatMarker && len + 2 < 64) {
        buf[len++] = '.';
        buf[len++] = '0';
    }
    return len;
}

// Escape-check lookup table for dump
static constexpr bool kNeedsEscape[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  // 0x00-0x0F
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  // 0x10-0x1F
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x20-0x2F  (0x22 = '"')
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x30-0x3F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x40-0x4F
    0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,  // 0x50-0x5F  (0x5C = '\')
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x60-0x6F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x70-0x7F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

NGF_FORCE_INLINE void escapeAndWrite(std::string_view sv, DumpBuffer& out)
{
    // Fast path: check if any escaping needed
    bool needsEscape = false;
    for (size_t k = 0; k < sv.size(); ++k) {
        if (kNeedsEscape[static_cast<unsigned char>(sv[k])]) {
            // Also check for \n, \r, \t, \b, \f
            char c = sv[k];
            if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t' || c == '\b' || c == '\f' || static_cast<unsigned char>(c) < 0x20) {
                needsEscape = true;
                break;
            }
        }
    }
    if (!needsEscape) {
        out.write(sv.data(), sv.size());
        return;
    }

    // Slow path: escape and write
    for (char c : sv) {
        switch (c) {
        case '\\': out.write("\\\\", 2); break;
        case '"':  out.write("\\\"", 2); break;
        case '\n': out.write("\\n", 2); break;
        case '\r': out.write("\\r", 2); break;
        case '\t': out.write("\\t", 2); break;
        case '\b': out.write("\\b", 2); break;
        case '\f': out.write("\\f", 2); break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                out.write(buf, 6);
            } else {
                out.write(c);
            }
            break;
        }
    }
}

void serializeValue(const NodeValue& v, int indent, DumpBuffer& out);

void serializeObjectBody(const GraphNode& node, int indent, DumpBuffer& out)
{
    if (indent >= kMaxSerializeDepth)
        return;
    for (const auto& prop : node.properties) {
        out.writeIndent(indent);
        if (!prop.name.empty() && prop.name[0] == '@' && prop.value.isSource()) {
            out.write(prop.name.data(), prop.name.size());
            if (!prop.label.empty()) {
                out.write(" \"", 2);
                escapeAndWrite(prop.label, out);
                out.write('"');
            }
            out.write(" {", 2);
            auto sv = prop.value.asString();
            out.write(sv.data(), sv.size());
            out.write("}\n", 2);
            continue;
        }
        out.write(prop.name.data(), prop.name.size());
        out.write(" = ", 3);
        if (prop.value.isObject() && prop.value.asObject()) {
            const auto* obj = prop.value.asObject();
            if (!obj->className.empty()) {
                out.write(obj->className.data(), obj->className.size());
                out.write(" {\n", 3);
                serializeObjectBody(*obj, indent + 1, out);
                out.writeIndent(indent);
                out.write("}\n", 2);
            } else {
                out.write("{\n", 2);
                serializeObjectBody(*obj, indent + 1, out);
                out.writeIndent(indent);
                out.write("}\n", 2);
            }
        } else {
            serializeValue(prop.value, indent, out);
            out.write('\n');
        }
    }
    for (const auto* child : node.children) {
        out.writeIndent(indent);
        out.write(child->className.data(), child->className.size());
        if (!child->name.empty()) {
            out.write(" \"", 2);
            escapeAndWrite(child->name, out);
            out.write('"');
        }
        out.write(" {\n", 3);
        serializeObjectBody(*child, indent + 1, out);
        out.writeIndent(indent);
        out.write("}\n", 2);
    }
}

void serializeValue(const NodeValue& v, int indent, DumpBuffer& out)
{
    switch (v.type) {
    case NodeValue::Null:
        out.write("null", 4);
        break;
    case NodeValue::Bool:
        if (v.boolVal)
            out.write("true", 4);
        else
            out.write("false", 5);
        break;
    case NodeValue::Int: {
        char buf[24];
        size_t len = i64ToChars(v.intVal, buf);
        out.write(buf, len);
        break;
    }
    case NodeValue::Float: {
        char buf[64];
        size_t len = f64ToChars(v.floatVal, buf);
        out.write(buf, len);
        break;
    }
    case NodeValue::Str:
        out.write('"');
        escapeAndWrite(v.strVal, out);
        out.write('"');
        break;
    case NodeValue::Source:
        out.write('{');
        out.write(v.strVal.data(), v.strVal.size());
        out.write('}');
        break;
    case NodeValue::List: {
        bool isTypedObjectArray = true;
        bool isUntypedObjectArray = true;
        for (const auto& elem : v.list) {
            if (elem.type != NodeValue::Object || !elem.objectVal) {
                isTypedObjectArray = false;
                isUntypedObjectArray = false;
                break;
            }
            if (elem.objectVal->className.empty())
                isTypedObjectArray = false;
            else
                isUntypedObjectArray = false;
        }

        if (isTypedObjectArray && !v.list.empty()) {
            out.write("{\n", 2);
            for (size_t k = 0; k < v.list.size(); ++k) {
                const auto* child = v.list[k].objectVal;
                out.writeIndent(indent + 1);
                out.write(child->className.data(), child->className.size());
                if (!child->name.empty()) {
                    out.write(" \"", 2);
                    escapeAndWrite(child->name, out);
                    out.write('"');
                }
                out.write(" {\n", 3);
                serializeObjectBody(*child, indent + 2, out);
                out.writeIndent(indent + 1);
                out.write("}\n", 2);
            }
            out.writeIndent(indent);
            out.write('}');
        } else if (isUntypedObjectArray && !v.list.empty()) {
            out.write("{\n", 2);
            for (size_t k = 0; k < v.list.size(); ++k) {
                const auto* child = v.list[k].objectVal;
                out.writeIndent(indent + 1);
                out.write("{\n", 2);
                serializeObjectBody(*child, indent + 2, out);
                out.writeIndent(indent + 1);
                out.write("}\n", 2);
            }
            out.writeIndent(indent);
            out.write('}');
        } else {
            bool simple = true;
            for (const auto& elem : v.list) {
                if (elem.type == NodeValue::Object || elem.type == NodeValue::List || elem.type == NodeValue::Source) {
                    simple = false;
                    break;
                }
            }
            if (simple) {
                out.write('[');
                for (size_t k = 0; k < v.list.size(); ++k) {
                    if (k > 0)
                        out.write(", ", 2);
                    serializeValue(v.list[k], indent, out);
                }
                out.write(']');
            } else {
                out.write("[\n", 2);
                for (const auto& elem : v.list) {
                    out.writeIndent(indent + 1);
                    serializeValue(elem, indent + 1, out);
                    out.write('\n');
                }
                out.writeIndent(indent);
                out.write(']');
            }
        }
        break;
    }
    case NodeValue::Object:
        if (v.objectVal) {
            if (!v.objectVal->className.empty()) {
                out.write(v.objectVal->className.data(), v.objectVal->className.size());
                out.write(" {\n", 3);
                serializeObjectBody(*v.objectVal, indent + 1, out);
                out.writeIndent(indent);
                out.write('}');
            } else {
                out.write("{\n", 2);
                serializeObjectBody(*v.objectVal, indent + 1, out);
                out.writeIndent(indent);
                out.write('}');
            }
        } else {
            out.write("{ }", 3);
        }
        break;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// NodeGraph method definitions
// ---------------------------------------------------------------------------

NodeGraph::NodeGraph() = default;

bool NodeGraph::parse(std::string text)
{
    mSource = std::move(text);
    mRoot = GraphNode {};
    mPool.clear();
    mError.clear();
    NodeParser parser;
    return parser.parse(mSource, mRoot, mPool, mError);
}

bool NodeGraph::parseFile(const std::string& filePath)
{
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream) {
        mError = std::format("Failed to read file: {}", filePath);
        return false;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    auto text = buffer.str();
    return parse(std::move(text));
}

// Estimate output size for pre-allocation
size_t estimateDumpSize(const GraphNode& node)
{
    size_t size = 0;
    for (const auto& prop : node.properties) {
        size += prop.name.size() + 4; // "name = "
        switch (prop.value.type) {
        case NodeValue::Str:
            size += prop.value.strVal.size() + 2;
            break;
        case NodeValue::Int:
            size += 24;
            break;
        case NodeValue::Float:
            size += 32;
            break;
        case NodeValue::Object:
            if (prop.value.objectVal)
                size += estimateDumpSize(*prop.value.objectVal) + 20;
            break;
        case NodeValue::List:
            for (const auto& elem : prop.value.list) {
                if (elem.type == NodeValue::Object && elem.objectVal)
                    size += estimateDumpSize(*elem.objectVal) + 20;
                else
                    size += 32;
            }
            break;
        default:
            size += 8;
            break;
        }
        size += 8; // indent + newline
    }
    for (const auto* child : node.children) {
        size += child->className.size() + child->name.size() + 20;
        size += estimateDumpSize(*child) + 10;
    }
    return size + 64;
}

std::string NodeGraph::dump() const
{
    DumpBuffer out;
    size_t estimated = estimateDumpSize(mRoot) + mRoot.className.size() + mRoot.name.size() + 64;
    out.preSize(estimated);

    if (!mRoot.className.empty()) {
        out.write(mRoot.className.data(), mRoot.className.size());
        if (!mRoot.name.empty()) {
            out.write(" \"", 2);
            escapeAndWrite(mRoot.name, out);
            out.write('"');
        }
        out.write(" {\n", 3);
    } else {
        out.write("{\n", 2);
    }
    serializeObjectBody(mRoot, 1, out);
    out.write("}\n", 2);
    return out.toString();
}

bool NodeGraph::saveFile(const std::string& filePath) const
{
    auto text = dump();
    std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
    if (!stream)
        return false;
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}

const GraphNode& NodeGraph::getRoot() const { return mRoot; }
GraphNode& NodeGraph::getRoot() { return mRoot; }
const std::string& NodeGraph::getError() const { return mError; }

GraphNode* NodeGraph::createNode()
{
    return mPool.create();
}

std::string_view NodeGraph::allocString(std::string_view s)
{
    return mPool.allocString(s);
}

} // namespace node_graph_format

#undef NGF_FORCE_INLINE
