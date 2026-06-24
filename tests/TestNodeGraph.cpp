#include <catch2/catch_test_macros.hpp>
#include "node_graph_format/NodeGraph.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

using namespace ng;
using String = std::string;

TEST_CASE("NodeGraph basic properties", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        intVal = 42
        floatVal = 3.14
        boolVal = true
        strVal = "hello"
        nullVal = null
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.getInt("intVal") == 42);
    REQUIRE(root.getFloat("floatVal") == 3.14);
    REQUIRE(root.getBool("boolVal") == true);
    REQUIRE(root.getString("strVal") == "hello");
    REQUIRE(root.hasProperty("nullVal"));
}

TEST_CASE("NodeGraph typed inline object", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        speed = 1.5
        count = 10
        name = "test"
        transform = Transform {
            position = [0.0, 0.0, 0.0]
        }
    })"));

    const auto& root = cfg.getRoot();
    auto* transform = root.getObject("transform");
    REQUIRE(transform != nullptr);
    REQUIRE(transform->className == "Transform");
    REQUIRE(transform->getList("position") != nullptr);
}

TEST_CASE("NodeGraph DSL children", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        Node {
            id = 1
        }
        Node {
            id = 2
        }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.children.size() == 2);
    REQUIRE(root.children[0]->className == "Node");
    REQUIRE(root.children[0]->getInt("id") == 1);
    REQUIRE(root.children[1]->className == "Node");
    REQUIRE(root.children[1]->getInt("id") == 2);
}

TEST_CASE("NodeGraph named child node", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        Shader "main" {
            technique = "default"
        }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.children.size() == 1);
    REQUIRE(root.children[0]->className == "Shader");
    REQUIRE(root.children[0]->name == "main");
    REQUIRE(root.children[0]->getString("technique") == "default");
}

TEST_CASE("NodeGraph named root node", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Shader "main" {
        technique = "default"
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.className == "Shader");
    REQUIRE(root.name == "main");
    REQUIRE(root.getString("technique") == "default");
}

TEST_CASE("NodeGraph object property", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        size = { width = 100 height = 200 }
        layout = Grid {
            cols = 3
        }
    })"));

    const auto& root = cfg.getRoot();
    auto* size = root.getObject("size");
    REQUIRE(size != nullptr);
    REQUIRE(size->className.empty()); // untyped inline object
    REQUIRE(size->getInt("width") == 100);
    REQUIRE(size->getInt("height") == 200);

    auto* layout = root.findProperty("layout");
    REQUIRE(layout != nullptr);
    REQUIRE(layout->value.isObject());
    REQUIRE(layout->value.asObject()->className == "Grid"); // typed inline object
    REQUIRE(layout->value.asObject()->getInt("cols") == 3);
}

TEST_CASE("NodeGraph source block", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        @vertex {
            void main() { gl_Position = vec4(1.0); }
        }
    })"));

    const auto& root = cfg.getRoot();
    auto* prop = root.findProperty("@vertex");
    REQUIRE(prop != nullptr);
    REQUIRE(prop->value.isSource());
    REQUIRE(prop->value.asString().find("void main()") != String::npos);
}

TEST_CASE("NodeGraph source block with equals sign", "[nodegraph]")
{
    // @source = { } syntax should also work
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        @vertex = {
            void main() { gl_Position = vec4(1.0); }
        }
    })"));

    const auto& root = cfg.getRoot();
    auto* prop = root.findProperty("@vertex");
    REQUIRE(prop != nullptr);
    REQUIRE(prop->value.isSource());
    REQUIRE(prop->value.asString().find("void main()") != String::npos);
}

TEST_CASE("NodeGraph source block ignores braces in strings and comments", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"NG({
        @fragment {
            const char* text = "}";
            // }
            /* } */
            void main() {
                if (true) {
                }
            }
        }
        value = 7
    })NG"));

    const auto& root = cfg.getRoot();
    auto* prop = root.findProperty("@fragment");
    REQUIRE(prop != nullptr);
    REQUIRE(prop->value.isSource());
    REQUIRE(prop->value.asString().find("const char* text") != String::npos);
    REQUIRE(root.getInt("value") == 7);
}

TEST_CASE("NodeGraph tagged source block", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        @vertex "glsl" {
            void main() { gl_Position = vec4(1.0); }
        }
    })"));

    const auto& root = cfg.getRoot();
    auto* prop = root.findProperty("@vertex");
    REQUIRE(prop != nullptr);
    REQUIRE(prop->value.isSource());
    REQUIRE(prop->label == "glsl");
    REQUIRE(prop->value.asString().find("void main()") != String::npos);
}

TEST_CASE("NodeGraph Shader with tagged sources", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Shader "main" {
        @vertex "glsl" {
            void main() {}
        }
        @fragment "glsl" {
            void main() {}
        }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.className == "Shader");
    REQUIRE(root.name == "main");
    REQUIRE(root.hasProperty("@vertex"));
    REQUIRE(root.hasProperty("@fragment"));

    auto* v = root.findProperty("@vertex");
    REQUIRE(v->label == "glsl");
    auto* f = root.findProperty("@fragment");
    REQUIRE(f->label == "glsl");
}

