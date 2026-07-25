#include "Model.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace object_view {
namespace {

std::string trim(const std::string& value) {
  auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch);
  });
  auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch);
  }).base();

  if (begin >= end) {
    return "";
  }

  return std::string(begin, end);
}

std::string jsonEscape(const std::string& value) {
  std::ostringstream output;
  for (char ch : value) {
    switch (ch) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        output << ch;
        break;
    }
  }
  return output.str();
}

std::string firstFaceIndex(const std::string& token) {
  const auto slash = token.find('/');
  if (slash == std::string::npos) {
    return token;
  }
  return token.substr(0, slash);
}

bool hasValidVertexIndex(const std::string& token) {
  const auto index = firstFaceIndex(token);
  if (index.empty()) {
    return false;
  }

  char* end = nullptr;
  std::strtol(index.c_str(), &end, 10);
  return end != index.c_str() && *end == '\0';
}

int resolveObjIndex(int index, std::size_t size) {
  if (index > 0) {
    return index - 1;
  }

  if (index < 0) {
    return static_cast<int>(size) + index;
  }

  return -1;
}

FaceVertex parseFaceVertex(
    const std::string& token,
    std::size_t vertexCount,
    std::size_t texCoordCount,
    std::size_t normalCount) {
  FaceVertex result;
  std::stringstream tokenStream(token);
  std::string part;

  if (std::getline(tokenStream, part, '/') && !part.empty()) {
    result.vertexIndex = resolveObjIndex(std::stoi(part), vertexCount);
  }

  if (std::getline(tokenStream, part, '/') && !part.empty()) {
    result.texCoordIndex = resolveObjIndex(std::stoi(part), texCoordCount);
  }

  if (std::getline(tokenStream, part, '/') && !part.empty()) {
    result.normalIndex = resolveObjIndex(std::stoi(part), normalCount);
  }

  return result;
}

void writeVector(std::ostringstream& output, const Vector3& vector) {
  output << "{\"x\":" << vector.x
         << ",\"y\":" << vector.y
         << ",\"z\":" << vector.z << "}";
}

void parseRgb(std::istringstream& line, std::array<double, 3>& color) {
  line >> color[0] >> color[1] >> color[2];
}

}  // namespace

void Model::reset(std::string sourceName) {
  stats_ = ModelStats{};
  stats_.sourceName = std::move(sourceName);
  baseDirectory_.clear();
  vertices_.clear();
  normals_.clear();
  texCoords_.clear();
  faces_.clear();
}

bool Model::loadObject(const std::filesystem::path& filename, std::string& error) {
  std::ifstream input(filename);
  if (!input) {
    error = "Could not open OBJ file: " + filename.string();
    return false;
  }

  reset(filename.filename().string());
  return parseObjStream(input, filename.parent_path(), error);
}

bool Model::loadFromString(
    const std::string& source,
    const std::filesystem::path& baseDirectory,
    std::string sourceName,
    std::string& error) {
  reset(std::move(sourceName));
  std::istringstream input(source);
  return parseObjStream(input, baseDirectory, error);
}

bool Model::parseObjStream(
    std::istream& input,
    const std::filesystem::path& baseDirectory,
    std::string& error) {
  std::unordered_set<std::string> seenGroups;
  baseDirectory_ = baseDirectory;
  std::string currentGroup;
  int currentMaterialIndex = -1;
  std::string line;

  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    std::istringstream parsedLine(line);
    std::string keyword;
    parsedLine >> keyword;

    if (keyword == "v") {
      Vector3 vertex;
      if (!(parsedLine >> vertex.x >> vertex.y >> vertex.z)) {
        error = "OBJ contains a malformed vertex line.";
        return false;
      }

      ++stats_.vertexCount;
      vertices_.push_back(vertex);
      updateBounds(vertex);
    } else if (keyword == "vn") {
      Vector3 normal;
      parsedLine >> normal.x >> normal.y >> normal.z;
      normals_.push_back(normal);
      ++stats_.normalCount;
    } else if (keyword == "vt") {
      TexCoord texCoord;
      parsedLine >> texCoord.u >> texCoord.v >> texCoord.w;
      texCoords_.push_back(texCoord);
      ++stats_.uvCount;
    } else if (keyword == "f") {
      Face face;
      face.materialIndex = currentMaterialIndex;
      face.group = currentGroup;
      std::string token;
      while (parsedLine >> token) {
        if (hasValidVertexIndex(token)) {
          FaceVertex faceVertex = parseFaceVertex(
              token,
              vertices_.size(),
              texCoords_.size(),
              normals_.size());

          if (faceVertex.vertexIndex >= 0 &&
              faceVertex.vertexIndex < static_cast<int>(vertices_.size())) {
            face.vertices.push_back(faceVertex);
          }
        }
      }

      if (face.vertices.size() >= 3) {
        faces_.push_back(face);
        ++stats_.faceCount;
        stats_.triangleCount += face.vertices.size() - 2;
      }
    } else if (keyword == "g" || keyword == "o") {
      std::string groupName;
      parsedLine >> groupName;
      if (!groupName.empty() && seenGroups.insert(groupName).second) {
        stats_.groups.push_back(groupName);
      }
      currentGroup = groupName;
    } else if (keyword == "usemtl") {
      std::string materialName;
      parsedLine >> materialName;
      currentMaterialIndex = materialIndexByName(materialName);
    } else if (keyword == "mtllib") {
      std::string materialFile;
      while (parsedLine >> materialFile) {
        stats_.materialLibraries.push_back(materialFile);
        loadMaterials(baseDirectory / materialFile);
      }
    }
  }

  if (stats_.vertexCount == 0) {
    error = "No vertices were found. ObjectView currently inspects OBJ geometry.";
    return false;
  }

  finishBounds();
  return true;
}

