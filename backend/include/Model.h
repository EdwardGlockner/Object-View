#pragma once

#include "Vector3.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace object_view {

struct Bounds {
  bool valid = false;
  Vector3 min;
  Vector3 max;
  Vector3 center;
  double radius = 0.0;
};

struct Material {
  std::string name;
  std::array<double, 3> ambient = {0.0, 0.0, 0.0};
  std::array<double, 3> diffuse = {1.0, 1.0, 1.0};
  std::array<double, 3> specular = {0.0, 0.0, 0.0};
  double alpha = 1.0;
  double shininess = 2.0;
  std::string diffuseTexture;
  std::vector<std::string> textureMaps;
};

struct TexCoord {
  double u = 0.0;
  double v = 0.0;
  double w = 0.0;
};

struct FaceVertex {
  int vertexIndex = -1;
  int texCoordIndex = -1;
  int normalIndex = -1;
};

struct Face {
  std::vector<FaceVertex> vertices;
  int materialIndex = -1;
  std::string group;
};

struct ModelStats {
  std::string sourceName;
  std::size_t vertexCount = 0;
  std::size_t normalCount = 0;
  std::size_t uvCount = 0;
  std::size_t faceCount = 0;
  std::size_t triangleCount = 0;
  std::vector<std::string> groups;
  std::vector<std::string> materialLibraries;
  std::vector<Material> materials;
  Bounds bounds;
};

class Model {
 public:
  bool loadObject(const std::filesystem::path& filename, std::string& error);
  bool loadFromString(
      const std::string& source,
      const std::filesystem::path& baseDirectory,
      std::string sourceName,
      std::string& error);

  const ModelStats& stats() const { return stats_; }
  const std::filesystem::path& baseDirectory() const { return baseDirectory_; }
  const std::vector<Vector3>& vertices() const { return vertices_; }
  const std::vector<Vector3>& normals() const { return normals_; }
  const std::vector<TexCoord>& texCoords() const { return texCoords_; }
  const std::vector<Face>& faces() const { return faces_; }
  const std::vector<Material>& materials() const { return stats_.materials; }

 private:
  void reset(std::string sourceName);
  bool parseObjStream(std::istream& input, const std::filesystem::path& baseDirectory, std::string& error);
  void loadMaterials(const std::filesystem::path& filename);
  int materialIndexByName(const std::string& name) const;
  void updateBounds(const Vector3& vertex);
  void finishBounds();

  ModelStats stats_;
  std::filesystem::path baseDirectory_;
  std::vector<Vector3> vertices_;
  std::vector<Vector3> normals_;
  std::vector<TexCoord> texCoords_;
  std::vector<Face> faces_;
};

std::string toJson(const ModelStats& stats);

}  // namespace object_view
