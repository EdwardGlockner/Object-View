#include "Model.h"
#include "Scene.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <propidl.h>
#include <gdiplus.h>
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kPanelWidth = 340;
#ifndef GL_GENERATE_MIPMAP
constexpr GLenum GL_GENERATE_MIPMAP = 0x8191;
#endif

struct CameraState {
  bool dragging = false;
  bool panning = false;
  double lastX = 0.0;
  double lastY = 0.0;
};

struct ModelState {
  float rotationX = -4.6f;
  float rotationY = 41.2f;
  float positionX = 0.0f;
  float positionY = 0.0f;
  float positionZ = 0.0f;
  float zoom = 1.0f;
  bool autoSpin = false;
  bool wireframe = false;
  bool modelVisible = true;
  bool detailOverlay = true;
};

struct TextureStore {
  std::map<std::string, GLuint> textures;
  GLuint checkerTexture = 0;
};

struct LoadedSceneObject {
  object_view::SceneObject object;
  object_view::Model model;
};

struct LoadedScene {
  object_view::Scene scene;
  std::vector<LoadedSceneObject> objects;
  double radius = 1.0;
  double duration = 0.0;
  std::size_t triangleCount = 0;
  object_view::Vector3 pivot;
};

struct PlaybackState {
  bool playing = false;
  bool loop = true;
  float time = 0.0f;
};

CameraState camera;
ModelState modelState;
PlaybackState playback;
double timelineDuration = 0.0;
bool timelineVisible = false;
float materialAlphaMultiplier = 1.0f;
bool legendVisible = false;
bool legendHasObjects = false;
bool legendHasPaths = false;
bool legendHasVectors = false;
bool legendHasGhosts = false;
bool legendHasMarkers = false;
bool legendHasFrames = false;
bool legendHasHeatmaps = false;
int legendRowCount = 0;
GLuint fontBase = 0;
bool screenshotRequested = false;
bool screenshotKeyWasDown = false;
std::string lastScreenshotName;

#ifdef _WIN32
class GdiPlusSession {
 public:
  GdiPlusSession() {
    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(&token_, &startupInput, nullptr) == Gdiplus::Ok) {
      active_ = true;
    }
  }

  ~GdiPlusSession() {
    if (active_) {
      Gdiplus::GdiplusShutdown(token_);
    }
  }

  bool active() const { return active_; }

 private:
  ULONG_PTR token_ = 0;
  bool active_ = false;
};
#endif

float clampf(float value, float minimum, float maximum) {
  return std::max(minimum, std::min(maximum, value));
}

void setPerspective(float fovDegrees, float aspect, float nearPlane, float farPlane) {
  const float top = std::tan(fovDegrees * static_cast<float>(kPi) / 360.0f) * nearPlane;
  const float bottom = -top;
  const float right = top * aspect;
  const float left = -right;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(left, right, bottom, top, nearPlane, farPlane);
  glMatrixMode(GL_MODELVIEW);
}

void setOrtho2D(int width, int height) {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, static_cast<double>(width), static_cast<double>(height), 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void setLookAt(
    float eyeX,
    float eyeY,
    float eyeZ,
    float centerX,
    float centerY,
    float centerZ,
    float upX,
    float upY,
    float upZ) {
  const float forwardX = centerX - eyeX;
  const float forwardY = centerY - eyeY;
  const float forwardZ = centerZ - eyeZ;
  const float forwardLength = std::sqrt(forwardX * forwardX + forwardY * forwardY + forwardZ * forwardZ);
  const float fx = forwardX / forwardLength;
  const float fy = forwardY / forwardLength;
  const float fz = forwardZ / forwardLength;

  float sideX = fy * upZ - fz * upY;
  float sideY = fz * upX - fx * upZ;
  float sideZ = fx * upY - fy * upX;
  const float sideLength = std::sqrt(sideX * sideX + sideY * sideY + sideZ * sideZ);
  sideX /= sideLength;
  sideY /= sideLength;
  sideZ /= sideLength;

  const float trueUpX = sideY * fz - sideZ * fy;
  const float trueUpY = sideZ * fx - sideX * fz;
  const float trueUpZ = sideX * fy - sideY * fx;

  const GLfloat matrix[] = {
      sideX, trueUpX, -fx, 0.0f,
      sideY, trueUpY, -fy, 0.0f,
      sideZ, trueUpZ, -fz, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f};

  glMultMatrixf(matrix);
  glTranslatef(-eyeX, -eyeY, -eyeZ);
}

GLuint createCheckerTexture() {
  constexpr int size = 64;
  std::vector<unsigned char> pixels(size * size * 3);

  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const bool bright = ((x / 8) + (y / 8)) % 2 == 0;
      const int offset = (y * size + x) * 3;
      pixels[offset + 0] = bright ? 230 : 45;
      pixels[offset + 1] = bright ? 174 : 45;
      pixels[offset + 2] = bright ? 92 : 45;
    }
  }

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture;
}

#ifdef _WIN32
void initializeFont() {
  if (fontBase != 0) {
    return;
  }

  HDC deviceContext = wglGetCurrentDC();
  if (!deviceContext) {
    return;
  }

  HFONT font = CreateFontA(
      -16,
      0,
      0,
      0,
      FW_SEMIBOLD,
      FALSE,
      FALSE,
      FALSE,
      ANSI_CHARSET,
      OUT_TT_PRECIS,
      CLIP_DEFAULT_PRECIS,
      ANTIALIASED_QUALITY,
      FF_SWISS,
      "Segoe UI");

  fontBase = glGenLists(96);
  HGDIOBJ oldFont = SelectObject(deviceContext, font);
  wglUseFontBitmapsA(deviceContext, 32, 96, fontBase);
  SelectObject(deviceContext, oldFont);
  DeleteObject(font);
}
#else
void initializeFont() {}
#endif

bool loadPpm(const fs::path& path, int& width, int& height, std::vector<unsigned char>& pixels) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }

  std::string magic;
  input >> magic;
  if (magic != "P3" && magic != "P6") {
    return false;
  }

  input >> width >> height;
  int maxValue = 0;
  input >> maxValue;
  input.get();

  if (width <= 0 || height <= 0 || maxValue != 255) {
    return false;
  }

  pixels.resize(static_cast<std::size_t>(width * height * 3));
  if (magic == "P6") {
    input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    return input.good();
  }

  for (unsigned char& channel : pixels) {
    int value = 0;
    input >> value;
    channel = static_cast<unsigned char>(std::clamp(value, 0, 255));
  }
  return true;
}

