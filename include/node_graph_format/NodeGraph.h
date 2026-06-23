#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace node_graph_format {

struct NodeValue;
struct GraphNode;
class GraphNodePool;

// NodeValue is a compact tagged union holding primitive values, lists, or
// object references. String/Source values store string_view into the source
// buffer (zero-copy for unescaped strings) or into the owned-strings pool
// (for escaped strings that need processing).
struct NodeValue {
    enum Type : uint8_t {
        Null,
        Bool,
        Int,
        Float,
        Str,
        Source,
        List,
        Object
    };

    Type type = Null;
    union {
        bool boolVal;
        int64_t intVal;
        double floatVal;
        std::string_view strVal;        // for Str and Source (zero-copy)
        std::vector<NodeValue> list;    // for List
        GraphNode* objectVal;            // pool-allocated, not owned
    };

    NodeValue() : type(Null) { intVal = 0; }
    ~NodeValue() { destroy(); }

    NodeValue(NodeValue&& other) noexcept;
    NodeValue& operator=(NodeValue&& other) noexcept;
    NodeValue(const NodeValue&) = delete;
    NodeValue& operator=(const NodeValue&) = delete;

    // Factory methods
    static NodeValue makeNull() { return NodeValue(); }
    static NodeValue makeBool(bool v) { NodeValue n; n.type = Bool; n.boolVal = v; return n; }
    static NodeValue makeInt(int64_t v) { NodeValue n; n.type = Int; n.intVal = v; return n; }
    static NodeValue makeFloat(double v) { NodeValue n; n.type = Float; n.floatVal = v; return n; }
    static NodeValue makeString(std::string_view s) { NodeValue n; n.type = Str; n.strVal = s; return n; }
    static NodeValue makeSource(std::string_view s) { NodeValue n; n.type = Source; n.strVal = s; return n; }
    static NodeValue makeList() { NodeValue n; n.type = List; new (&n.list) std::vector<NodeValue>(); return n; }
    static NodeValue makeObject(GraphNode* obj) { NodeValue n; n.type = Object; n.objectVal = obj; return n; }

    void destroy() noexcept;

    bool isNull() const { return type == Null; }
    bool isBool() const { return type == Bool; }
    bool isInt() const { return type == Int; }
    bool isFloat() const { return type == Float; }
    bool isString() const { return type == Str; }
    bool isSource() const { return type == Source; }
    bool isList() const { return type == List; }
    bool isObject() const { return type == Object; }

    bool asBool(bool defaultVal = false) const;
    int64_t asInt(int64_t defaultVal = 0) const;
    double asFloat(double defaultVal = 0.0) const;
    std::string_view asString(std::string_view defaultVal = {}) const;
    const GraphNode* asObject() const;
    GraphNode* asObject();
    const std::vector<NodeValue>* asList() const;
};

// A named property within a GraphNode. All string fields use string_view
// pointing into the source buffer (zero-copy) or the owned-strings pool.
struct Property {
    std::string_view name;
    std::string_view label; // quoted name for source blocks
    NodeValue value;

    Property() = default;
    Property(std::string_view n, NodeValue v);
};

// GraphNode represents an Object/Node in the format. Children are pool-
// allocated raw pointers owned by GraphNodePool (lifetime managed by NodeGraph).
struct GraphNode {
    std::string_view className;
    std::string_view name;
    std::vector<Property> properties;
    std::vector<GraphNode*> children; // pool-allocated, not owned

    // Hash index for O(1) property lookup by name
    mutable std::unordered_map<std::string_view, size_t> mPropertyIndex;
    mutable bool mIndexDirty = true;

    ~GraphNode();
    GraphNode() = default;
    GraphNode(const GraphNode&) = delete;
    GraphNode& operator=(const GraphNode&) = delete;
    GraphNode(GraphNode&& o) noexcept
        : className(o.className)
        , name(o.name)
        , properties(std::move(o.properties))
        , children(std::move(o.children))
        , mIndexDirty(true) // index keys point to old properties memory, must rebuild
    {
    }
    GraphNode& operator=(GraphNode&& o) noexcept
    {
        if (this != &o) {
            className = o.className;
            name = o.name;
            properties = std::move(o.properties);
            children = std::move(o.children);
            mIndexDirty = true;
        }
        return *this;
    }

    void rebuildIndex() const;
    void markIndexDirty() { mIndexDirty = true; }

    const Property* findProperty(std::string_view name) const;
    Property* findProperty(std::string_view name);
    bool hasProperty(std::string_view name) const;

    bool getBool(std::string_view name, bool defaultVal = false) const;
    int64_t getInt(std::string_view name, int64_t defaultVal = 0) const;
    double getFloat(std::string_view name, double defaultVal = 0.0) const;
    std::string_view getString(std::string_view name, std::string_view defaultVal = {}) const;
    const GraphNode* getObject(std::string_view name) const;
    const std::vector<NodeValue>* getList(std::string_view name) const;

    // Find first child node by className
    const GraphNode* findChild(std::string_view className) const;
    GraphNode* findChild(std::string_view className);

    // Find all child nodes by className
    std::vector<const GraphNode*> findChildren(std::string_view className) const;
    std::vector<GraphNode*> findChildren(std::string_view className);

    Property& addProperty(std::string_view name, NodeValue value);
};

// GraphNodePool provides batch allocation for GraphNode objects and owns
// escaped strings that cannot be zero-copied from the source buffer.
class GraphNodePool {
public:
    GraphNodePool() { mChunks.push_back(std::make_unique<Chunk>()); }
    ~GraphNodePool() { clear(); }
    GraphNodePool(const GraphNodePool&) = delete;
    GraphNodePool& operator=(const GraphNodePool&) = delete;

    GraphNode* create();
    // Allocate an owned string (for escaped content) and return a view into it
    std::string_view allocString(std::string_view s);
    void clear();

private:
    static constexpr size_t kChunkSize = 64;
    struct Chunk {
        alignas(GraphNode) unsigned char storage[sizeof(GraphNode) * kChunkSize];
        size_t used = 0;
    };
    std::vector<std::unique_ptr<Chunk>> mChunks;
    std::deque<std::string> mOwnedStrings; // for escaped strings (deque doesn't invalidate references)
};

// NodeGraph owns the parsed tree, the source buffer, and the allocation pool.
// All string_views in the tree point into mSource or mPool.mOwnedStrings.
class NodeGraph {
public:
    NodeGraph();

    // Takes ownership of the input text by move. All string_views in the
    // parsed tree point into this buffer, so it must outlive the tree.
    bool parse(std::string text);
    bool parseFile(const std::string& filePath);

    std::string dump() const;
    bool saveFile(const std::string& filePath) const;

    const GraphNode& getRoot() const;
    GraphNode& getRoot();
    const std::string& getError() const;

    GraphNode* createNode();
    std::string_view allocString(std::string_view s);

private:
    std::string mSource; // owns the input text; string_views point here
    GraphNode mRoot;
    GraphNodePool mPool;
    std::string mError;
};

} // namespace node_graph_format