TEST_CASE("NodeGraph simple array", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        position = [1, 2, 3]
        colors = ["red", "green", "blue"]
        flags = [true, false, true]
    })"));

    const auto& root = cfg.getRoot();
    auto* pos = root.getList("position");
    REQUIRE(pos != nullptr);
    REQUIRE(pos->size() == 3);
    REQUIRE(pos->at(0).asInt() == 1);
    REQUIRE(pos->at(1).asInt() == 2);
    REQUIRE(pos->at(2).asInt() == 3);

    auto* colors = root.getList("colors");
    REQUIRE(colors != nullptr);
    REQUIRE(colors->size() == 3);
    REQUIRE(colors->at(0).asString() == "red");
    REQUIRE(colors->at(1).asString() == "green");
    REQUIRE(colors->at(2).asString() == "blue");
}

TEST_CASE("NodeGraph complex array", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        locations = [
            { x = 1 y = 2 z = 3 }
            { x = 4 y = 5 z = 6 }
        ]
    })"));

    const auto& root = cfg.getRoot();
    auto* locs = root.getList("locations");
    REQUIRE(locs != nullptr);
    REQUIRE(locs->size() == 2);
    REQUIRE(locs->at(0).isObject());
    REQUIRE(locs->at(0).asObject()->getInt("x") == 1);
    REQUIRE(locs->at(0).asObject()->getInt("y") == 2);
    REQUIRE(locs->at(1).asObject()->getInt("z") == 6);
}

TEST_CASE("NodeGraph table array", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        players = [
            #Player id name position meta
            1 "name 1" [4, 5, 6] { x = 1 y = 2 }
            2 "name 2" [7, 8, 9] { x = 3 y = 4 }
        ]
    })"));

    const auto& root = cfg.getRoot();
    auto* players = root.getList("players");
    REQUIRE(players != nullptr);
    REQUIRE(players->size() == 2);

    const GraphNode* player1 = players->at(0).asObject();
    REQUIRE(player1 != nullptr);
    REQUIRE(player1->className == "Player");
    REQUIRE(player1->getInt("id") == 1);
    REQUIRE(player1->getString("name") == "name 1");

    auto* position = player1->getList("position");
    REQUIRE(position != nullptr);
    REQUIRE(position->size() == 3);
    REQUIRE(position->at(0).asInt() == 4);
    REQUIRE(position->at(2).asInt() == 6);

    const GraphNode* meta = player1->getObject("meta");
    REQUIRE(meta != nullptr);
    REQUIRE(meta->getInt("x") == 1);
    REQUIRE(meta->getInt("y") == 2);

    const GraphNode* player2 = players->at(1).asObject();
    REQUIRE(player2 != nullptr);
    REQUIRE(player2->className == "Player");
    REQUIRE(player2->getInt("id") == 2);
    REQUIRE(player2->getString("name") == "name 2");
}

TEST_CASE("NodeGraph dump preserves table array format", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        players = [
            #Player id name position meta
            1 "name 1" [4, 5, 6] { x = 1 y = 2 }
            2 "name 2" [7, 8, 9] { x = 3 y = 4 }
        ]
    })"));

    String dumped = cfg.dump();
    INFO("Dumped output:\n" << dumped);
    REQUIRE(dumped.find("players = [\n") != String::npos);
    REQUIRE(dumped.find("    #Player id name position meta\n") != String::npos);
    REQUIRE(dumped.find("    1 \"name 1\" [4, 5, 6] { x = 1 y = 2 }\n") != String::npos);
    REQUIRE(dumped.find("    2 \"name 2\" [7, 8, 9] { x = 3 y = 4 }\n") != String::npos);

    NodeGraph reparsed;
    REQUIRE(reparsed.parse(dumped));
    auto* players = reparsed.getRoot().getList("players");
    REQUIRE(players != nullptr);
    REQUIRE(players->size() == 2);
    REQUIRE(players->at(0).asObject()->className == "Player");
    REQUIRE(players->at(0).asObject()->getObject("meta")->getInt("x") == 1);
}

TEST_CASE("NodeGraph nested table array", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        teams = [
            #Team id players
            1 [
                #Player id name
                10 "name 10"
                11 "name 11"
            ]
        ]
    })"));

    auto* teams = cfg.getRoot().getList("teams");
    REQUIRE(teams != nullptr);
    REQUIRE(teams->size() == 1);

    const GraphNode* team = teams->at(0).asObject();
    REQUIRE(team != nullptr);
    REQUIRE(team->className == "Team");
    REQUIRE(team->getInt("id") == 1);

    auto* players = team->getList("players");
    REQUIRE(players != nullptr);
    REQUIRE(players->size() == 2);
    REQUIRE(players->at(0).asObject()->className == "Player");
    REQUIRE(players->at(0).asObject()->getInt("id") == 10);
    REQUIRE(players->at(1).asObject()->getString("name") == "name 11");
}