#ifdef _WIN32
bool loadImageWithGdiPlus(const fs::path& path, int& width, int& height, std::vector<unsigned char>& pixels) {
  Gdiplus::Bitmap bitmap(path.wstring().c_str());
  if (bitmap.GetLastStatus() != Gdiplus::Ok) {
    return false;
  }

  width = static_cast<int>(bitmap.GetWidth());
  height = static_cast<int>(bitmap.GetHeight());
  if (width <= 0 || height <= 0) {
    return false;
  }

  Gdiplus::Rect rect(0, 0, width, height);
  Gdiplus::BitmapData bitmapData;
  if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok) {
    return false;
  }

  pixels.resize(static_cast<std::size_t>(width * height * 3));
  const auto* source = static_cast<const unsigned char*>(bitmapData.Scan0);
  const int stride = bitmapData.Stride;
  const auto* firstRow = stride < 0 ? source + static_cast<std::ptrdiff_t>(height - 1) * stride : source;

  for (int y = 0; y < height; ++y) {
    const auto* row = firstRow + static_cast<std::ptrdiff_t>(y) * std::abs(stride);
    for (int x = 0; x < width; ++x) {
      const int sourceOffset = x * 4;
      const int targetOffset = (y * width + x) * 3;
      pixels[static_cast<std::size_t>(targetOffset + 0)] = row[sourceOffset + 2];
      pixels[static_cast<std::size_t>(targetOffset + 1)] = row[sourceOffset + 1];
      pixels[static_cast<std::size_t>(targetOffset + 2)] = row[sourceOffset + 0];
    }
  }

  bitmap.UnlockBits(&bitmapData);
  return true;
}
#endif

std::tm localTime(std::time_t value) {
  std::tm result{};
#ifdef _WIN32
  localtime_s(&result, &value);
#else
  localtime_r(&value, &result);
#endif
  return result;
}

void writeUint16(std::ofstream& output, std::uint16_t value) {
  output.put(static_cast<char>(value & 0xff));
  output.put(static_cast<char>((value >> 8) & 0xff));
}

void writeUint32(std::ofstream& output, std::uint32_t value) {
  output.put(static_cast<char>(value & 0xff));
  output.put(static_cast<char>((value >> 8) & 0xff));
  output.put(static_cast<char>((value >> 16) & 0xff));
  output.put(static_cast<char>((value >> 24) & 0xff));
}

std::string saveScreenshotBmp(int width, int height) {
  if (width <= 0 || height <= 0) {
    return "";
  }

  fs::create_directories("screenshots");

  const auto now = std::chrono::system_clock::now();
  const auto timestamp = std::chrono::system_clock::to_time_t(now);
  const auto time = localTime(timestamp);
  std::ostringstream filename;
  filename << "objectview-" << std::put_time(&time, "%Y%m%d-%H%M%S") << ".bmp";
  const fs::path outputPath = fs::path("screenshots") / filename.str();

  std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3));
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

  const int paddingSize = (4 - (width * 3) % 4) % 4;
  const std::uint32_t pixelDataSize = static_cast<std::uint32_t>((width * 3 + paddingSize) * height);
  const std::uint32_t fileSize = 54 + pixelDataSize;
  const unsigned char padding[3] = {0, 0, 0};

  std::ofstream output(outputPath, std::ios::binary);
  if (!output) {
    return "";
  }

  output.put('B');
  output.put('M');
  writeUint32(output, fileSize);
  writeUint16(output, 0);
  writeUint16(output, 0);
  writeUint32(output, 54);
  writeUint32(output, 40);
  writeUint32(output, static_cast<std::uint32_t>(width));
  writeUint32(output, static_cast<std::uint32_t>(height));
  writeUint16(output, 1);
  writeUint16(output, 24);
  writeUint32(output, 0);
  writeUint32(output, pixelDataSize);
  writeUint32(output, 2835);
  writeUint32(output, 2835);
  writeUint32(output, 0);
  writeUint32(output, 0);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int offset = (y * width + x) * 3;
      output.put(static_cast<char>(pixels[static_cast<std::size_t>(offset + 2)]));
      output.put(static_cast<char>(pixels[static_cast<std::size_t>(offset + 1)]));
      output.put(static_cast<char>(pixels[static_cast<std::size_t>(offset + 0)]));
    }
    output.write(reinterpret_cast<const char*>(padding), paddingSize);
  }

  return filename.str();
}

std::string panelStatus(const std::string& title) {
  if (!lastScreenshotName.empty()) {
    return lastScreenshotName == "screenshot failed" ? "Screenshot failed." : "Screenshot saved.";
  }

  if (!modelState.modelVisible) {
    return "Scene cleared.";
  }

  const std::size_t maxLength = 25;
  if (title.size() > maxLength) {
    return title.substr(0, maxLength - 3) + "... loaded.";
  }

  return title + " loaded.";
}

GLuint loadTextureOrFallback(const fs::path& path, TextureStore& textureStore) {
  const std::string key = path.string();
  if (const auto it = textureStore.textures.find(key); it != textureStore.textures.end()) {
    return it->second;
  }

  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
  GLuint texture = textureStore.checkerTexture;

  const bool loaded =
#ifdef _WIN32
      loadImageWithGdiPlus(path, width, height, pixels) ||
#endif
      loadPpm(path, width, height, pixels);

  if (loaded) {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  textureStore.textures[key] = texture;
  return texture;
}

void applyMaterial(const object_view::Model& model, int materialIndex, TextureStore& textureStore) {
  GLfloat ambient[] = {0.04f, 0.04f, 0.04f, 1.0f};
  GLfloat diffuse[] = {0.82f, 0.78f, 0.68f, 1.0f};
  GLfloat specular[] = {0.18f, 0.18f, 0.18f, 1.0f};
  GLfloat shininess = 18.0f;

  bool useTexture = false;
  GLuint texture = textureStore.checkerTexture;

  if (materialIndex >= 0 && materialIndex < static_cast<int>(model.materials().size())) {
    const auto& material = model.materials()[static_cast<std::size_t>(materialIndex)];
    ambient[0] = static_cast<GLfloat>(material.ambient[0]);
    ambient[1] = static_cast<GLfloat>(material.ambient[1]);
    ambient[2] = static_cast<GLfloat>(material.ambient[2]);
    diffuse[0] = static_cast<GLfloat>(material.diffuse[0]);
    diffuse[1] = static_cast<GLfloat>(material.diffuse[1]);
    diffuse[2] = static_cast<GLfloat>(material.diffuse[2]);
    diffuse[3] = static_cast<GLfloat>(material.alpha);
    specular[0] = static_cast<GLfloat>(material.specular[0]);
    specular[1] = static_cast<GLfloat>(material.specular[1]);
    specular[2] = static_cast<GLfloat>(material.specular[2]);
    shininess = static_cast<GLfloat>(material.shininess);

    if (!material.diffuseTexture.empty()) {
      texture = loadTextureOrFallback(model.baseDirectory() / material.diffuseTexture, textureStore);
      useTexture = true;
    }
  }

  diffuse[3] *= materialAlphaMultiplier;

  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
  glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shininess);
  glColor4fv(diffuse);

  if (useTexture) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
  } else {
    glDisable(GL_TEXTURE_2D);
  }
}

