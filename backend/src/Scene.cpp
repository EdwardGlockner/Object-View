#include "Scene.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace object_view {
namespace {

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;

  bool isObject() const { return type == Type::Object; }
  bool isArray() const { return type == Type::Array; }
  bool isString() const { return type == Type::String; }
  bool isNumber() const { return type == Type::Number; }
};

class JsonParser {
 public:
  explicit JsonParser(std::string source) : source_(std::move(source)) {}

  JsonValue parse() {
    JsonValue value = parseValue();
    skipWhitespace();
    if (position_ != source_.size()) {
      throw std::runtime_error("Unexpected trailing JSON content.");
    }
    return value;
  }

 private:
  JsonValue parseValue() {
    skipWhitespace();
    if (position_ >= source_.size()) {
      throw std::runtime_error("Unexpected end of JSON.");
    }

    const char ch = source_[position_];
    if (ch == '{') return parseObject();
    if (ch == '[') return parseArray();
    if (ch == '"') return parseString();
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return parseNumber();
    if (consumeLiteral("true")) {
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolean = true;
      return value;
    }
    if (consumeLiteral("false")) {
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolean = false;
      return value;
    }
    if (consumeLiteral("null")) {
      return {};
    }

    throw std::runtime_error("Unexpected JSON token.");
  }

  JsonValue parseObject() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    expect('{');
    skipWhitespace();
    if (peek('}')) {
      expect('}');
      return value;
    }

    while (true) {
      JsonValue key = parseString();
      skipWhitespace();
      expect(':');
      value.object[key.string] = parseValue();
      skipWhitespace();
      if (peek('}')) {
        expect('}');
        return value;
      }
      expect(',');
    }
  }

  JsonValue parseArray() {
    JsonValue value;
    value.type = JsonValue::Type::Array;
    expect('[');
    skipWhitespace();
    if (peek(']')) {
      expect(']');
      return value;
    }

    while (true) {
      value.array.push_back(parseValue());
      skipWhitespace();
      if (peek(']')) {
        expect(']');
        return value;
      }
      expect(',');
    }
  }

  JsonValue parseString() {
    JsonValue value;
    value.type = JsonValue::Type::String;
    expect('"');
    while (position_ < source_.size()) {
      const char ch = source_[position_++];
      if (ch == '"') {
        return value;
      }
      if (ch == '\\') {
        if (position_ >= source_.size()) {
          throw std::runtime_error("Invalid JSON string escape.");
        }
        const char escaped = source_[position_++];
        switch (escaped) {
          case '"':
          case '\\':
          case '/':
            value.string.push_back(escaped);
            break;
          case 'b':
            value.string.push_back('\b');
            break;
          case 'f':
            value.string.push_back('\f');
            break;
          case 'n':
            value.string.push_back('\n');
            break;
          case 'r':
            value.string.push_back('\r');
            break;
          case 't':
            value.string.push_back('\t');
            break;
          default:
            throw std::runtime_error("Unsupported JSON string escape.");
        }
      } else {
        value.string.push_back(ch);
      }
    }
    throw std::runtime_error("Unterminated JSON string.");
  }

  JsonValue parseNumber() {
    const char* begin = source_.c_str() + position_;
    char* end = nullptr;
    const double number = std::strtod(begin, &end);
    if (end == begin) {
      throw std::runtime_error("Invalid JSON number.");
    }

    position_ = static_cast<std::size_t>(end - source_.c_str());
    JsonValue value;
    value.type = JsonValue::Type::Number;
    value.number = number;
    return value;
  }

  bool consumeLiteral(const char* literal) {
    const std::string text(literal);
    if (source_.compare(position_, text.size(), text) == 0) {
      position_ += text.size();
      return true;
    }
    return false;
  }

  bool peek(char expected) {
    skipWhitespace();
    return position_ < source_.size() && source_[position_] == expected;
  }

  void expect(char expected) {
    skipWhitespace();
    if (position_ >= source_.size() || source_[position_] != expected) {
      throw std::runtime_error("Unexpected JSON character.");
    }
    ++position_;
  }

  void skipWhitespace() {
    while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) {
      ++position_;
    }
  }

  std::string source_;
  std::size_t position_ = 0;
};