TEST_CASE("NodeGraph dump preserves nested table array format", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        teams = [
            #Team id players
            1 [
                #Player id name
                10 "name 10"
                11 "name 11"
            ]
        ]
    })"));

    String dumped = cfg.dump();
    INFO("Dumped output:\n" << dumped);
    REQUIRE(dumped.find("    #Team id players\n") != String::npos);
    REQUIRE(dumped.find("    1 [\n") != String::npos);
    REQUIRE(dumped.find("        #Player id name\n") != String::npos);
    REQUIRE(dumped.find("        10 \"name 10\"\n") != String::npos);

    NodeGraph reparsed;
    REQUIRE(reparsed.parse(dumped));
    auto* teams = reparsed.getRoot().getList("teams");
    REQUIRE(teams != nullptr);
    auto* players = teams->at(0).asObject()->getList("players");
    REQUIRE(players != nullptr);
    REQUIRE(players->at(1).asObject()->getString("name") == "name 11");
}

TEST_CASE("NodeGraph comments", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        // This is a comment
        value = 42 // inline comment
        // Another comment
        name = "test"
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.getInt("value") == 42);
    REQUIRE(root.getString("name") == "test");
}

TEST_CASE("NodeGraph nested children", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        Node {
            child = "first"
            Node {
                child = "nested"
            }
        }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.children.size() == 1);
    REQUIRE(root.children[0]->getString("child") == "first");
    REQUIRE(root.children[0]->children.size() == 1);
    REQUIRE(root.children[0]->children[0]->getString("child") == "nested");
}

TEST_CASE("NodeGraph escape sequences", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        path = "C:\\temp\\file.txt"
        multiline = "line1\nline2\ttabbed"
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.getString("path") == "C:\\temp\\file.txt");
    REQUIRE(root.getString("multiline") == "line1\nline2\ttabbed");
}

TEST_CASE("NodeGraph round-trip basic", "[nodegraph]")
{
    const char* input = R"({
    intVal = 42
    floatVal = 3.14
    strVal = "hello"
    boolVal = true
}
)";

    NodeGraph cfg1;
    REQUIRE(cfg1.parse(input));
    String dumped = cfg1.dump();

    NodeGraph cfg2;
    REQUIRE(cfg2.parse(dumped));

    const auto& r1 = cfg1.getRoot();
    const auto& r2 = cfg2.getRoot();
    REQUIRE(r1.getInt("intVal") == r2.getInt("intVal"));
    REQUIRE(r1.getFloat("floatVal") == r2.getFloat("floatVal"));
    REQUIRE(r1.getString("strVal") == r2.getString("strVal"));
    REQUIRE(r1.getBool("boolVal") == r2.getBool("boolVal"));
}

TEST_CASE("NodeGraph dump preserves integral-looking float type", "[nodegraph]")
{
    NodeGraph cfg1;
    REQUIRE(cfg1.parse(R"({
    floatVal = 1.0
}
)"));

    String dumped = cfg1.dump();
    REQUIRE(dumped.find("floatVal = 1.0") != String::npos);

    NodeGraph cfg2;
    REQUIRE(cfg2.parse(dumped));
    auto* prop = cfg2.getRoot().findProperty("floatVal");
    REQUIRE(prop != nullptr);
    REQUIRE(prop->value.isFloat());
    REQUIRE(prop->value.asFloat() == 1.0);
}

TEST_CASE("NodeGraph round-trip with children and names", "[nodegraph]")
{
    const char* input = "Shader \"main\" {\n"
                        "    @vertex \"glsl\" {\n"
                        "        void main() {}\n"
                        "    }\n"
                        "    technique = \"default\"\n"
                        "    Node {\n"
                        "        id = 1\n"
                        "    }\n"
                        "}\n";

    NodeGraph cfg1;
    REQUIRE(cfg1.parse(input));
    String dumped = cfg1.dump();

    // Round-trip: parse the dumped output and verify structure matches
    NodeGraph cfg2;
    REQUIRE(cfg2.parse(dumped));

    const auto& r1 = cfg1.getRoot();
    const auto& r2 = cfg2.getRoot();
    REQUIRE(r1.className == r2.className);
    REQUIRE(r1.name == r2.name);
    REQUIRE(r1.getString("technique") == r2.getString("technique"));

    REQUIRE(r1.children.size() == r2.children.size());
    REQUIRE(r1.children[0]->className == r2.children[0]->className);
    REQUIRE(r1.children[0]->getInt("id") == r2.children[0]->getInt("id"));

    auto* v1 = r1.findProperty("@vertex");
    auto* v2 = r2.findProperty("@vertex");
    REQUIRE(v1 != nullptr);
    REQUIRE(v2 != nullptr);
    REQUIRE(v1->label == v2->label);
    REQUIRE(v1->value.asString() == v2->value.asString());
}