void drawAxes(float length, float width = 2.0f) {
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glLineWidth(width);
  glBegin(GL_LINES);
  glColor3f(1.0f, 0.25f, 0.18f);
  glVertex3f(0.0f, 0.0f, 0.0f);
  glVertex3f(length, 0.0f, 0.0f);
  glColor3f(0.55f, 0.9f, 0.2f);
  glVertex3f(0.0f, 0.0f, 0.0f);
  glVertex3f(0.0f, length, 0.0f);
  glColor3f(0.2f, 0.62f, 1.0f);
  glVertex3f(0.0f, 0.0f, 0.0f);
  glVertex3f(0.0f, 0.0f, length);
  glEnd();
  glEnable(GL_LIGHTING);
}

void drawRect(float x, float y, float width, float height, float r, float g, float b, float a) {
  glColor4f(r, g, b, a);
  glBegin(GL_QUADS);
  glVertex2f(x, y);
  glVertex2f(x + width, y);
  glVertex2f(x + width, y + height);
  glVertex2f(x, y + height);
  glEnd();
}

void drawText(float x, float y, const std::string& text, float r, float g, float b) {
  if (fontBase == 0 || text.empty()) {
    return;
  }

  glColor3f(r, g, b);
  glRasterPos2f(x, y);
  glPushAttrib(GL_LIST_BIT);
  glListBase(fontBase - 32);
  glCallLists(static_cast<GLsizei>(text.size()), GL_UNSIGNED_BYTE, text.c_str());
  glPopAttrib();
}

void drawButton(float x, float y, float width, float height, const std::string& label, bool active = false) {
  if (active) {
    drawRect(x, y, width, height, 0.88f, 0.57f, 0.27f, 1.0f);
    drawText(x + 18.0f, y + 25.0f, label, 0.07f, 0.07f, 0.06f);
  } else {
    drawRect(x, y, width, height, 0.13f, 0.15f, 0.16f, 1.0f);
    drawText(x + 18.0f, y + 25.0f, label, 0.94f, 0.94f, 0.9f);
  }
}

float enhancedChannel(float value) {
  return clampf(value * 1.25f + 0.08f, 0.0f, 1.0f);
}

std::array<float, 3> detailColor(const object_view::Model& model, int materialIndex) {
  if (materialIndex < 0 || materialIndex >= static_cast<int>(model.materials().size())) {
    return {0.95f, 0.62f, 0.26f};
  }

  const auto& material = model.materials()[static_cast<std::size_t>(materialIndex)];
  const float red = static_cast<float>(material.diffuse[0]);
  const float green = static_cast<float>(material.diffuse[1]);
  const float blue = static_cast<float>(material.diffuse[2]);
  const float average = (red + green + blue) / 3.0f;

  if (average > 0.70f) {
    return {0.95f, 0.68f, 0.28f};
  }

  return {enhancedChannel(red), enhancedChannel(green), enhancedChannel(blue)};
}