const JsonValue* member(const JsonValue& value, const std::string& name) {
  if (!value.isObject()) return nullptr;
  const auto it = value.object.find(name);
  return it == value.object.end() ? nullptr : &it->second;
}

std::string stringMember(const JsonValue& value, const std::string& name, const std::string& fallback = "") {
  const JsonValue* item = member(value, name);
  return item && item->isString() ? item->string : fallback;
}

double numberMember(const JsonValue& value, const std::string& name, double fallback = 0.0) {
  const JsonValue* item = member(value, name);
  return item && item->isNumber() ? item->number : fallback;
}

Vector3 vectorFromArray(const JsonValue* value, Vector3 fallback = {}) {
  if (!value || !value->isArray() || value->array.size() < 3) {
    return fallback;
  }
  if (!value->array[0].isNumber() || !value->array[1].isNumber() || !value->array[2].isNumber()) {
    return fallback;
  }
  return {value->array[0].number, value->array[1].number, value->array[2].number};
}

Color colorFromValue(const JsonValue* value, Color fallback = {}) {
  if (!value || !value->isArray() || value->array.size() < 3) {
    return fallback;
  }
  Color color = fallback;
  if (value->array[0].isNumber()) color.r = value->array[0].number;
  if (value->array[1].isNumber()) color.g = value->array[1].number;
  if (value->array[2].isNumber()) color.b = value->array[2].number;
  if (value->array.size() > 3 && value->array[3].isNumber()) color.a = value->array[3].number;
  return color;
}

Transform transformFromValue(const JsonValue* value) {
  Transform transform;
  if (!value || !value->isObject()) {
    return transform;
  }

  transform.position = vectorFromArray(member(*value, "position"));
  transform.rotation = vectorFromArray(member(*value, "rotation"));
  transform.scale = numberMember(*value, "scale", 1.0);
  return transform;
}

std::vector<Vector3> pointsFromValue(const JsonValue* value) {
  std::vector<Vector3> points;
  if (!value || !value->isArray()) {
    return points;
  }

  for (const JsonValue& point : value->array) {
    points.push_back(vectorFromArray(&point));
  }
  return points;
}

}  // namespace

