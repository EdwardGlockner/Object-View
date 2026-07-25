#pragma once

#include "Vector3.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace object_view {

struct Color {
  double r = 1.0;
  double g = 1.0;
  double b = 1.0;
  double a = 1.0;
};

struct Transform {
  Vector3 position;
  Vector3 rotation;
  double scale = 1.0;
};

struct Keyframe {
  double time = 0.0;
  Transform transform;
};

struct Ghost {
  Transform transform;
  double opacity = 0.35;
};

struct SceneObject {
  std::string id;
  std::filesystem::path asset;
  Transform transform;
  std::vector<Keyframe> keyframes;
  std::vector<Ghost> ghosts;
};

struct ScenePath {
  std::string id;
  std::vector<Vector3> points;
  Color color = {0.44, 0.91, 0.77, 1.0};
  double width = 2.0;
};

struct SceneVector {
  std::string id;
  Vector3 origin;
  Vector3 direction;
  Color color = {0.94, 0.70, 0.36, 1.0};
  double scale = 1.0;
};

struct SceneFrame {
  std::string id;
  Transform transform;
  double size = 0.6;
};

struct SceneMarker {
  std::string id;
  Vector3 position;
  Color color = {0.94, 0.70, 0.36, 1.0};
  double size = 0.08;
};

struct SceneHeatmap {
  std::string id;
  Vector3 origin;
  double cellSize = 0.5;
  int rows = 0;
  int cols = 0;
  std::vector<double> values;
  Color colorLow = {0.16, 0.32, 0.85, 0.55};
  Color colorHigh = {0.95, 0.25, 0.18, 0.85};
};

struct Scene {
  std::string name = "ObjectView scene";
  std::filesystem::path sourcePath;
  std::vector<SceneObject> objects;
  std::vector<ScenePath> paths;
  std::vector<SceneVector> vectors;
  std::vector<SceneFrame> frames;
  std::vector<SceneMarker> markers;
  std::vector<SceneHeatmap> heatmaps;
};

bool loadScene(const std::filesystem::path& filename, Scene& scene, std::string& error);

}  // namespace object_view