TEST_CASE("NodeGraph round-trip arrays", "[nodegraph]")
{
    const char* input = "{\n"
                        "    position = [1, 2, 3]\n"
                        "    locations = [\n"
                        "        {\n"
                        "            x = 1\n"
                        "            y = 2\n"
                        "        }\n"
                        "        {\n"
                        "            x = 4\n"
                        "            y = 5\n"
                        "        }\n"
                        "    ]\n"
                        "}\n";

    NodeGraph cfg1;
    REQUIRE(cfg1.parse(input));
    String dumped = cfg1.dump();

    NodeGraph cfg2;
    REQUIRE(cfg2.parse(dumped));

    const auto& r1 = cfg1.getRoot();
    const auto& r2 = cfg2.getRoot();

    auto* p1 = r1.getList("position");
    auto* p2 = r2.getList("position");
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p1->size() == p2->size());
    for (size_t k = 0; k < p1->size(); ++k)
        REQUIRE(p1->at(k).asInt() == p2->at(k).asInt());

    auto* l1 = r1.getList("locations");
    auto* l2 = r2.getList("locations");
    REQUIRE(l1 != nullptr);
    REQUIRE(l2 != nullptr);
    REQUIRE(l1->size() == l2->size());
    for (size_t k = 0; k < l1->size(); ++k) {
        REQUIRE(l1->at(k).asObject()->getInt("x") == l2->at(k).asObject()->getInt("x"));
        REQUIRE(l1->at(k).asObject()->getInt("y") == l2->at(k).asObject()->getInt("y"));
    }
}

TEST_CASE("NodeGraph dump format", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        value = 42
    })"));

    String dumped = cfg.dump();
    // Dump should produce parseable output
    REQUIRE(dumped.find("value = 42") != String::npos);
    REQUIRE(dumped.find("{") != String::npos);
    REQUIRE(dumped.find("}") != String::npos);
}

TEST_CASE("NodeGraph dump separates properties and DSL children", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Scene {
        Active = true
        Node "Player" {
            id = 1
        }
    })"));

    String dumped = cfg.dump();
    INFO("Dumped output:\n" << dumped);
    REQUIRE(dumped.find("    Active = true\n\n    Node \"Player\" {") != String::npos);

    NodeGraph reparsed;
    REQUIRE(reparsed.parse(dumped));
    REQUIRE(reparsed.getRoot().getBool("Active"));
    REQUIRE(reparsed.getRoot().children.size() == 1);
}

TEST_CASE("NodeGraph dump quotes non-identifier property names", "[nodegraph]")
{
    NodeGraph cfg;
    auto& root = cfg.getRoot();
    root.addProperty(cfg.allocString("Light Channel"), NodeValue::makeInt(255));

    String dumped = cfg.dump();
    REQUIRE(dumped.find("\"Light Channel\" = 255") != String::npos);

    NodeGraph parsed;
    INFO("Dumped output:\n" << dumped);
    REQUIRE(parsed.parse(dumped));
    REQUIRE(parsed.getRoot().getInt("Light Channel") == 255);
}

TEST_CASE("NodeGraph parses legacy unquoted spaced property names", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        Light Channel = 255
    })"));
    REQUIRE(cfg.getRoot().getInt("Light Channel") == 255);
}

TEST_CASE("NodeGraph dump simple array one line", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        pos = [1, 2, 3]
    })"));

    String dumped = cfg.dump();
    // Simple array should be on one line
    REQUIRE(dumped.find("pos = [1, 2, 3]") != String::npos);
}

TEST_CASE("NodeGraph dump nested inline array one line", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        matrix = [[1, 2], [3, 4]]
    })"));

    String dumped = cfg.dump();
    REQUIRE(dumped.find("matrix = [[1, 2], [3, 4]]") != String::npos);
}

TEST_CASE("NodeGraph dump preserves inline object property", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Node {
        Meta = { x = 1 y = 2 z = 3 }
    })"));

    String dumped = cfg.dump();
    REQUIRE(dumped.find("Meta = { x = 1 y = 2 z = 3 }") != String::npos);

    NodeGraph reparsed;
    REQUIRE(reparsed.parse(dumped));
    const GraphNode* meta = reparsed.getRoot().getObject("Meta");
    REQUIRE(meta != nullptr);
    REQUIRE(meta->getInt("z") == 3);
}

TEST_CASE("NodeGraph dump preserves multiline object property", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Node {
        Meta = {
            x = 1
            y = 2
            z = 3
        }
    })"));

    String dumped = cfg.dump();
    REQUIRE(dumped.find("Meta = {\n") != String::npos);
    REQUIRE(dumped.find("Meta = { x = 1 y = 2 z = 3 }") == String::npos);
}

TEST_CASE("NodeGraph dump complex array multi line", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        items = [
            { x = 1 }
            { x = 2 }
        ]
    })"));

    String dumped = cfg.dump();
    // Complex array with untyped objects is serialized as object array block
    REQUIRE(dumped.find("items = {\n") != String::npos);
}

TEST_CASE("NodeGraph dump named node", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse("Shader \"main\" {\n    value = 1\n}\n"));

    String dumped = cfg.dump();
    REQUIRE(dumped.find("Shader \"main\" {") != String::npos);
}

TEST_CASE("NodeGraph dump tagged source", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        @vertex "glsl" {
            code
        }
    })"));

    String dumped = cfg.dump();
    REQUIRE(dumped.find("@vertex \"glsl\" {") != String::npos);
}

TEST_CASE("NodeGraph error missing equals", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE_FALSE(cfg.parse(R"({
        property value
    })"));
    REQUIRE_FALSE(cfg.getError().empty());
    REQUIRE(cfg.getError().find("column") != String::npos);
    REQUIRE(cfg.getError().find("\\n") != String::npos);
}

