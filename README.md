# node-graph-format

`node-graph-format` is a small C++20 library for reading and writing a human-friendly object graph text format.
It is designed for configuration files, scene/prefab-like data, shader definitions, and other data that benefits from
a concise DSL instead of plain JSON.

The library is standalone and only depends on the C++ standard library.

## Why NodeGraph?

NodeGraph stores data as a tree of typed nodes. A node can have:

- a class name, such as `Scene`, `Node`, `Camera`, or `Shader`
- an optional quoted name, such as `Node "Player"`
- named properties
- nested child nodes

This makes it useful for formats where structure matters as much as key-value data:

```ngf
Scene "MainScene" {
    Active = true
    Gravity = [0.0, -9.8, 0.0]

    Node "Player" {
        Position = [10.0, 0.0, 5.0]

        Components = {
            Camera {
                Fov = 60.0
            }
            Light {
                Intensity = 1.0
                Color = [1.0, 0.95, 0.8]
            }
        }
    }
}
```

## Features

- Standalone C++20 library with no engine or framework dependency.
- Human-readable object graph syntax.
- Typed values: `null`, `bool`, `int64`, `double`, strings, source blocks, lists, and objects.
- Named and typed nodes: `Shader "main" { ... }`.
- Direct child nodes for DSL-style hierarchy.
- Inline objects and typed inline objects.
- Object array blocks for compact component/resource lists.
- Raw source blocks for shader code or embedded scripts.
- Triple-quoted multiline strings.
- Escaped strings and quoted property names.
- Line/column parse errors with a nearby snippet.
- Round-trippable `parse()` and `dump()` workflow.
- Zero-copy string views for unescaped parsed strings.
- Pool allocation for parsed graph nodes.

## Basic Syntax

### Root Object

A file can be an anonymous object:

```ngf
{
    value = 42
    name = "example"
}
```

Or a typed root node:

```ngf
Shader "main" {
    technique = "default"
}
```

### Properties

Properties use `name = value`:

```ngf
{
    intValue = 42
    floatValue = 3.14
    boolValue = true
    stringValue = "hello"
    emptyValue = null
}
```

Property names can be quoted when they contain characters that are not valid identifier characters:

```ngf
{
    "display name" = "Player"
}
```

### Comments

Line comments are supported:

```ngf
{
    // This is a comment.
    value = 42 // Inline comment.
}
```

### Strings

Regular strings support common escape sequences:

```ngf
{
    path = "C:\\temp\\file.txt"
    text = "line1\nline2\ttabbed"
}
```

Triple-quoted strings are useful for multiline text:

```ngf
{
    description = """First line
Second line
Third line"""
}
```

### Arrays

Simple arrays use `[]`:

```ngf
{
    position = [1.0, 2.0, 3.0]
    tags = ["player", "hero"]
    flags = [true, false, true]
}
```

Arrays can also contain objects:

```ngf
{
    locations = [
        { x = 1 y = 2 z = 3 }
        { x = 4 y = 5 z = 6 }
    ]
}
```

Arrays can use table syntax when every row has the same shape. The first line starts with `#ClassName`
followed by column names, and each following row supplies one value per column:

```ngf
{
    players = [
        #Player id name position meta
        1 "name 1" [4, 5, 6] { x = 1 y = 2 }
        2 "name 2" [7, 8, 9] { x = 3 y = 4 }
    ]
}
```

This parses as a list of `Player` objects with `id`, `name`, `position`, and `meta` properties. Cell values
use the normal value parser, so inline objects, arrays, and nested tables are valid:

```ngf
{
    teams = [
        #Team id players
        1 [
            #Player id name
            10 "name 10"
            11 "name 11"
        ]
    ]
}
```

`dump()` preserves parsed table arrays as table arrays when the rows still have the same schema. Simple arrays,
including nested primitive arrays, are emitted inline.

### Inline Objects

An inline object can be anonymous:

```ngf
{
    size = { width = 1920 height = 1080 }
    meta = { x = 1 y = 2 z = 3 }
}
```

When parsed from a single line, `dump()` preserves object values as inline values where possible.

Or typed:

```ngf
{
    transform = Transform {
        position = [0.0, 0.0, 0.0]
        rotation = [0.0, 0.0, 0.0, 1.0]
    }
}
```

### Child Nodes

NodeGraph supports DSL-style children directly inside a node:

```ngf
Scene "Main" {
    Node "Player" {
        Position = [0.0, 0.0, 0.0]

        Node "Weapon" {
            Visible = true
        }
    }
}
```