void drawPanel(const std::string& title, const std::string& format, std::size_t triangleCount, int windowHeight) {
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  drawRect(0.0f, 0.0f, static_cast<float>(kPanelWidth), static_cast<float>(windowHeight), 0.07f, 0.08f, 0.09f, 0.96f);

  drawRect(12.0f, 12.0f, 316.0f, 138.0f, 0.10f, 0.12f, 0.13f, 1.0f);
  drawText(30.0f, 38.0f, "OBJECTVIEW", 0.88f, 0.57f, 0.27f);
  drawText(30.0f, 72.0f, "View a 3D model.", 0.94f, 0.94f, 0.90f);
  drawText(30.0f, 104.0f, "Native C++ / OpenGL.", 0.62f, 0.66f, 0.67f);
  drawText(30.0f, 128.0f, "Local renderer path.", 0.62f, 0.66f, 0.67f);

  drawRect(12.0f, 158.0f, 316.0f, 136.0f, 0.10f, 0.12f, 0.13f, 1.0f);
  drawText(30.0f, 184.0f, "Samples", 0.94f, 0.94f, 0.90f);
  drawRect(24.0f, 198.0f, 292.0f, 40.0f, 0.06f, 0.07f, 0.075f, 1.0f);
  drawText(38.0f, 224.0f, title, 0.94f, 0.94f, 0.90f);
  drawButton(24.0f, 248.0f, 292.0f, 38.0f, "Load sample", true);

  drawRect(12.0f, 306.0f, 316.0f, 118.0f, 0.10f, 0.12f, 0.13f, 1.0f);
  drawText(30.0f, 332.0f, "Open files", 0.94f, 0.94f, 0.90f);
  drawRect(24.0f, 346.0f, 292.0f, 38.0f, 0.06f, 0.07f, 0.075f, 1.0f);
  drawText(38.0f, 371.0f, "Launch with an OBJ path", 0.68f, 0.70f, 0.70f);
  drawText(30.0f, 406.0f, "CLI: object-view-native model.obj", 0.62f, 0.66f, 0.67f);

  drawRect(12.0f, 450.0f, 316.0f, 156.0f, 0.10f, 0.12f, 0.13f, 1.0f);
  drawButton(24.0f, 462.0f, 142.0f, 38.0f, "Reset");
  drawButton(174.0f, 462.0f, 142.0f, 38.0f, modelState.autoSpin ? "Stop spin" : "Spin", modelState.autoSpin);
  drawButton(24.0f, 510.0f, 142.0f, 38.0f, "Wireframe", modelState.wireframe);
  drawButton(174.0f, 510.0f, 142.0f, 38.0f, modelState.modelVisible ? "Clear" : "Show", !modelState.modelVisible);
  drawButton(24.0f, 558.0f, 142.0f, 38.0f, "Detail", modelState.detailOverlay);
  drawButton(174.0f, 558.0f, 142.0f, 38.0f, "Screenshot");

  drawRect(12.0f, 618.0f, 316.0f, 106.0f, 0.10f, 0.12f, 0.13f, 1.0f);
  drawText(30.0f, 644.0f, panelStatus(title), 0.62f, 0.66f, 0.67f);
  drawText(30.0f, 674.0f, "Format", 0.62f, 0.66f, 0.67f);
  drawText(174.0f, 674.0f, "Triangles", 0.62f, 0.66f, 0.67f);
  drawText(30.0f, 700.0f, format, 0.94f, 0.94f, 0.90f);
  drawText(174.0f, 700.0f, std::to_string(triangleCount), 0.94f, 0.94f, 0.90f);

  if (timelineVisible) {
    drawRect(12.0f, 730.0f, 316.0f, 96.0f, 0.10f, 0.12f, 0.13f, 1.0f);
    drawButton(24.0f, 742.0f, 84.0f, 36.0f, playback.playing ? "Pause" : "Play", playback.playing);

    drawRect(120.0f, 754.0f, 196.0f, 10.0f, 0.06f, 0.07f, 0.075f, 1.0f);
    const float progress = timelineDuration > 0.0
                                ? clampf(static_cast<float>(playback.time / timelineDuration), 0.0f, 1.0f)
                                : 0.0f;
    if (progress > 0.0f) {
      drawRect(120.0f, 754.0f, 196.0f * progress, 10.0f, 0.88f, 0.57f, 0.27f, 1.0f);
    }

    std::ostringstream timeLabel;
    timeLabel << std::fixed << std::setprecision(1) << playback.time << "s / " << timelineDuration << "s";
    drawText(24.0f, 800.0f, timeLabel.str(), 0.62f, 0.66f, 0.67f);
  }

  if (legendVisible) {
    const float legendY = 730.0f + (timelineVisible ? 108.0f : 0.0f);
    const float legendHeight = 40.0f + static_cast<float>(legendRowCount) * 22.0f;
    drawRect(12.0f, legendY, 316.0f, legendHeight, 0.10f, 0.12f, 0.13f, 1.0f);
    drawText(30.0f, legendY + 26.0f, "Legend", 0.94f, 0.94f, 0.90f);

    float rowY = legendY + 50.0f;
    auto legendRow = [&](float r, float g, float b, const char* label) {
      drawRect(30.0f, rowY - 12.0f, 14.0f, 14.0f, r, g, b, 1.0f);
      drawText(54.0f, rowY, label, 0.82f, 0.84f, 0.84f);
      rowY += 22.0f;
    };

    if (legendHasObjects) {
      legendRow(1.0f, 0.25f, 0.18f, "Local axes: spins with object");
      legendRow(0.2f, 0.62f, 1.0f, "Global axes: fixed to world");
    }
    if (legendHasPaths) legendRow(0.44f, 0.91f, 0.77f, "Path: a route or trajectory");
    if (legendHasVectors) legendRow(0.75f, 0.35f, 0.85f, "Vector: a direction");
    if (legendHasGhosts) legendRow(0.7f, 0.7f, 0.7f, "Ghost: alt. pose (faded)");
    if (legendHasMarkers) legendRow(0.98f, 0.85f, 0.35f, "Marker: point of interest");
    if (legendHasFrames) legendRow(0.9f, 0.5f, 0.2f, "Frame: authored coord. axes");
    if (legendHasHeatmaps) legendRow(0.75f, 0.25f, 0.55f, "Heatmap: value on the ground");
  }

  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
}

bool inside(double x, double y, double left, double top, double width, double height) {
  return x >= left && x <= left + width && y >= top && y <= top + height;
}

bool handlePanelClick(double x, double y) {
  if (x > kPanelWidth) {
    return false;
  }

  if (inside(x, y, 24.0, 248.0, 292.0, 38.0)) {
    modelState = ModelState{};
    return true;
  }

  if (inside(x, y, 24.0, 462.0, 142.0, 38.0)) {
    modelState = ModelState{};
    return true;
  }

  if (inside(x, y, 174.0, 462.0, 142.0, 38.0)) {
    modelState.autoSpin = !modelState.autoSpin;
    return true;
  }

  if (inside(x, y, 24.0, 510.0, 142.0, 38.0)) {
    modelState.wireframe = !modelState.wireframe;
    return true;
  }

  if (inside(x, y, 174.0, 510.0, 142.0, 38.0)) {
    modelState.modelVisible = !modelState.modelVisible;
    return true;
  }

  if (inside(x, y, 24.0, 558.0, 142.0, 38.0)) {
    modelState.detailOverlay = !modelState.detailOverlay;
    return true;
  }

  if (inside(x, y, 174.0, 558.0, 142.0, 38.0)) {
    screenshotRequested = true;
    return true;
  }

  if (timelineVisible) {
    if (inside(x, y, 24.0, 742.0, 84.0, 36.0)) {
      playback.playing = !playback.playing;
      return true;
    }

    if (inside(x, y, 120.0, 746.0, 196.0, 26.0)) {
      const double ratio = std::clamp((x - 120.0) / 196.0, 0.0, 1.0);
      playback.time = static_cast<float>(ratio * timelineDuration);
      playback.playing = false;
      return true;
    }
  }

  return true;
}

void drawGrid(float extent, int divisions) {
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glColor3f(0.18f, 0.22f, 0.24f);
  glBegin(GL_LINES);
  for (int index = -divisions; index <= divisions; ++index) {
    const float value = extent * static_cast<float>(index) / static_cast<float>(divisions);
    glVertex3f(value, -1.6f, -extent);
    glVertex3f(value, -1.6f, extent);
    glVertex3f(-extent, -1.6f, value);
    glVertex3f(extent, -1.6f, value);
  }
  glEnd();
  glEnable(GL_LIGHTING);
}

float modelScale(const object_view::Model& model) {
  const auto& bounds = model.stats().bounds;
  const float radius = static_cast<float>(std::max(bounds.radius, 0.001));
  return 1.58f / radius;
}

float modelRadius(const object_view::Model& model) {
  return static_cast<float>(std::max(model.stats().bounds.radius, 0.001));
}