TEST_CASE("NodeGraph error unclosed brace", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE_FALSE(cfg.parse(R"({
        value = 42
    )"));
    REQUIRE_FALSE(cfg.getError().empty());
}

TEST_CASE("NodeGraph error unclosed escaped string", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE_FALSE(cfg.parse("{ value = \"abc\\\" }"));
    REQUIRE_FALSE(cfg.getError().empty());
    REQUIRE(cfg.getError().find("closing") != String::npos);
}

TEST_CASE("NodeGraph error unclosed quoted node name", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE_FALSE(cfg.parse("Node \"abc\\\" { value = 1 }"));
    REQUIRE_FALSE(cfg.getError().empty());
    REQUIRE(cfg.getError().find("quoted name") != String::npos);
}

TEST_CASE("NodeGraph error unclosed triple string", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE_FALSE(cfg.parse("{ value = \"\"\"abc }"));
    REQUIRE_FALSE(cfg.getError().empty());
    REQUIRE(cfg.getError().find("triple quote") != String::npos);
}

TEST_CASE("NodeGraph error invalid numbers", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE_FALSE(cfg.parse(R"({ value = 1e })"));
    REQUIRE_FALSE(cfg.getError().empty());

    REQUIRE_FALSE(cfg.parse(R"({ value = 1.2.3 })"));
    REQUIRE_FALSE(cfg.getError().empty());

    REQUIRE_FALSE(cfg.parse(R"({ value = 9223372036854775808 })"));
    REQUIRE_FALSE(cfg.getError().empty());
}

TEST_CASE("NodeGraph integer boundaries", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        minValue = -9223372036854775808
        maxValue = 9223372036854775807
    })"));

    REQUIRE(cfg.getRoot().getInt("minValue") == std::numeric_limits<int64_t>::min());
    REQUIRE(cfg.getRoot().getInt("maxValue") == std::numeric_limits<int64_t>::max());
}

TEST_CASE("NodeGraph empty object", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse("{}"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.properties.empty());
    REQUIRE(root.children.empty());
}

TEST_CASE("NodeGraph triple quoted string", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        text = """multi
line
string"""
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.getString("text") == "multi\nline\nstring");
}

TEST_CASE("NodeGraph object array block", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(TestRoot {
        Components = {
            Light {
                Color = [1.0, 1.0, 1.0]
                Intensity = 1.0
            }
            Camera {
                Fov = 60.0
            }
        }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.className == "TestRoot");
    auto* comps = root.getList("Components");
    REQUIRE(comps != nullptr);
    REQUIRE(comps->size() == 2);
    
    // First element: Light
    REQUIRE(comps->at(0).isObject());
    const GraphNode* light = comps->at(0).asObject();
    REQUIRE(light != nullptr);
    REQUIRE(light->className == "Light");
    REQUIRE(light->name.empty());
    
    auto* lightColor = light->getList("Color");
    REQUIRE(lightColor != nullptr);
    REQUIRE(lightColor->size() == 3);
    REQUIRE(lightColor->at(0).asFloat() == 1.0);
    REQUIRE(light->getFloat("Intensity") == 1.0);
    
    // Second element: Camera
    REQUIRE(comps->at(1).isObject());
    const GraphNode* camera = comps->at(1).asObject();
    REQUIRE(camera != nullptr);
    REQUIRE(camera->className == "Camera");
    REQUIRE(camera->getFloat("Fov") == 60.0);
}

TEST_CASE("NodeGraph object array block with names", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(TestRoot {
        Children = {
            Node "Player" {
                Position = [10.0, 0.0, 5.0]
            }
            Node "Enemy" {
                Position = [20.0, 0.0, 0.0]
            }
        }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.className == "TestRoot");
    auto* children = root.getList("Children");
    REQUIRE(children != nullptr);
    REQUIRE(children->size() == 2);
    
    // First child
    const GraphNode* player = children->at(0).asObject();
    REQUIRE(player != nullptr);
    REQUIRE(player->className == "Node");
    REQUIRE(player->name == "Player");
    
    // Second child
    const GraphNode* enemy = children->at(1).asObject();
    REQUIRE(enemy != nullptr);
    REQUIRE(enemy->className == "Node");
    REQUIRE(enemy->name == "Enemy");
}

TEST_CASE("NodeGraph mixed children and object array", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Scene "MainScene" {
        Active = true
        Position = [0.0, 0.0, 0.0]
        
        // Direct children (DSL style)
        Node "Player" {
            Position = [10.0, 0.0, 5.0]
            
            // Object array block for components
            Components = {
                Light {
                    Color = [1.0, 1.0, 1.0]
                }
            }
            
            // Nested child
            Node "Weapon" {
                Position = [1.0, 0.0, 0.0]
            }
        }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.className == "Scene");
    REQUIRE(root.name == "MainScene");
    REQUIRE(root.getBool("Active") == true);
    
    // Direct child
    REQUIRE(root.children.size() == 1);
    const GraphNode* player = root.children[0];
    REQUIRE(player->className == "Node");
    REQUIRE(player->name == "Player");
    
    // Player's components (object array block)
    auto* comps = player->getList("Components");
    REQUIRE(comps != nullptr);
    REQUIRE(comps->size() == 1);
    REQUIRE(comps->at(0).asObject()->className == "Light");
    
    // Player's nested child
    REQUIRE(player->children.size() == 1);
    const GraphNode* weapon = player->children[0];
    REQUIRE(weapon->className == "Node");
    REQUIRE(weapon->name == "Weapon");
}

TEST_CASE("NodeGraph round-trip object array block", "[nodegraph]")
{
    const char* input = R"(Node "Root" {
    Components = {
        Light {
            Intensity = 1.0
        }
        Camera {
            Fov = 60.0
        }
    }
}
)";

    NodeGraph cfg1;
    REQUIRE(cfg1.parse(input));
    String dumped = cfg1.dump();
    
    // Check that dumped output is valid
    NodeGraph cfg2;
    bool parseResult = cfg2.parse(dumped);
    if (!parseResult) {
        FAIL("Parse error: " << cfg2.getError() << "\nDumped output:\n" << dumped);
    }
    REQUIRE(parseResult);
    
    const auto& r1 = cfg1.getRoot();
    const auto& r2 = cfg2.getRoot();
    
    REQUIRE(r1.className == r2.className);
    REQUIRE(r1.name == r2.name);
    
    auto* c1 = r1.getList("Components");
    auto* c2 = r2.getList("Components");
    REQUIRE(c1 != nullptr);
    REQUIRE(c2 != nullptr);
    REQUIRE(c1->size() == c2->size());
    
    for (size_t i = 0; i < c1->size(); ++i) {
        const GraphNode* n1 = c1->at(i).asObject();
        const GraphNode* n2 = c2->at(i).asObject();
        REQUIRE(n1->className == n2->className);
    }
}