bool loadScene(const std::filesystem::path& filename, Scene& scene, std::string& error) {
  std::ifstream input(filename);
  if (!input) {
    error = "Could not open scene file: " + filename.string();
    return false;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  JsonValue root;
  try {
    root = JsonParser(buffer.str()).parse();
  } catch (const std::exception& exception) {
    error = "Could not parse scene JSON: " + std::string(exception.what());
    return false;
  }

  if (!root.isObject()) {
    error = "Scene JSON root must be an object.";
    return false;
  }

  scene = Scene{};
  scene.sourcePath = filename;
  scene.name = stringMember(root, "name", filename.filename().string());
  const auto baseDirectory = filename.parent_path();

  if (const JsonValue* objects = member(root, "objects"); objects && objects->isArray()) {
    for (const JsonValue& item : objects->array) {
      if (!item.isObject()) continue;
      SceneObject object;
      object.id = stringMember(item, "id");
      object.asset = baseDirectory / stringMember(item, "asset");
      object.transform = transformFromValue(member(item, "transform"));

      if (const JsonValue* keyframes = member(item, "keyframes"); keyframes && keyframes->isArray()) {
        for (const JsonValue& keyframeItem : keyframes->array) {
          if (!keyframeItem.isObject()) continue;
          Keyframe keyframe;
          keyframe.time = numberMember(keyframeItem, "t", 0.0);
          keyframe.transform = transformFromValue(&keyframeItem);
          object.keyframes.push_back(keyframe);
        }
        std::sort(object.keyframes.begin(), object.keyframes.end(), [](const Keyframe& a, const Keyframe& b) {
          return a.time < b.time;
        });
      }

      if (const JsonValue* ghosts = member(item, "ghosts"); ghosts && ghosts->isArray()) {
        for (const JsonValue& ghostItem : ghosts->array) {
          if (!ghostItem.isObject()) continue;
          Ghost ghost;
          ghost.transform = transformFromValue(member(ghostItem, "transform"));
          ghost.opacity = numberMember(ghostItem, "opacity", ghost.opacity);
          object.ghosts.push_back(ghost);
        }
      }

      scene.objects.push_back(object);
    }
  }

  if (const JsonValue* paths = member(root, "paths"); paths && paths->isArray()) {
    for (const JsonValue& item : paths->array) {
      if (!item.isObject()) continue;
      ScenePath path;
      path.id = stringMember(item, "id");
      path.points = pointsFromValue(member(item, "points"));
      path.color = colorFromValue(member(item, "color"), path.color);
      path.width = numberMember(item, "width", path.width);
      scene.paths.push_back(path);
    }
  }

  if (const JsonValue* vectors = member(root, "vectors"); vectors && vectors->isArray()) {
    for (const JsonValue& item : vectors->array) {
      if (!item.isObject()) continue;
      SceneVector vector;
      vector.id = stringMember(item, "id");
      vector.origin = vectorFromArray(member(item, "origin"));
      vector.direction = vectorFromArray(member(item, "direction"));
      vector.color = colorFromValue(member(item, "color"), vector.color);
      vector.scale = numberMember(item, "scale", vector.scale);
      scene.vectors.push_back(vector);
    }
  }

  if (const JsonValue* frames = member(root, "frames"); frames && frames->isArray()) {
    for (const JsonValue& item : frames->array) {
      if (!item.isObject()) continue;
      SceneFrame frame;
      frame.id = stringMember(item, "id");
      frame.transform = transformFromValue(member(item, "transform"));
      frame.size = numberMember(item, "size", frame.size);
      scene.frames.push_back(frame);
    }
  }

  if (const JsonValue* markers = member(root, "markers"); markers && markers->isArray()) {
    for (const JsonValue& item : markers->array) {
      if (!item.isObject()) continue;
      SceneMarker marker;
      marker.id = stringMember(item, "id");
      marker.position = vectorFromArray(member(item, "position"));
      marker.color = colorFromValue(member(item, "color"), marker.color);
      marker.size = numberMember(item, "size", marker.size);
      scene.markers.push_back(marker);
    }
  }

  if (const JsonValue* heatmaps = member(root, "heatmaps"); heatmaps && heatmaps->isArray()) {
    for (const JsonValue& item : heatmaps->array) {
      if (!item.isObject()) continue;
      SceneHeatmap heatmap;
      heatmap.id = stringMember(item, "id");
      heatmap.origin = vectorFromArray(member(item, "origin"));
      heatmap.cellSize = numberMember(item, "cellSize", heatmap.cellSize);
      heatmap.rows = static_cast<int>(numberMember(item, "rows", 0.0));
      heatmap.cols = static_cast<int>(numberMember(item, "cols", 0.0));
      heatmap.colorLow = colorFromValue(member(item, "colorLow"), heatmap.colorLow);
      heatmap.colorHigh = colorFromValue(member(item, "colorHigh"), heatmap.colorHigh);

      if (const JsonValue* values = member(item, "values"); values && values->isArray()) {
        for (const JsonValue& valueItem : values->array) {
          if (valueItem.isNumber()) heatmap.values.push_back(valueItem.number);
        }
      }

      scene.heatmaps.push_back(heatmap);
    }
  }

  if (scene.objects.empty() && scene.paths.empty() && scene.vectors.empty() && scene.frames.empty() &&
      scene.markers.empty() && scene.heatmaps.empty()) {
    error = "Scene contains no renderable objects, paths, vectors, frames, markers, or heatmaps.";
    return false;
  }

  return true;
}

}  // namespace object_view