void drawModelGeometry(const object_view::Model& model, TextureStore& textureStore) {
  const auto& bounds = model.stats().bounds;

  glPushMatrix();
  glTranslated(-bounds.center.x, -bounds.center.y, -bounds.center.z);

  int activeMaterial = -999;
  for (const auto& face : model.faces()) {
    if (face.materialIndex != activeMaterial) {
      applyMaterial(model, face.materialIndex, textureStore);
      activeMaterial = face.materialIndex;
    }

    glBegin(GL_TRIANGLE_FAN);
    for (const auto& faceVertex : face.vertices) {
      if (faceVertex.texCoordIndex >= 0 &&
          faceVertex.texCoordIndex < static_cast<int>(model.texCoords().size())) {
        const auto& texCoord = model.texCoords()[static_cast<std::size_t>(faceVertex.texCoordIndex)];
        glTexCoord2d(texCoord.u, texCoord.v);
      }

      if (faceVertex.normalIndex >= 0 &&
          faceVertex.normalIndex < static_cast<int>(model.normals().size())) {
        const auto& normal = model.normals()[static_cast<std::size_t>(faceVertex.normalIndex)];
        glNormal3d(normal.x, normal.y, normal.z);
      }

      const auto& vertex = model.vertices()[static_cast<std::size_t>(faceVertex.vertexIndex)];
      glVertex3d(vertex.x, vertex.y, vertex.z);
    }
    glEnd();
  }

  glPopMatrix();
  glDisable(GL_TEXTURE_2D);
}

void drawModelDetailOverlay(const object_view::Model& model) {
  if (!modelState.detailOverlay || modelState.wireframe) {
    return;
  }

  const auto& bounds = model.stats().bounds;
  glPushMatrix();
  glTranslated(-bounds.center.x, -bounds.center.y, -bounds.center.z);

  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(1.0f);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  int activeMaterial = -999;
  for (const auto& face : model.faces()) {
    if (face.materialIndex != activeMaterial) {
      const auto color = detailColor(model, face.materialIndex);
      glColor4f(color[0], color[1], color[2], 0.34f);
      activeMaterial = face.materialIndex;
    }

    glBegin(GL_TRIANGLE_FAN);
    for (const auto& faceVertex : face.vertices) {
      const auto& vertex = model.vertices()[static_cast<std::size_t>(faceVertex.vertexIndex)];
      glVertex3d(vertex.x, vertex.y, vertex.z);
    }
    glEnd();
  }

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_BLEND);
  glEnable(GL_LIGHTING);
  glPopMatrix();
}

void applyModelTransform(const object_view::Model& model) {
  const float scale = modelScale(model) * modelState.zoom;
  glTranslatef(modelState.positionX, modelState.positionY, modelState.positionZ);
  glRotatef(modelState.rotationX, 1.0f, 0.0f, 0.0f);
  glRotatef(modelState.rotationY, 0.0f, 1.0f, 0.0f);
  glScalef(scale, scale, scale);
}

void applyModelCenterTransform(const object_view::Model& model) {
  const float scale = modelScale(model) * modelState.zoom;
  glTranslatef(modelState.positionX, modelState.positionY, modelState.positionZ);
  glScalef(scale, scale, scale);
}

void drawModelRoot(const object_view::Model& model, TextureStore& textureStore) {
  glPushMatrix();
  applyModelTransform(model);
  drawModelGeometry(model, textureStore);
  drawModelDetailOverlay(model);
  drawAxes(modelRadius(model) * 0.70f, 2.0f);
  glPopMatrix();

  glPushMatrix();
  applyModelCenterTransform(model);
  drawAxes(modelRadius(model) * 1.15f, 3.0f);
  glPopMatrix();
}

float sceneScale(double radius) {
  return 1.58f / static_cast<float>(std::max(radius, 0.001));
}

void applySceneRootTransform(double radius, const object_view::Vector3& pivot) {
  const float scale = sceneScale(radius) * modelState.zoom;
  glTranslatef(modelState.positionX, modelState.positionY, modelState.positionZ);
  glTranslated(scale * pivot.x, scale * pivot.y, scale * pivot.z);
  glRotatef(modelState.rotationX, 1.0f, 0.0f, 0.0f);
  glRotatef(modelState.rotationY, 0.0f, 1.0f, 0.0f);
  glScalef(scale, scale, scale);
  glTranslated(-pivot.x, -pivot.y, -pivot.z);
}

void applySceneTransform(const object_view::Transform& transform) {
  glTranslated(transform.position.x, transform.position.y, transform.position.z);
  glRotated(transform.rotation.x, 1.0, 0.0, 0.0);
  glRotated(transform.rotation.y, 0.0, 1.0, 0.0);
  glRotated(transform.rotation.z, 0.0, 0.0, 1.0);
  glScaled(transform.scale, transform.scale, transform.scale);
}

