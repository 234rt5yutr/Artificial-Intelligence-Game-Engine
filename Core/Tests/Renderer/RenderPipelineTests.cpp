// Checks the two pieces of the new rendering stack that are pure CPU logic and
// therefore actually testable without a device: the material graph compiler and
// the mesh clusteriser that feeds GPU-driven culling.
//
// Both are easy to get subtly wrong in ways a running frame hides - a cycle that
// makes codegen loop, a cluster whose index range does not match the reordered
// buffer - so they get a check each.

#include "Core/Log.h"
#include "Core/Renderer/GPUDriven/GPUScene.h"
#include "Core/Renderer/Material/MaterialGraph.h"
#include "Core/Renderer/Mesh.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>

// ctest runs these with -C Release, where NDEBUG turns assert() into a no-op:
// the condition is not evaluated, so any call inside one silently disappears and
// nothing is actually verified. CHECK always evaluates and always reports.
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

namespace {

using namespace Core::Renderer;
using Core::Math::Vec3;
using Core::Math::Vec4;

void TestDefaultGraphCompiles() {
    MaterialGraph graph = MaterialGraph::MakeDefault("Test", Vec3(0.2f, 0.4f, 0.9f), 0.3f, 0.5f);
    const auto result = graph.Compile();

    CHECK(result.Succeeded);
    CHECK(result.Error.empty());
    // Every output slot the shader template reads must be assigned, or the
    // generated body leaves a surface field at its default and the material
    // silently ignores what the graph said.
    CHECK(result.FragmentBody.find("surf.BaseColor") != std::string::npos);
    CHECK(result.FragmentBody.find("surf.Metallic") != std::string::npos);
    CHECK(result.FragmentBody.find("surf.Roughness") != std::string::npos);
    CHECK(result.FragmentBody.find("surf.Emissive") != std::string::npos);
    CHECK(result.FragmentBody.find("surf.Opacity") != std::string::npos);
    CHECK(result.Hash != 0);
}

void TestUnconnectedInputsFallBack() {
    // A graph that is only an Output node must still compile: every input falls
    // back to a literal rather than emitting a reference to a variable that was
    // never declared.
    MaterialGraph graph("Bare");
    graph.AddNode(MaterialNodeType::Output);
    const auto result = graph.Compile();

    CHECK(result.Succeeded);
    CHECK(result.FragmentBody.find("n0") == std::string::npos);
}

void TestCycleIsRejected() {
    MaterialGraph graph("Cyclic");
    const uint32_t a = graph.AddNode(MaterialNodeType::Multiply);
    const uint32_t b = graph.AddNode(MaterialNodeType::Multiply);
    const uint32_t output = graph.AddNode(MaterialNodeType::Output);

    CHECK(graph.Connect(a, output, 0));
    CHECK(graph.Connect(b, a, 0));

    // Closing the loop must be refused at the edge, not discovered later during
    // codegen.
    std::string error;
    CHECK(!graph.Connect(a, b, 0, &error));
    CHECK(!error.empty());
    CHECK(graph.Compile().Succeeded);
}

void TestMissingOutputIsRejected() {
    MaterialGraph graph("NoOutput");
    graph.AddNode(MaterialNodeType::ConstantColor);
    const auto result = graph.Compile();

    CHECK(!result.Succeeded);
    CHECK(!result.Error.empty());
}

void TestTextureSlotsAreDeduplicated() {
    MaterialGraph graph("Textured");
    const uint32_t albedo = graph.AddNode(MaterialNodeType::TextureSample);
    const uint32_t second = graph.AddNode(MaterialNodeType::TextureSample);
    const uint32_t multiply = graph.AddNode(MaterialNodeType::Multiply);
    const uint32_t output = graph.AddNode(MaterialNodeType::Output);

    graph.SetNodeTextureSlot(albedo, "BaseColor");
    graph.SetNodeTextureSlot(second, "BaseColor");
    CHECK(graph.Connect(albedo, multiply, 0));
    CHECK(graph.Connect(second, multiply, 1));
    CHECK(graph.Connect(multiply, output, 0));

    const auto result = graph.Compile();
    CHECK(result.Succeeded);
    // Two nodes naming the same slot must share one binding, or the material
    // burns two descriptor slots on the same texture.
    CHECK(result.TextureSlots.size() == 1);
    CHECK(result.TextureSlots[0] == "BaseColor");
}

void TestJsonRoundTrip() {
    MaterialGraph original = MaterialGraph::MakeDefault("RoundTrip", Vec3(0.7f, 0.1f, 0.2f), 1.0f, 0.25f);
    const std::string json = original.ToJson();

    MaterialGraph restored;
    std::string error;
    CHECK(MaterialGraph::FromJson(json, restored, &error));
    CHECK(error.empty());
    CHECK(restored.GetNodes().size() == original.GetNodes().size());
    CHECK(restored.GetLinks().size() == original.GetLinks().size());
    // The hash keys the pipeline cache, so a round trip that changes it would
    // rebuild every pipeline on every load.
    CHECK(restored.Compile().Hash == original.Compile().Hash);
}

void TestMalformedJsonIsRejected() {
    MaterialGraph graph;
    std::string error;
    CHECK(!MaterialGraph::FromJson("{ not json", graph, &error));
    CHECK(!MaterialGraph::FromJson(R"({"nodes":[{"id":1,"type":"NoSuchNode"}]})", graph, &error));
    CHECK(!error.empty());
}

void TestLibraryIndexing() {
    MaterialLibrary& library = MaterialLibrary::Get();
    library.Clear();

    const uint32_t defaultIndex = library.GetOrCreateDefault();
    CHECK(defaultIndex == 0);
    CHECK(library.GetMaterial(0) != nullptr);
    CHECK(library.GetMaterial(0)->Compiled.Succeeded);

    const uint32_t custom = library.CreateMaterial("Custom");
    CHECK(custom == 1);
    CHECK(library.FindMaterial("Custom") == 1);
    CHECK(library.FindMaterial("Absent") == UINT32_MAX);
    // Creating the same name twice must return the existing slot, or a draw
    // command's MaterialIndex would start pointing at a duplicate.
    CHECK(library.CreateMaterial("Custom") == 1);

    const uint64_t before = library.GetRevision();
    CHECK(library.MarkDirty(custom));
    CHECK(library.CompileDirty() == 1);
    CHECK(library.GetRevision() != before);

    library.Clear();
}

void TestClusterBuilderCoversEveryTriangle() {
    auto mesh = Mesh::CreatePrimitive("sphere", 24);
    CHECK(mesh != nullptr);
    CHECK(!mesh->vertices.empty());
    CHECK(mesh->indices.size() % 3 == 0);

    const uint32_t maxTriangles = 64;
    const MeshClusterSet clusters = BuildMeshClusters(
        mesh->vertices.data(), sizeof(Vertex), mesh->vertices.size(),
        mesh->indices, maxTriangles);

    CHECK(!clusters.Clusters.empty());
    // Reordering must be a permutation: dropping or duplicating a triangle would
    // show up as holes in the mesh only once it is on screen.
    CHECK(clusters.ReorderedIndices.size() == mesh->indices.size());

    std::size_t coveredIndices = 0;
    uint32_t previousEnd = 0;
    for (const auto& cluster : clusters.Clusters) {
        CHECK(cluster.IndexCount > 0);
        CHECK(cluster.IndexCount % 3 == 0);
        CHECK(cluster.IndexCount <= maxTriangles * 3);
        // Clusters must be contiguous and in order, because a draw argument is
        // just (firstIndex, indexCount) into the merged buffer.
        CHECK(cluster.FirstIndex == previousEnd);
        previousEnd = cluster.FirstIndex + cluster.IndexCount;
        coveredIndices += cluster.IndexCount;

        CHECK(cluster.CenterRadius.w > 0.0f);
        // The cone is either a usable half-angle or explicitly disabled; a value
        // in between would cull clusters that are actually facing the camera.
        CHECK(cluster.ConeAxisCutoff.w <= 1.0f || cluster.ConeAxisCutoff.w >= 2.0f);
    }
    CHECK(coveredIndices == mesh->indices.size());
    CHECK(previousEnd == mesh->indices.size());

    // Every index still has to be a valid vertex reference after reordering.
    for (uint32_t index : clusters.ReorderedIndices) {
        CHECK(index < mesh->vertices.size());
    }
    CHECK(clusters.BoundsCenterRadius.w > 0.0f);
}

void TestPrimitivesAreWellFormed() {
    for (const char* kind : {"box", "sphere", "plane", "cylinder"}) {
        auto mesh = Mesh::CreatePrimitive(kind, 12);
        CHECK(mesh != nullptr);
        CHECK(!mesh->vertices.empty());
        CHECK(mesh->indices.size() >= 3);
        CHECK(mesh->indices.size() % 3 == 0);
        CHECK(mesh->primitives.size() == 1);
        CHECK(mesh->primitives[0].indexCount == mesh->indices.size());
        for (uint32_t index : mesh->indices) {
            CHECK(index < mesh->vertices.size());
        }
    }
    CHECK(Mesh::CreatePrimitive("not-a-primitive") == nullptr);
}

void TestClusterBuilderRejectsBadInput() {
    std::vector<uint32_t> indices = {0, 1, 2};
    std::vector<Vertex> vertices(3);

    CHECK(BuildMeshClusters(nullptr, sizeof(Vertex), 3, indices, 64).Clusters.empty());
    CHECK(BuildMeshClusters(vertices.data(), sizeof(Vertex), 0, indices, 64).Clusters.empty());
    CHECK(BuildMeshClusters(vertices.data(), sizeof(Vertex), 3, {}, 64).Clusters.empty());
    CHECK(BuildMeshClusters(vertices.data(), sizeof(Vertex), 3, indices, 0).Clusters.empty());
}

} // namespace

int main() {
    Engine::Log::Init();

    TestDefaultGraphCompiles();
    TestUnconnectedInputsFallBack();
    TestCycleIsRejected();
    TestMissingOutputIsRejected();
    TestTextureSlotsAreDeduplicated();
    TestJsonRoundTrip();
    TestMalformedJsonIsRejected();
    TestLibraryIndexing();
    TestPrimitivesAreWellFormed();
    TestClusterBuilderCoversEveryTriangle();
    TestClusterBuilderRejectsBadInput();

    std::printf("RenderPipelineTests: all checks passed\n");
    return 0;
}