TEST_CASE("NodeGraph inline object vs object array block", "[nodegraph]")
{
    // Inline object: first element after { is property name (followed by =)
    NodeGraph cfg1;
    REQUIRE(cfg1.parse(R"(TestRoot {
        transform = {
            x = 1
            y = 2
        }
    })"));
    
    const auto& r1 = cfg1.getRoot();
    auto* transform = r1.getObject("transform");
    REQUIRE(transform != nullptr);
    REQUIRE(transform->className.empty()); // inline object has no className
    REQUIRE(transform->getInt("x") == 1);
    REQUIRE(transform->getInt("y") == 2);
    
    // Object array block: first element after { is ClassName (followed by {)
    NodeGraph cfg2;
    REQUIRE(cfg2.parse(R"(TestRoot {
        items = {
            Item {
                value = 1
            }
            Item {
                value = 2
            }
        }
    })"));
    
    const auto& r2 = cfg2.getRoot();
    auto* items = r2.getList("items");
    REQUIRE(items != nullptr);
    REQUIRE(items->size() == 2);
    REQUIRE(items->at(0).asObject()->className == "Item");
}

TEST_CASE("NodeGraph untyped object array block", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Node {
        tags = {
            {
                "test" = "111"
            }
            {
                "test1" = "222"
            }
        }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.className == "Node");
    auto* tags = root.getList("tags");
    REQUIRE(tags != nullptr);
    REQUIRE(tags->size() == 2);

    // First untyped element
    REQUIRE(tags->at(0).isObject());
    const GraphNode* tag1 = tags->at(0).asObject();
    REQUIRE(tag1->className.empty()); // untyped
    REQUIRE(tag1->getString("test") == "111");

    // Second untyped element
    REQUIRE(tags->at(1).isObject());
    const GraphNode* tag2 = tags->at(1).asObject();
    REQUIRE(tag2->className.empty());
    REQUIRE(tag2->getString("test1") == "222");
}

TEST_CASE("NodeGraph typed object array with escaped element name", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(
        "Node {\n"
        "    items = {\n"
        "        Item \"a\\\"b\" {\n"
        "            value = 1\n"
        "        }\n"
        "    }\n"
        "}"));

    auto* items = cfg.getRoot().getList("items");
    REQUIRE(items != nullptr);
    REQUIRE(items->size() == 1);

    const GraphNode* item = items->at(0).asObject();
    REQUIRE(item != nullptr);
    REQUIRE(item->className == "Item");
    REQUIRE(item->name == "a\"b");
    REQUIRE(item->getInt("value") == 1);
}

TEST_CASE("NodeGraph typed inline object in value position", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Node {
        transform = Transform {
            position = { x = 0 y = 0 }
        }
    })"));

    const auto& root = cfg.getRoot();
    auto* transform = root.getObject("transform");
    REQUIRE(transform != nullptr);
    REQUIRE(transform->className == "Transform");

    auto* position = transform->getObject("position");
    REQUIRE(position != nullptr);
    REQUIRE(position->className.empty()); // untyped inline object
    REQUIRE(position->getInt("x") == 0);
    REQUIRE(position->getInt("y") == 0);
}

TEST_CASE("NodeGraph mixed typed and untyped object arrays", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Node {
        components = {
            Camera {
                fov = 60.0
            }
            Light {
                intensity = 1.0
            }
        }
    })"));

    const auto& root = cfg.getRoot();
    auto* comps = root.getList("components");
    REQUIRE(comps != nullptr);
    REQUIRE(comps->size() == 2);
    REQUIRE(comps->at(0).asObject()->className == "Camera");
    REQUIRE(comps->at(1).asObject()->className == "Light");
}