object_view::Vector3 lerpVector(const object_view::Vector3& a, const object_view::Vector3& b, double t) {
  return object_view::Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

object_view::Transform interpolatedTransform(const object_view::SceneObject& object, double time) {
  if (object.keyframes.empty()) {
    return object.transform;
  }
  if (object.keyframes.size() == 1 || time <= object.keyframes.front().time) {
    return object.keyframes.front().transform;
  }
  if (time >= object.keyframes.back().time) {
    return object.keyframes.back().transform;
  }

  for (std::size_t index = 0; index + 1 < object.keyframes.size(); ++index) {
    const auto& start = object.keyframes[index];
    const auto& end = object.keyframes[index + 1];
    if (time < start.time || time > end.time) {
      continue;
    }

    const double span = end.time - start.time;
    const double t = span > 1e-9 ? (time - start.time) / span : 0.0;
    object_view::Transform result;
    result.position = lerpVector(start.transform.position, end.transform.position, t);
    result.rotation = lerpVector(start.transform.rotation, end.transform.rotation, t);
    result.scale = start.transform.scale + (end.transform.scale - start.transform.scale) * t;
    return result;
  }

  return object.transform;
}

double sceneDuration(const object_view::Scene& scene) {
  double duration = 0.0;
  for (const auto& object : scene.objects) {
    for (const auto& keyframe : object.keyframes) {
      duration = std::max(duration, keyframe.time);
    }
  }
  return duration;
}

void setColor(const object_view::Color& color) {
  glColor4d(
      clampf(static_cast<float>(color.r), 0.0f, 1.0f),
      clampf(static_cast<float>(color.g), 0.0f, 1.0f),
      clampf(static_cast<float>(color.b), 0.0f, 1.0f),
      clampf(static_cast<float>(color.a), 0.0f, 1.0f));
}

void drawScenePath(const object_view::ScenePath& path) {
  if (path.points.size() < 2) {
    return;
  }

  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glLineWidth(static_cast<GLfloat>(std::max(path.width, 1.0)));
  setColor(path.color);
  glBegin(GL_LINE_STRIP);
  for (const auto& point : path.points) {
    glVertex3d(point.x, point.y, point.z);
  }
  glEnd();
  glEnable(GL_LIGHTING);
}

void drawSceneVector(const object_view::SceneVector& vector) {
  const object_view::Vector3 end = {
      vector.origin.x + vector.direction.x * vector.scale,
      vector.origin.y + vector.direction.y * vector.scale,
      vector.origin.z + vector.direction.z * vector.scale};

  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glLineWidth(2.5f);
  setColor(vector.color);
  glBegin(GL_LINES);
  glVertex3d(vector.origin.x, vector.origin.y, vector.origin.z);
  glVertex3d(end.x, end.y, end.z);
  glEnd();

  const double arrow = 0.08 * std::max(vector.scale, 0.4);
  glBegin(GL_LINES);
  glVertex3d(end.x, end.y, end.z);
  glVertex3d(end.x - arrow, end.y, end.z - arrow);
  glVertex3d(end.x, end.y, end.z);
  glVertex3d(end.x + arrow, end.y, end.z - arrow);
  glEnd();
  glEnable(GL_LIGHTING);
}

void drawSceneMarker(const object_view::SceneMarker& marker) {
  const double size = std::max(marker.size, 0.01);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glLineWidth(2.0f);
  setColor(marker.color);
  glBegin(GL_LINES);
  glVertex3d(marker.position.x - size, marker.position.y, marker.position.z);
  glVertex3d(marker.position.x + size, marker.position.y, marker.position.z);
  glVertex3d(marker.position.x, marker.position.y - size, marker.position.z);
  glVertex3d(marker.position.x, marker.position.y + size, marker.position.z);
  glVertex3d(marker.position.x, marker.position.y, marker.position.z - size);
  glVertex3d(marker.position.x, marker.position.y, marker.position.z + size);
  glEnd();
  glEnable(GL_LIGHTING);
}

void drawSceneFrame(const object_view::SceneFrame& frame) {
  glPushMatrix();
  applySceneTransform(frame.transform);
  drawAxes(static_cast<float>(std::max(frame.size, 0.05)), 2.0f);
  glPopMatrix();
}

object_view::Color heatmapCellColor(const object_view::SceneHeatmap& heatmap, double normalized) {
  const double t = std::clamp(normalized, 0.0, 1.0);
  object_view::Color color;
  color.r = heatmap.colorLow.r + (heatmap.colorHigh.r - heatmap.colorLow.r) * t;
  color.g = heatmap.colorLow.g + (heatmap.colorHigh.g - heatmap.colorLow.g) * t;
  color.b = heatmap.colorLow.b + (heatmap.colorHigh.b - heatmap.colorLow.b) * t;
  color.a = heatmap.colorLow.a + (heatmap.colorHigh.a - heatmap.colorLow.a) * t;
  return color;
}

void drawSceneHeatmap(const object_view::SceneHeatmap& heatmap) {
  if (heatmap.rows <= 0 || heatmap.cols <= 0) {
    return;
  }

  const std::size_t rows = static_cast<std::size_t>(heatmap.rows);
  const std::size_t cols = static_cast<std::size_t>(heatmap.cols);
  if (heatmap.values.size() < rows * cols) {
    return;
  }

  double minValue = heatmap.values[0];
  double maxValue = heatmap.values[0];
  for (double value : heatmap.values) {
    minValue = std::min(minValue, value);
    maxValue = std::max(maxValue, value);
  }
  const double range = maxValue - minValue;

  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      const double value = heatmap.values[row * cols + col];
      const double normalized = range > 1e-9 ? (value - minValue) / range : 0.5;
      setColor(heatmapCellColor(heatmap, normalized));

      const double x0 = heatmap.origin.x + static_cast<double>(col) * heatmap.cellSize;
      const double x1 = x0 + heatmap.cellSize;
      const double z0 = heatmap.origin.z + static_cast<double>(row) * heatmap.cellSize;
      const double z1 = z0 + heatmap.cellSize;
      const double y = heatmap.origin.y;

      glBegin(GL_QUADS);
      glVertex3d(x0, y, z0);
      glVertex3d(x1, y, z0);
      glVertex3d(x1, y, z1);
      glVertex3d(x0, y, z1);
      glEnd();
    }
  }

  glDisable(GL_BLEND);
  glEnable(GL_LIGHTING);
}

void drawObjectGhost(
    const object_view::Model& model,
    TextureStore& textureStore,
    const object_view::Ghost& ghost,
    double radius,
    const object_view::Vector3& pivot) {
  glPushMatrix();
  applySceneRootTransform(radius, pivot);
  applySceneTransform(ghost.transform);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  materialAlphaMultiplier = clampf(static_cast<float>(ghost.opacity), 0.0f, 1.0f);
  drawModelGeometry(model, textureStore);
  materialAlphaMultiplier = 1.0f;
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  glPopMatrix();
}

void drawLoadedScene(const LoadedScene& loadedScene, TextureStore& textureStore) {
  for (const auto& loadedObject : loadedScene.objects) {
    const object_view::Transform transform = interpolatedTransform(loadedObject.object, playback.time);

    for (const auto& ghost : loadedObject.object.ghosts) {
      drawObjectGhost(loadedObject.model, textureStore, ghost, loadedScene.radius, loadedScene.pivot);
    }

    glPushMatrix();
    applySceneRootTransform(loadedScene.radius, loadedScene.pivot);
    applySceneTransform(transform);
    drawModelGeometry(loadedObject.model, textureStore);
    drawModelDetailOverlay(loadedObject.model);
    drawAxes(modelRadius(loadedObject.model) * 0.70f, 2.0f);
    glPopMatrix();

    glPushMatrix();
    applySceneRootTransform(loadedScene.radius, loadedScene.pivot);
    glTranslated(transform.position.x, transform.position.y, transform.position.z);
    glScaled(transform.scale, transform.scale, transform.scale);
    glRotatef(-modelState.rotationY, 0.0f, 1.0f, 0.0f);
    glRotatef(-modelState.rotationX, 1.0f, 0.0f, 0.0f);
    drawAxes(modelRadius(loadedObject.model) * 1.15f, 3.0f);
    glPopMatrix();
  }

  glPushMatrix();
  applySceneRootTransform(loadedScene.radius, loadedScene.pivot);
  for (const auto& heatmap : loadedScene.scene.heatmaps) drawSceneHeatmap(heatmap);
  for (const auto& path : loadedScene.scene.paths) drawScenePath(path);
  for (const auto& vector : loadedScene.scene.vectors) drawSceneVector(vector);
  for (const auto& frame : loadedScene.scene.frames) drawSceneFrame(frame);
  for (const auto& marker : loadedScene.scene.markers) drawSceneMarker(marker);
  glPopMatrix();
}