These child nodes are stored separately from properties.

### Object Array Blocks

Object array blocks are useful when a property should hold a list of typed objects:

```ngf
Node "Player" {
    Components = {
        Camera {
            Fov = 60.0
        }
        Light {
            Intensity = 1.0
        }
    }
}
```

Object array blocks can also contain named objects:

```ngf
{
    Children = {
        Node "Player" {
            Position = [10.0, 0.0, 5.0]
        }
        Node "Enemy" {
            Position = [20.0, 0.0, 0.0]
        }
    }
}
```

And anonymous object elements:

```ngf
{
    tags = {
        {
            key = "value1"
        }
        {
            key = "value2"
        }
    }
}
```

### Source Blocks

Properties starting with `@` can store raw source text:

```ngf
Shader "main" {
    @vertex "glsl" {
        void main() {
            gl_Position = vec4(1.0);
        }
    }

    @fragment "glsl" {
        void main() {
        }
    }
}
```

The optional quoted string after the source property is stored as a label. This is useful for language tags such as
`"glsl"`, `"hlsl"`, or `"lua"`.

The parser tracks braces inside source blocks and ignores braces in strings and comments:

```ngf
{
    @fragment {
        const char* text = "}";
        // }
        /* } */
        void main() {
            if (true) {
            }
        }
    }
}
```

The following equivalent form is also accepted:

```ngf
{
    @vertex = {
        void main() {}
    }
}
```

## C++ Usage

```cpp
#include "node_graph_format/NodeGraph.h"

#include <iostream>

int main()
{
    ng::NodeGraph graph;

    if (!graph.parse(R"(Scene "Main" {
        Active = true
        Node "Player" {
            Position = [1.0, 2.0, 3.0]
        }
    })")) {
        std::cerr << graph.getError() << "\n";
        return 1;
    }

    const auto& root = graph.getRoot();
    std::cout << root.className << ": " << root.name << "\n";
    std::cout << "active: " << root.getBool("Active") << "\n";

    if (const auto* player = root.findChild("Node")) {
        std::cout << "child: " << player->name << "\n";
    }

    std::string text = graph.dump();
    std::cout << text;
}
```

### Loading and Saving Files

```cpp
ng::NodeGraph graph;

if (!graph.parseFile("scene.ngf")) {
    throw std::runtime_error(graph.getError());
}

graph.saveFile("scene.out.ngf");
```

### Common Accessors

```cpp
const auto& root = graph.getRoot();

bool enabled = root.getBool("Enabled", true);
int64_t count = root.getInt("Count", 0);
double scale = root.getFloat("Scale", 1.0);
std::string_view name = root.getString("Name", "Unnamed");

const ng::GraphNode* object = root.getObject("Transform");
const std::vector<ng::NodeValue>* list = root.getList("Position");
const ng::Property* prop = root.findProperty("@vertex");
```

## CMake Integration

Add the directory and link the target:

```cmake
add_subdirectory(node-graph-format)

target_link_libraries(my_target PRIVATE node_graph_format)
```

Then include the public header:

```cpp
#include "node_graph_format/NodeGraph.h"
```

The target requires C++20:

```cmake
target_compile_features(my_target PRIVATE cxx_std_20)
```

## Data Model

The parsed tree is represented by:

- `NodeGraph`: owns the source text, root node, allocation pool, and parse error string.
- `GraphNode`: stores `className`, optional `name`, properties, and child nodes.
- `Property`: stores a property name, optional label, and `NodeValue`.
- `NodeValue`: stores one of `Null`, `Bool`, `Int`, `Float`, `Str`, `Source`, `List`, or `Object`.

String values are exposed as `std::string_view`. Unescaped strings and identifiers usually point directly into the
owned source buffer. Escaped strings are stored in an internal owned string pool.

Because parsed strings are views owned by `NodeGraph`, do not keep `std::string_view`, `GraphNode*`, or `Property*`
after the owning `NodeGraph` is destroyed or reparsed.

## Notes

- Integer values are parsed as signed 64-bit integers.
- Floating-point values are parsed as `double`.
- `dump()` emits parseable text and preserves integral-looking float values as floats, for example `1.0`.
- `dump()` preserves parsed table arrays when possible, and emits simple arrays inline.
- Source block values are stored as `NodeValue::Source`, not normal strings.
- This library only handles the text graph format. Higher-level object serialization, reflection, or engine binding
  should live outside this library.