TEST_CASE("NodeGraph round-trip typed inline object", "[nodegraph]")
{
    const char* input = R"(Node {
    transform = Transform {
        position = { x = 0 y = 0 }
    }
}
)";

    NodeGraph cfg1;
    REQUIRE(cfg1.parse(input));
    String dumped = cfg1.dump();

    NodeGraph cfg2;
    REQUIRE(cfg2.parse(dumped));

    const auto& r2 = cfg2.getRoot();
    auto* transform = r2.getObject("transform");
    REQUIRE(transform != nullptr);
    REQUIRE(transform->className == "Transform");
    auto* position = transform->getObject("position");
    REQUIRE(position != nullptr);
    REQUIRE(position->getInt("x") == 0);
    REQUIRE(position->getInt("y") == 0);
}

TEST_CASE("NodeGraph round-trip untyped object array block", "[nodegraph]")
{
    const char* input = R"(Node {
    tags = {
        {
            key = "value1"
        }
        {
            key = "value2"
        }
    }
}
)";

    NodeGraph cfg1;
    REQUIRE(cfg1.parse(input));
    String dumped = cfg1.dump();

    NodeGraph cfg2;
    REQUIRE(cfg2.parse(dumped));

    const auto& r2 = cfg2.getRoot();
    auto* tags = r2.getList("tags");
    REQUIRE(tags != nullptr);
    REQUIRE(tags->size() == 2);
    REQUIRE(tags->at(0).asObject()->className.empty());
    REQUIRE(tags->at(0).asObject()->getString("key") == "value1");
    REQUIRE(tags->at(1).asObject()->getString("key") == "value2");
}

TEST_CASE("NodeGraph accessor defaults and type safety", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        enabled = true
        count = 7
        ratio = 2.5
        name = "unit"
        values = [1, 2]
        nested = { value = 9 }
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.getBool("enabled", false));
    REQUIRE(root.getBool("missingBool", true));
    REQUIRE(root.getBool("count", true)); // Wrong type returns caller default.

    REQUIRE(root.getInt("count", -1) == 7);
    REQUIRE(root.getInt("missingInt", -1) == -1);
    REQUIRE(root.getInt("ratio", -1) == -1);

    REQUIRE(root.getFloat("count", -1.0) == 7.0); // Ints are valid floats.
    REQUIRE(root.getFloat("ratio", -1.0) == 2.5);
    REQUIRE(root.getFloat("name", -1.0) == -1.0);

    REQUIRE(root.getString("name", "fallback") == "unit");
    REQUIRE(root.getString("enabled", "fallback") == "fallback");
    REQUIRE(root.getObject("nested") != nullptr);
    REQUIRE(root.getObject("values") == nullptr);
    REQUIRE(root.getList("values") != nullptr);
    REQUIRE(root.getList("nested") == nullptr);
}

TEST_CASE("NodeGraph duplicate properties update the indexed value", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({
        value = 1
        value = 2
        value = 3
    })"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.properties.size() == 3);
    REQUIRE(root.properties[0].value.asInt() == 1);
    REQUIRE(root.properties[1].value.asInt() == 2);
    REQUIRE(root.properties[2].value.asInt() == 3);
    REQUIRE(root.getInt("value") == 3);
}

TEST_CASE("NodeGraph addProperty keeps property index coherent", "[nodegraph]")
{
    NodeGraph cfg;
    auto& root = cfg.getRoot();

    root.addProperty(cfg.allocString("value"), NodeValue::makeInt(1));
    REQUIRE(root.getInt("value") == 1); // Builds the lookup index.

    root.addProperty(cfg.allocString("value"), NodeValue::makeInt(2));
    REQUIRE(root.properties.size() == 1);
    REQUIRE(root.getInt("value") == 2);

    root.addProperty(cfg.allocString("second"), NodeValue::makeString(cfg.allocString("ok")));
    REQUIRE(root.properties.size() == 2);
    REQUIRE(root.getString("second") == "ok");
}

TEST_CASE("NodeGraph child lookup returns expected matches in order", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"(Scene {
        Node "Player" { id = 1 }
        Light "Key" { intensity = 3.0 }
        Node "Enemy" { id = 2 }
    })"));

    const auto& root = cfg.getRoot();
    const GraphNode* firstNode = root.findChild("Node");
    REQUIRE(firstNode != nullptr);
    REQUIRE(firstNode->name == "Player");

    auto nodes = root.findChildren("Node");
    REQUIRE(nodes.size() == 2);
    REQUIRE(nodes[0]->name == "Player");
    REQUIRE(nodes[1]->name == "Enemy");
    REQUIRE(root.findChild("Camera") == nullptr);
    REQUIRE(root.findChildren("Camera").empty());
}

TEST_CASE("NodeGraph escaped strings and unicode survive round-trip", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(
        "{\n"
        "    escaped = \"line\\n\\tquote\\\"slash\\\\solidus\\/\"\n"
        "    unicode = \"snowman: \\u2603\"\n"
        "    Node \"name\\u0020with\\u0020space\" { value = 1 }\n"
        "}"));

    const auto& root = cfg.getRoot();
    REQUIRE(root.getString("escaped") == "line\n\tquote\"slash\\solidus/");
    REQUIRE(root.getString("unicode") == std::string_view("snowman: \xE2\x98\x83"));

    REQUIRE(root.children.size() == 1);
    const GraphNode* node = root.children[0];
    REQUIRE(node != nullptr);
    REQUIRE(node->className == "Node");
    REQUIRE(node->name == "name with space");

    NodeGraph reparsed;
    String dumped = cfg.dump();
    INFO("Dumped output:\n" << dumped);
    REQUIRE(reparsed.parse(dumped));
    REQUIRE(reparsed.getRoot().getString("escaped") == root.getString("escaped"));
    REQUIRE(reparsed.getRoot().getString("unicode") == root.getString("unicode"));
}