double pointRadius(const object_view::Vector3& point) {
  return std::sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
}

double calculateSceneRadius(const LoadedScene& loadedScene) {
  double radius = 1.0;
  for (const auto& loadedObject : loadedScene.objects) {
    const double objectRadius = loadedObject.model.stats().bounds.radius * loadedObject.object.transform.scale;
    radius = std::max(radius, pointRadius(loadedObject.object.transform.position) + objectRadius);
    for (const auto& keyframe : loadedObject.object.keyframes) {
      radius = std::max(radius, pointRadius(keyframe.transform.position) + objectRadius);
    }
    for (const auto& ghost : loadedObject.object.ghosts) {
      radius = std::max(radius, pointRadius(ghost.transform.position) + objectRadius);
    }
  }
  for (const auto& path : loadedScene.scene.paths) {
    for (const auto& point : path.points) radius = std::max(radius, pointRadius(point));
  }
  for (const auto& vector : loadedScene.scene.vectors) {
    radius = std::max(radius, pointRadius(vector.origin));
    const object_view::Vector3 end = {
        vector.origin.x + vector.direction.x * vector.scale,
        vector.origin.y + vector.direction.y * vector.scale,
        vector.origin.z + vector.direction.z * vector.scale};
    radius = std::max(radius, pointRadius(end));
  }
  for (const auto& frame : loadedScene.scene.frames) {
    radius = std::max(radius, pointRadius(frame.transform.position) + frame.size);
  }
  for (const auto& marker : loadedScene.scene.markers) {
    radius = std::max(radius, pointRadius(marker.position) + marker.size);
  }
  for (const auto& heatmap : loadedScene.scene.heatmaps) {
    const object_view::Vector3 farCorner = {
        heatmap.origin.x + static_cast<double>(heatmap.cols) * heatmap.cellSize,
        heatmap.origin.y,
        heatmap.origin.z + static_cast<double>(heatmap.rows) * heatmap.cellSize};
    radius = std::max(radius, pointRadius(heatmap.origin));
    radius = std::max(radius, pointRadius(farCorner));
  }
  return radius;
}

object_view::Vector3 calculateScenePivot(const LoadedScene& loadedScene) {
  if (loadedScene.objects.empty()) {
    return object_view::Vector3(0.0, 0.0, 0.0);
  }

  object_view::Vector3 sum(0.0, 0.0, 0.0);
  for (const auto& loadedObject : loadedScene.objects) {
    sum += loadedObject.object.transform.position;
  }
  return sum / static_cast<double>(loadedScene.objects.size());
}

void setupLighting() {
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glDisable(GL_COLOR_MATERIAL);
  glShadeModel(GL_SMOOTH);
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glClearColor(0.06f, 0.075f, 0.08f, 1.0f);

  GLfloat lightPosition[] = {4.5f, 6.0f, 5.0f, 1.0f};
  GLfloat lightDiffuse[] = {0.82f, 0.78f, 0.72f, 1.0f};
  GLfloat lightAmbient[] = {0.12f, 0.12f, 0.12f, 1.0f};
  glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
  glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
  if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT) {
    return;
  }

  if (action == GLFW_PRESS) {
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    if (button == GLFW_MOUSE_BUTTON_LEFT && handlePanelClick(cursorX, cursorY)) {
      camera.dragging = false;
      return;
    }

    camera.dragging = true;
    camera.panning = button == GLFW_MOUSE_BUTTON_RIGHT ||
                     glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                     glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    camera.lastX = cursorX;
    camera.lastY = cursorY;
  } else if (action == GLFW_RELEASE) {
    camera.dragging = false;
  }
}

void cursorCallback(GLFWwindow*, double x, double y) {
  if (!camera.dragging) {
    camera.lastX = x;
    camera.lastY = y;
    return;
  }

  const double dx = x - camera.lastX;
  const double dy = y - camera.lastY;
  camera.lastX = x;
  camera.lastY = y;

  if (camera.panning) {
    modelState.positionX += static_cast<float>(dx * 0.008);
    modelState.positionY -= static_cast<float>(dy * 0.008);
  } else {
    modelState.rotationY += static_cast<float>(dx * 0.35);
    modelState.rotationX += static_cast<float>(dy * 0.35);
  }
}

void scrollCallback(GLFWwindow*, double, double yOffset) {
  modelState.zoom = clampf(modelState.zoom * (yOffset > 0.0 ? 1.1f : 0.9f), 0.18f, 7.5f);
}

void handleKeyboard(GLFWwindow* window, float deltaSeconds) {
  float dx = 0.0f;
  float dy = 0.0f;
  float dz = 0.0f;

  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) dx -= 1.0f;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) dx += 1.0f;
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) dy -= 1.0f;
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) dy += 1.0f;
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) dz -= 1.0f;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) dz += 1.0f;

  const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (length > 0.0f) {
    const float speed = 1.85f;
    const float step = speed * deltaSeconds / std::max(modelState.zoom, 0.35f);
    modelState.positionX += (dx / length) * step;
    modelState.positionY += (dy / length) * step;
    modelState.positionZ += (dz / length) * step;
  }

  if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
    modelState = ModelState{};
  }

  if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) modelState.autoSpin = true;
  if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) modelState.wireframe = true;

  const bool screenshotKeyDown = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
  if (screenshotKeyDown && !screenshotKeyWasDown) {
    screenshotRequested = true;
  }
  screenshotKeyWasDown = screenshotKeyDown;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  GdiPlusSession gdiPlus;
  if (!gdiPlus.active()) {
    std::cerr << "Warning: GDI+ image loading is unavailable. Textures may fall back to checkerboard.\n";
  }
