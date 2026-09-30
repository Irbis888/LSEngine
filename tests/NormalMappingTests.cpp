#include "ResourceManager.h"
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>

static_assert(offsetof(Vertex, TangentU) == 24);
static_assert(offsetof(Vertex, TexC) == 40);
static_assert(sizeof(Vertex) == 48);

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("Expected portrait OBJ path");
        ResourceManager resources;
        const Mesh& mesh = resources.GetMesh(resources.LoadMesh(argv[1]));
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(argv[1], aiProcess_Triangulate |
            aiProcess_ConvertToLeftHanded | aiProcess_GenNormals | aiProcess_CalcTangentSpace);
        if (!scene)
            throw std::runtime_error(importer.GetErrorString());

        size_t offset = 0, positive = 0, negative = 0;
        for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh& imported = *scene->mMeshes[m];
            if (!imported.HasTangentsAndBitangents())
                throw std::runtime_error("Portrait tangents were not generated");
            for (unsigned int i = 0; i < imported.mNumVertices; ++i)
            {
                const Vertex& v = mesh.vertices.at(offset++);
                const glm::vec3 t(v.TangentU);
                const aiVector3D& b = imported.mBitangents[i];
                const glm::vec3 expected(b.x, b.y, b.z);
                const glm::vec3 actual = v.TangentU.w * glm::cross(v.Normal, t);
                const float agreement = glm::dot(actual, expected);
                // Assimp's UV basis can be skewed, but its orientation must agree.
                if (!std::isfinite(agreement) || agreement <= 0.0f)
                    throw std::runtime_error("Reconstructed bitangent disagrees with Assimp");
                if (v.TangentU.w < 0.0f) ++negative; else ++positive;
            }
        }
        if (offset != mesh.vertices.size() || negative == 0)
            throw std::runtime_error("Portrait regression did not exercise negative handedness");
        const Mesh& plane = resources.GetMesh(resources.CreatePlane(0));
        for (const Vertex& v : plane.vertices)
            if (v.TangentU.w != 1.0f)
                throw std::runtime_error("Primitive tangent convention changed");
        std::cout << "Normal mapping passed: " << positive << " positive, " << negative
                  << " negative tangent signs; vertex layout and primitive defaults verified.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