TEST_CASE("NodeGraph rejects malformed unicode escapes", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE_FALSE(cfg.parse(R"({ value = "\u12G4" })"));
    REQUIRE_FALSE(cfg.getError().empty());

    REQUIRE_FALSE(cfg.parse(R"({ value = "\u12" })"));
    REQUIRE_FALSE(cfg.getError().empty());

    REQUIRE_FALSE(cfg.parse(R"(Node "bad\u00G0" { value = 1 })"));
    REQUIRE_FALSE(cfg.getError().empty());
}

TEST_CASE("NodeGraph parse resets root and error after failure", "[nodegraph]")
{
    NodeGraph cfg;
    REQUIRE(cfg.parse(R"({ oldValue = 1 })"));
    REQUIRE(cfg.getRoot().hasProperty("oldValue"));

    REQUIRE_FALSE(cfg.parse(R"({ broken = })"));
    REQUIRE_FALSE(cfg.getError().empty());

    REQUIRE(cfg.parse(R"({ newValue = 2 })"));
    REQUIRE(cfg.getError().empty());
    REQUIRE_FALSE(cfg.getRoot().hasProperty("oldValue"));
    REQUIRE(cfg.getRoot().getInt("newValue") == 2);
}

TEST_CASE("NodeGraph handles large property sets and long scalar fast paths", "[nodegraph]")
{
    std::ostringstream input;
    input << "{\n";
    input << "    longString = \"";
    for (int i = 0; i < 256; ++i)
        input << char('a' + (i % 26));
    input << "\"\n";
    input << "    longIdentifierValue = ";
    for (int i = 0; i < 32; ++i)
        input << '9';
    input << ".25\n";
    for (int i = 0; i < 300; ++i)
        input << "    prop" << i << " = " << i << "\n";
    input << "}\n";

    NodeGraph cfg;
    REQUIRE(cfg.parse(input.str()));

    const auto& root = cfg.getRoot();
    REQUIRE(root.properties.size() == 302);
    REQUIRE(root.getString("longString").size() == 256);
    REQUIRE(root.getFloat("longIdentifierValue") > 0.0);
    REQUIRE(root.getInt("prop0") == 0);
    REQUIRE(root.getInt("prop127") == 127);
    REQUIRE(root.getInt("prop299") == 299);
    REQUIRE(root.getInt("missing", -42) == -42);
}

TEST_CASE("NodeGraph handles deeply nested children", "[nodegraph]")
{
    std::ostringstream input;
    input << "Root {\n";
    for (int depth = 0; depth < 64; ++depth)
        input << "Node \"level" << depth << "\" {\n";
    input << "value = 64\n";
    for (int depth = 0; depth < 64; ++depth)
        input << "}\n";
    input << "}\n";

    NodeGraph cfg;
    REQUIRE(cfg.parse(input.str()));

    const GraphNode* node = &cfg.getRoot();
    REQUIRE(node->className == "Root");
    for (int depth = 0; depth < 64; ++depth) {
        REQUIRE(node->children.size() == 1);
        node = node->children[0];
        REQUIRE(node->className == "Node");
    }
    REQUIRE(node->getInt("value") == 64);
}

TEST_CASE("NodeGraph file parse and save round-trip", "[nodegraph]")
{
    auto inputPath = std::filesystem::temp_directory_path() / "node_graph_format_input.ngf";
    auto outputPath = std::filesystem::temp_directory_path() / "node_graph_format_output.ngf";

    {
        std::ofstream out(inputPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "Scene \"File\" {\n"
            << "    enabled = true\n"
            << "    value = 42\n"
            << "}\n";
    }

    NodeGraph cfg;
    REQUIRE(cfg.parseFile(inputPath.string()));
    REQUIRE(cfg.getRoot().className == "Scene");
    REQUIRE(cfg.getRoot().name == "File");
    REQUIRE(cfg.getRoot().getBool("enabled"));
    REQUIRE(cfg.saveFile(outputPath.string()));

    NodeGraph reparsed;
    REQUIRE(reparsed.parseFile(outputPath.string()));
    REQUIRE(reparsed.getRoot().getInt("value") == 42);

    std::filesystem::remove(inputPath);
    std::filesystem::remove(outputPath);
}

TEST_CASE("NodeGraph parseFile reports missing files", "[nodegraph]")
{
    NodeGraph cfg;
    auto missingPath = std::filesystem::temp_directory_path() / "node_graph_format_missing_file.ngf";
    std::filesystem::remove(missingPath);

    REQUIRE_FALSE(cfg.parseFile(missingPath.string()));
    REQUIRE(cfg.getError().find("Failed to read file") != String::npos);
}