#endif

  fs::path modelPath = "native/samples/perseverance-rover/perseverance_rover.obj";
  bool captureAndExit = false;
  float initialPlaybackTime = 0.0f;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--capture") {
      captureAndExit = true;
    } else if (argument.rfind("--time=", 0) == 0) {
      initialPlaybackTime = std::strtof(argument.c_str() + 7, nullptr);
    } else {
      modelPath = argument;
    }
  }

  object_view::Model model;
  LoadedScene loadedScene;
  std::string error;
  const bool sceneMode = modelPath.extension() == ".json";
  std::string panelTitle = "Perseverance Rover";
  std::string panelFormat = "OBJ";
  std::size_t panelTriangles = 0;

  if (sceneMode) {
    if (!object_view::loadScene(modelPath, loadedScene.scene, error)) {
      std::cerr << error << '\n';
      return 1;
    }

    for (const auto& sceneObject : loadedScene.scene.objects) {
      LoadedSceneObject loadedObject;
      loadedObject.object = sceneObject;
      if (!loadedObject.model.loadObject(sceneObject.asset, error)) {
        std::cerr << error << '\n';
        return 1;
      }
      loadedScene.triangleCount += loadedObject.model.stats().triangleCount;
      loadedScene.objects.push_back(std::move(loadedObject));
    }

    loadedScene.radius = calculateSceneRadius(loadedScene);
    loadedScene.pivot = calculateScenePivot(loadedScene);
    loadedScene.duration = sceneDuration(loadedScene.scene);
    timelineDuration = loadedScene.duration;
    timelineVisible = timelineDuration > 0.0;
    panelTitle = loadedScene.scene.name;
    panelFormat = "SCENE";
    panelTriangles = loadedScene.triangleCount;

    legendHasObjects = !loadedScene.objects.empty();
    legendHasPaths = !loadedScene.scene.paths.empty();
    legendHasVectors = !loadedScene.scene.vectors.empty();
    legendHasFrames = !loadedScene.scene.frames.empty();
    legendHasMarkers = !loadedScene.scene.markers.empty();
    legendHasHeatmaps = !loadedScene.scene.heatmaps.empty();
    legendHasGhosts = std::any_of(loadedScene.objects.begin(), loadedScene.objects.end(), [](const LoadedSceneObject& loadedObject) {
      return !loadedObject.object.ghosts.empty();
    });
    legendRowCount = (legendHasObjects ? 2 : 0) + (legendHasPaths ? 1 : 0) + (legendHasVectors ? 1 : 0) +
                     (legendHasGhosts ? 1 : 0) + (legendHasMarkers ? 1 : 0) + (legendHasFrames ? 1 : 0) +
                     (legendHasHeatmaps ? 1 : 0);
    legendVisible = legendRowCount > 0;
    playback.time = clampf(initialPlaybackTime, 0.0f, static_cast<float>(std::max(timelineDuration, 0.0)));
  } else {
    if (!model.loadObject(modelPath, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    panelTitle = model.stats().sourceName;
    panelTriangles = model.stats().triangleCount;
  }

  if (!glfwInit()) {
    std::cerr << "Could not initialize GLFW.\n";
    return 1;
  }

  int windowHeight = 820;
  if (timelineVisible) windowHeight += 108;
  if (legendVisible) windowHeight += 52 + legendRowCount * 22;
  GLFWwindow* window = glfwCreateWindow(1280, windowHeight, "ObjectView Native OpenGL", nullptr, nullptr);
  if (!window) {
    std::cerr << "Could not create OpenGL window.\n";
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  initializeFont();
  glfwSetMouseButtonCallback(window, mouseButtonCallback);
  glfwSetCursorPosCallback(window, cursorCallback);
  glfwSetScrollCallback(window, scrollCallback);

  setupLighting();

  TextureStore textureStore;
  textureStore.checkerTexture = createCheckerTexture();

  std::cout << "Loaded " << modelPath << '\n';
  std::cout << "Controls: drag rotate model, right/shift-drag move model, scroll zoom, WASD/QE move, hold Space spin, hold F wireframe, R reset.\n";
  if (timelineVisible) {
    std::cout << "Timeline: Play/Pause button toggles playback, click the scrub bar to seek.\n";
  }

  double previousTime = glfwGetTime();
  int renderedFrames = 0;
  while (!glfwWindowShouldClose(window)) {
    const double now = glfwGetTime();
    const float deltaSeconds = clampf(static_cast<float>(now - previousTime), 0.0f, 0.05f);
    previousTime = now;

    handleKeyboard(window, deltaSeconds);
    if (modelState.autoSpin) {
      modelState.rotationY += 50.0f * deltaSeconds;
    }

    if (playback.playing && timelineDuration > 0.0) {
      playback.time += deltaSeconds;
      if (playback.time >= static_cast<float>(timelineDuration)) {
        if (playback.loop) {
          playback.time = std::fmod(playback.time, static_cast<float>(timelineDuration));
        } else {
          playback.time = static_cast<float>(timelineDuration);
          playback.playing = false;
        }
      }
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    const int viewportX = std::min(kPanelWidth, width);
    const int viewportWidth = std::max(width - viewportX, 1);
    glViewport(viewportX, 0, viewportWidth, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    setPerspective(45.0f, static_cast<float>(viewportWidth) / static_cast<float>(std::max(height, 1)), 0.1f, 200.0f);

    glLoadIdentity();
    setLookAt(5.2f, 3.6f, 6.2f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    drawGrid(5.0f, 10);
    glPolygonMode(GL_FRONT_AND_BACK, modelState.wireframe ? GL_LINE : GL_FILL);
    if (modelState.modelVisible) {
      if (sceneMode) {
        drawLoadedScene(loadedScene, textureStore);
      } else {
        drawModelRoot(model, textureStore);
      }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glViewport(0, 0, width, height);
    setOrtho2D(width, height);
    drawPanel(panelTitle, panelFormat, panelTriangles, height);

    if (screenshotRequested) {
      lastScreenshotName = saveScreenshotBmp(width, height);
      if (lastScreenshotName.empty()) {
        lastScreenshotName = "screenshot failed";
      }
      screenshotRequested = false;
    }

    if (captureAndExit && renderedFrames++ >= 2) {
      lastScreenshotName = saveScreenshotBmp(width, height);
      if (lastScreenshotName.empty()) {
        std::cerr << "Could not save capture.\n";
        return 1;
      }
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  for (const auto& entry : textureStore.textures) {
    GLuint texture = entry.second;
    if (texture != textureStore.checkerTexture) {
      glDeleteTextures(1, &texture);
    }
  }
  glDeleteTextures(1, &textureStore.checkerTexture);

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