void Model::loadMaterials(const std::filesystem::path& filename) {
  std::ifstream input(filename);
  if (!input) {
    return;
  }

  Material* currentMaterial = nullptr;
  std::string line;

  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    std::istringstream parsedLine(line);
    std::string keyword;
    parsedLine >> keyword;

    if (keyword == "newmtl") {
      Material material;
      parsedLine >> material.name;
      stats_.materials.push_back(material);
      currentMaterial = &stats_.materials.back();
    } else if (!currentMaterial) {
      continue;
    } else if (keyword == "Ka") {
      parseRgb(parsedLine, currentMaterial->ambient);
    } else if (keyword == "Kd") {
      parseRgb(parsedLine, currentMaterial->diffuse);
    } else if (keyword == "Ks") {
      parseRgb(parsedLine, currentMaterial->specular);
    } else if (keyword == "Ns") {
      parsedLine >> currentMaterial->shininess;
    } else if (keyword == "d" || keyword == "Tr") {
      parsedLine >> currentMaterial->alpha;
    } else if (keyword.rfind("map_", 0) == 0) {
      std::string texturePath;
      parsedLine >> texturePath;
      if (!texturePath.empty()) {
        if (keyword == "map_Kd") {
          currentMaterial->diffuseTexture = texturePath;
        }
        currentMaterial->textureMaps.push_back(texturePath);
      }
    }
  }
}

int Model::materialIndexByName(const std::string& name) const {
  for (std::size_t index = 0; index < stats_.materials.size(); ++index) {
    if (stats_.materials[index].name == name) {
      return static_cast<int>(index);
    }
  }

  return -1;
}

void Model::updateBounds(const Vector3& vertex) {
  if (!stats_.bounds.valid) {
    stats_.bounds.valid = true;
    stats_.bounds.min = vertex;
    stats_.bounds.max = vertex;
    return;
  }

  stats_.bounds.min.x = std::min(stats_.bounds.min.x, vertex.x);
  stats_.bounds.min.y = std::min(stats_.bounds.min.y, vertex.y);
  stats_.bounds.min.z = std::min(stats_.bounds.min.z, vertex.z);
  stats_.bounds.max.x = std::max(stats_.bounds.max.x, vertex.x);
  stats_.bounds.max.y = std::max(stats_.bounds.max.y, vertex.y);
  stats_.bounds.max.z = std::max(stats_.bounds.max.z, vertex.z);
}

void Model::finishBounds() {
  if (!stats_.bounds.valid || stats_.vertexCount == 0) {
    return;
  }

  stats_.bounds.center = (stats_.bounds.min + stats_.bounds.max) / 2.0;
  stats_.bounds.radius = (stats_.bounds.max - stats_.bounds.min).length() / 2.0;
}

std::string toJson(const ModelStats& stats) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(5);
  output << "{";
  output << "\"sourceName\":\"" << jsonEscape(stats.sourceName) << "\",";
  output << "\"vertexCount\":" << stats.vertexCount << ",";
  output << "\"normalCount\":" << stats.normalCount << ",";
  output << "\"uvCount\":" << stats.uvCount << ",";
  output << "\"faceCount\":" << stats.faceCount << ",";
  output << "\"triangleCount\":" << stats.triangleCount << ",";

  output << "\"groups\":[";
  for (std::size_t index = 0; index < stats.groups.size(); ++index) {
    if (index > 0) {
      output << ",";
    }
    output << "\"" << jsonEscape(stats.groups[index]) << "\"";
  }
  output << "],";

  output << "\"materialLibraries\":[";
  for (std::size_t index = 0; index < stats.materialLibraries.size(); ++index) {
    if (index > 0) {
      output << ",";
    }
    output << "\"" << jsonEscape(stats.materialLibraries[index]) << "\"";
  }
  output << "],";

  output << "\"materials\":[";
  for (std::size_t index = 0; index < stats.materials.size(); ++index) {
    const auto& material = stats.materials[index];
    if (index > 0) {
      output << ",";
    }
    output << "{\"name\":\"" << jsonEscape(material.name) << "\",";
    output << "\"alpha\":" << material.alpha << ",";
    output << "\"shininess\":" << material.shininess << ",";
    output << "\"textureMaps\":[";
    for (std::size_t textureIndex = 0; textureIndex < material.textureMaps.size(); ++textureIndex) {
      if (textureIndex > 0) {
        output << ",";
      }
      output << "\"" << jsonEscape(material.textureMaps[textureIndex]) << "\"";
    }
    output << "]}";
  }
  output << "],";

  output << "\"bounds\":{";
  output << "\"valid\":" << (stats.bounds.valid ? "true" : "false") << ",";
  output << "\"min\":";
  writeVector(output, stats.bounds.min);
  output << ",\"max\":";
  writeVector(output, stats.bounds.max);
  output << ",\"center\":";
  writeVector(output, stats.bounds.center);
  output << ",\"radius\":" << stats.bounds.radius;
  output << "}";

  output << "}";
  return output.str();
}

}  // namespace object_view
