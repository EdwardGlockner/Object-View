// GLCanvas.cpp

//*********************************************************************************
// Headers
//*********************************************************************************
#include "../include/GLCanvas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace fs = std::filesystem;

namespace {

#ifndef GL_GENERATE_MIPMAP
constexpr GLenum GL_GENERATE_MIPMAP = 0x8191;
#endif

#ifdef _WIN32
using WglSwapIntervalProc = BOOL(WINAPI*)(int);
#endif

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

 private:
  ULONG_PTR token_ = 0;
  bool active_ = false;
};

GdiPlusSession& gdiPlusSession() {
  static GdiPlusSession session;
  return session;
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

float clampf(float value, float minimum, float maximum) {
  return std::max(minimum, std::min(maximum, value));
}

float lerpf(float current, float target, float factor) {
  return current + (target - current) * factor;
}

float wrapAngleDelta(float current, float target) {
  float delta = std::fmod(target - current, 360.0f);
  if (delta > 180.0f) {
    delta -= 360.0f;
  } else if (delta < -180.0f) {
    delta += 360.0f;
  }
  return delta;
}

int normalizeKeyCode(int keyCode) {
  if (keyCode >= 'a' && keyCode <= 'z') {
    return std::toupper(static_cast<unsigned char>(keyCode));
  }
  return keyCode;
}

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

GLuint loadTextureOrFallback(const fs::path& path, GLCanvas::TextureStore& textureStore) {
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

void applyMaterial(const object_view::Model& model, int materialIndex, GLCanvas::TextureStore& textureStore) {
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

  return {
      clampf(red * 1.25f + 0.08f, 0.0f, 1.0f),
      clampf(green * 1.25f + 0.08f, 0.0f, 1.0f),
      clampf(blue * 1.25f + 0.08f, 0.0f, 1.0f)};
}

double modelRadius(const object_view::Model& model) {
  return std::max(model.stats().bounds.radius, 0.001);
}

float modelScale(const object_view::Model& model) {
  return 1.58f / static_cast<float>(modelRadius(model));
}

void drawModelGeometry(const object_view::Model& model, GLCanvas::TextureStore& textureStore) {
  bool batchOpen = false;
  GLenum currentPrimitive = 0;
  int currentMaterialIndex = std::numeric_limits<int>::min();

  for (const auto& face : model.faces()) {
    if (face.vertices.size() < 3) {
      continue;
    }

    const GLenum nextPrimitive = face.vertices.size() == 3 ? GL_TRIANGLES : GL_POLYGON;
    if (!batchOpen || face.materialIndex != currentMaterialIndex || nextPrimitive != currentPrimitive) {
      if (batchOpen) {
        glEnd();
      }
      applyMaterial(model, face.materialIndex, textureStore);
      glBegin(nextPrimitive);
      batchOpen = true;
      currentPrimitive = nextPrimitive;
      currentMaterialIndex = face.materialIndex;
    }

    for (const auto& faceVertex : face.vertices) {
      if (faceVertex.normalIndex >= 0 && faceVertex.normalIndex < static_cast<int>(model.normals().size())) {
        const auto& normal = model.normals()[static_cast<std::size_t>(faceVertex.normalIndex)];
        glNormal3d(normal.x, normal.y, normal.z);
      }
      if (faceVertex.texCoordIndex >= 0 && faceVertex.texCoordIndex < static_cast<int>(model.texCoords().size())) {
        const auto& texCoord = model.texCoords()[static_cast<std::size_t>(faceVertex.texCoordIndex)];
        glTexCoord2d(texCoord.u, texCoord.v);
      }
      const auto& vertex = model.vertices()[static_cast<std::size_t>(faceVertex.vertexIndex)];
      glVertex3d(vertex.x, vertex.y, vertex.z);
    }
  }

  if (batchOpen) {
    glEnd();
  }
}

void drawModelDetailOverlay(const object_view::Model& model) {
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(1.1f);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  for (const auto& face : model.faces()) {
    if (face.vertices.size() < 3) {
      continue;
    }

    const auto color = detailColor(model, face.materialIndex);
    glColor4f(color[0], color[1], color[2], 0.58f);
    glBegin(face.vertices.size() == 3 ? GL_TRIANGLE_FAN : GL_POLYGON);
    for (const auto& faceVertex : face.vertices) {
      const auto& vertex = model.vertices()[static_cast<std::size_t>(faceVertex.vertexIndex)];
      glVertex3d(vertex.x, vertex.y, vertex.z);
    }
    glEnd();
  }

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_BLEND);
  glEnable(GL_LIGHTING);
}

void applyObjectTransform(const object_view::Model& model, const GLCanvas::ModelState& modelState) {
  const float scale = modelScale(model) * modelState.zoom;
  glTranslatef(modelState.positionX, modelState.positionY, modelState.positionZ);
  glRotatef(modelState.rotationX, 1.0f, 0.0f, 0.0f);
  glRotatef(modelState.rotationY, 0.0f, 1.0f, 0.0f);
  glScalef(scale, scale, scale);
}

void applyModelCenterOffset(const object_view::Model& model) {
  const auto& center = model.stats().bounds.center;
  glTranslatef(
      -static_cast<GLfloat>(center.x),
      -static_cast<GLfloat>(center.y),
      -static_cast<GLfloat>(center.z));
}

void drawCoordinateSystem(const object_view::Model& model) {
  const float axisLength = std::max(0.48f, static_cast<float>(modelRadius(model)) * 0.52f);
  const float headLength = axisLength * 0.16f;
  const float headWidth = axisLength * 0.06f;

  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(2.2f);

  auto drawAxis = [&](float red, float green, float blue, float x, float y, float z) {
    glColor4f(red, green, blue, 0.96f);
    glBegin(GL_LINES);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(x, y, z);

    if (x != 0.0f) {
      glVertex3f(x, y, z);
      glVertex3f(x - headLength, headWidth, 0.0f);
      glVertex3f(x, y, z);
      glVertex3f(x - headLength, -headWidth, 0.0f);
    } else if (y != 0.0f) {
      glVertex3f(x, y, z);
      glVertex3f(headWidth, y - headLength, 0.0f);
      glVertex3f(x, y, z);
      glVertex3f(-headWidth, y - headLength, 0.0f);
    } else {
      glVertex3f(x, y, z);
      glVertex3f(headWidth, 0.0f, z - headLength);
      glVertex3f(x, y, z);
      glVertex3f(-headWidth, 0.0f, z - headLength);
    }
    glEnd();
  };

  drawAxis(0.98f, 0.42f, 0.32f, axisLength, 0.0f, 0.0f);
  drawAxis(0.64f, 0.88f, 0.36f, 0.0f, axisLength, 0.0f);
  drawAxis(0.32f, 0.74f, 0.98f, 0.0f, 0.0f, axisLength);

  glLineWidth(1.0f);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_LIGHTING);
}

}  // namespace

//*********************************************************************************
// Public Class Functions
//*********************************************************************************

//
// GLCanvas
// Description:
//      Constructor.
//      Initializes all the variables, starts a timer for refreshing and binds
//      all events used by the OpenGL canvas.
// Parameters:
//      parent      <wxFrame*>: Pointer to the main frame (MyApp).
//      args        <int*>:     Settings for wxWidgets.
//      initialPath <path>:     Optional initial object path.
// Returns:
//      None (void).
//
GLCanvas::GLCanvas(wxFrame* parent, int* args, const fs::path& initialPath)
    : wxGLCanvas(parent, wxID_ANY, args, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE),
      timer(new wxTimer(this, wxID_ANY)),
      m_context(this) {
#ifdef _WIN32
  gdiPlusSession();
  timeBeginPeriod(1);
#endif
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  Bind(wxEVT_IDLE, &GLCanvas::OnIdle, this);
  Bind(wxEVT_TIMER, &GLCanvas::OnTimer, this, wxID_ANY);
  Bind(wxEVT_PAINT, &GLCanvas::OnPaint, this);
  Bind(wxEVT_ERASE_BACKGROUND, &GLCanvas::OnEraseBackground, this);
  Bind(wxEVT_KEY_UP, &GLCanvas::OnKeyUp, this);
  Bind(wxEVT_KEY_DOWN, &GLCanvas::OnKeyDown, this);
  Bind(wxEVT_SIZE, &GLCanvas::OnResize, this);
  Bind(wxEVT_LEFT_DOWN, &GLCanvas::OnLeftDown, this);
  Bind(wxEVT_LEFT_UP, &GLCanvas::OnLeftUp, this);
  Bind(wxEVT_RIGHT_DOWN, &GLCanvas::OnRightDown, this);
  Bind(wxEVT_RIGHT_UP, &GLCanvas::OnRightUp, this);
  Bind(wxEVT_MOTION, &GLCanvas::OnMouseMove, this);
  Bind(wxEVT_MOUSEWHEEL, &GLCanvas::OnMouseWheel, this);

  initialized = false;
  triangle_count = 0;
  choosen_model = 0;
  display_title = "No file loaded";
  display_format = "None";
  bounds_label = "-";
  status_text = "Load an OBJ model.";
  render_model_state = model_state;
  lastFrameTime = std::chrono::steady_clock::now();
  lastMouseTime = lastFrameTime;

  for (bool& key : keys) {
    key = false;
  }

  timer->Start(8);

  initializeDefaultAssets();

  std::string error;
  if (!initialPath.empty()) {
    if (initialPath.extension() != ".obj" && initialPath.extension() != ".OBJ") {
      updateStatusText("ObjectView native loads OBJ files only.");
      loadDefaultAsset();
    } else if (!loadObjectVisual(initialPath, error)) {
      updateStatusText(error);
      loadDefaultAsset();
    } else {
      setChoosenModel(-1);
    }
  } else {
    loadDefaultAsset();
  }
}

//
// ~GLCanvas
// Description:
//      Destructor.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
GLCanvas::~GLCanvas() {
  SetCurrent(m_context);
  if (timer != nullptr) {
    timer->Stop();
  }
  for (const auto& entry : render_lists) {
    if (entry.second.solid != 0) {
      glDeleteLists(entry.second.solid, 1);
    }
    if (entry.second.overlay != 0) {
      glDeleteLists(entry.second.overlay, 1);
    }
  }
  for (const auto& entry : texture_store.textures) {
    GLuint texture = entry.second;
    if (texture != texture_store.checkerTexture) {
      glDeleteTextures(1, &texture);
    }
  }
  if (texture_store.checkerTexture != 0) {
    glDeleteTextures(1, &texture_store.checkerTexture);
  }
#ifdef _WIN32
  timeEndPeriod(1);
#endif
  delete timer;
}

void GLCanvas::initializeDefaultAssets() {
  sample_assets.clear();
  sample_assets.push_back("native/samples/perseverance-rover/perseverance_rover.obj");
  sample_assets.push_back("native/samples/craft-racer/craft_racer.obj");
  sample_assets.push_back("native/samples/toy-car/ToyCar.obj");
  sample_assets.push_back("native/samples/textured-cube/textured_cube.obj");

  for (const auto& sampleAsset : sample_assets) {
    std::string error;
    if (!loadObjectVisual(sampleAsset, error) && status_text == "Load an OBJ model.") {
      updateStatusText(error);
    }
  }
}

void GLCanvas::loadDefaultAsset() {
  if (!models.empty()) {
    setChoosenModel(0);
  }
}

bool GLCanvas::loadObjectVisual(const fs::path& path, std::string& error) {
  auto nextModel = std::make_unique<object_view::Model>();
  if (!nextModel->loadObject(path, error)) {
    return false;
  }

  loaded_path = path;
  models.push_back(nextModel.get());
  owned_models.push_back(std::move(nextModel));
  updateStatusText(path.filename().string() + " added.");
  return true;
}

void GLCanvas::updateLoadedModelInformation(const object_view::Model& model) {
  display_title = model.stats().sourceName;
  display_format = "OBJ";
  triangle_count = model.stats().triangleCount;

  const auto& bounds = model.stats().bounds;
  std::ostringstream boundsStream;
  boundsStream << std::fixed << std::setprecision(2)
               << (bounds.max.x - bounds.min.x) << " x "
               << (bounds.max.y - bounds.min.y) << " x "
               << (bounds.max.z - bounds.min.z);
  bounds_label = boundsStream.str();
  updateStatusText(display_title + " loaded.");
  syncRenderState(true);
  requestFrame();
}

void GLCanvas::ensureModelRenderLists(const object_view::Model& model) {
  RenderLists& lists = render_lists[&model];
  if (lists.solid != 0 && lists.overlay != 0) {
    return;
  }

  if (lists.solid == 0) {
    lists.solid = glGenLists(1);
    glNewList(lists.solid, GL_COMPILE);
    drawModelGeometry(model, texture_store);
    glEndList();
  }

  if (lists.overlay == 0) {
    lists.overlay = glGenLists(1);
    glNewList(lists.overlay, GL_COMPILE);
    drawModelDetailOverlay(model);
    glEndList();
  }
}

void GLCanvas::advanceFrame(double deltaSeconds) {
  if (model_state.autoSpin) {
    model_state.rotationY += autoRotateY_speed * static_cast<GLfloat>(deltaSeconds);
  }

  model_state.rotationX += motion_state.rotationVelocityX * static_cast<GLfloat>(deltaSeconds);
  model_state.rotationY += motion_state.rotationVelocityY * static_cast<GLfloat>(deltaSeconds);
  model_state.positionX += motion_state.panVelocityX * static_cast<GLfloat>(deltaSeconds);
  model_state.positionY += motion_state.panVelocityY * static_cast<GLfloat>(deltaSeconds);
  model_state.zoom += motion_state.zoomVelocity * static_cast<GLfloat>(deltaSeconds);

  const float smoothing = 1.0f - std::exp(-18.0f * static_cast<float>(deltaSeconds));
  render_model_state.rotationX += wrapAngleDelta(render_model_state.rotationX, model_state.rotationX) * smoothing;
  render_model_state.rotationY += wrapAngleDelta(render_model_state.rotationY, model_state.rotationY) * smoothing;
  render_model_state.positionX = lerpf(render_model_state.positionX, model_state.positionX, smoothing);
  render_model_state.positionY = lerpf(render_model_state.positionY, model_state.positionY, smoothing);
  render_model_state.positionZ = lerpf(render_model_state.positionZ, model_state.positionZ, smoothing);
  render_model_state.zoom = lerpf(render_model_state.zoom, model_state.zoom, smoothing);
  render_model_state.autoSpin = model_state.autoSpin;
  render_model_state.wireframe = model_state.wireframe;
  render_model_state.modelVisible = model_state.modelVisible;
  render_model_state.detailOverlay = model_state.detailOverlay;

  const float freeDamping = std::exp(-12.0f * static_cast<float>(deltaSeconds));
  const float dragDamping = std::exp(-18.0f * static_cast<float>(deltaSeconds));
  const float rotationDamping = camera_state.dragging ? dragDamping : freeDamping;
  const float panDamping = camera_state.dragging ? dragDamping : freeDamping;
  const float zoomDamping = dragDamping;

  motion_state.rotationVelocityX *= rotationDamping;
  motion_state.rotationVelocityY *= rotationDamping;
  motion_state.panVelocityX *= panDamping;
  motion_state.panVelocityY *= panDamping;
  motion_state.zoomVelocity *= zoomDamping;

  normalizeRotationAngles();
  model_state.zoom = clampf(model_state.zoom, 0.18f, 7.5f);
}

void GLCanvas::updateStatusText(const std::string& text) {
  status_text = text;
}

//
// establishProjectionMatrix
// Description:
//      Sets up the projection matrix for the OpenGL rendering.
// Parameters:
//      width  <GLsizei>: Width of the viewport.
//      height <GLsizei>: Height of the viewport.
// Returns:
//      None (GLvoid).
//
GLvoid GLCanvas::establishProjectionMatrix(GLsizei width, GLsizei height) {
  glViewport(0, 0, width, height);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45.0f, static_cast<GLfloat>(width) / static_cast<GLfloat>(std::max(height, 1)), 0.1f, 200.0f);
}

//
// initGL
// Description:
//      Initializes OpenGL settings and lighting.
// Parameters:
//      width  <GLsizei>: Width of the viewport.
//      height <GLsizei>: Height of the viewport.
// Returns:
//      None (GLvoid).
//
GLvoid GLCanvas::initGL(GLsizei width, GLsizei height) {
  establishProjectionMatrix(width, height);
  glShadeModel(GL_SMOOTH);
  glClearColor(0.06f, 0.075f, 0.08f, 1.0f);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glEnable(GL_NORMALIZE);
  glDisable(GL_COLOR_MATERIAL);

  GLfloat lightPosition[] = {4.5f, 6.0f, 5.0f, 1.0f};
  GLfloat lightDiffuse[] = {0.82f, 0.78f, 0.72f, 1.0f};
  GLfloat lightAmbient[] = {0.12f, 0.12f, 0.12f, 1.0f};
  glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
  glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);

#ifdef _WIN32
  if (const auto swapInterval = reinterpret_cast<WglSwapIntervalProc>(wglGetProcAddress("wglSwapIntervalEXT"))) {
    swapInterval(1);
  }
#endif

  if (texture_store.checkerTexture == 0) {
    texture_store.checkerTexture = []() {
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
    }();
  }
}

//
// displayFPS
// Description:
//      Tracks frames per second.
//      Kept in the same place as the original viewer structure even though
//      the generic viewer does not render the FPS value.
// Parameters:
//      None (GLvoid).
// Returns:
//      None (GLvoid).
//
GLvoid GLCanvas::displayFPS(GLvoid) {
  static long lastTime = static_cast<long>(wxGetLocalTimeMillis().GetLo());
  static long loops = 0;
  static GLfloat fps = 0.0f;

  const long newTime = static_cast<long>(wxGetLocalTimeMillis().GetLo());
  if (newTime - lastTime > 100) {
    const float newFPS = static_cast<float>(loops) / static_cast<float>(newTime - lastTime) * 1000.0f;
    fps = (fps + newFPS) / 2.0f;
    lastTime = newTime;
    loops = 0;
  }
  loops++;
}

//
// checkNavigation
// Description:
//      Handles user input for navigation and camera control.
// Parameters:
//      deltaSeconds <double>: Frame time delta.
// Returns:
//      <GLboolean>: Indicates whether the navigation should end.
//
GLboolean GLCanvas::checkNavigation(double deltaSeconds) {
  if (keys[WXK_ESCAPE]) {
    return true;
  }

  GLfloat dx = 0.0f;
  GLfloat dy = 0.0f;
  if (keys['A']) dx -= 1.0f;
  if (keys['D']) dx += 1.0f;
  if (keys['W']) dy += 1.0f;
  if (keys['S']) dy -= 1.0f;

  const GLfloat length = std::sqrt(dx * dx + dy * dy);
  if (length > 0.0f) {
    const GLfloat step = translate_speed * static_cast<GLfloat>(deltaSeconds);
    model_state.positionX += (dx / length) * step;
    model_state.positionY += (dy / length) * step;
  }

  if (keys['Q']) {
    model_state.zoom -= zoom_speed * static_cast<GLfloat>(deltaSeconds);
  }
  if (keys['E']) {
    model_state.zoom += zoom_speed * static_cast<GLfloat>(deltaSeconds);
  }

  normalizeRotationAngles();
  model_state.zoom = clampf(model_state.zoom, 0.18f, 7.5f);
  return false;
}

//
// drawScene
// Description:
//      Renders the current 3D object.
// Parameters:
//      None (GLvoid).
// Returns:
//      None (GLvoid).
//
GLvoid GLCanvas::drawScene(GLvoid) {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  setLookAt(5.2f, 3.6f, 6.2f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

  if (!model_state.modelVisible || models.empty()) {
    glFlush();
    SwapBuffers();
    return;
  }

  object_view::Model* model = models[choosen_model];
  ensureModelRenderLists(*model);
  glPolygonMode(GL_FRONT_AND_BACK, render_model_state.wireframe ? GL_LINE : GL_FILL);
  glPushMatrix();
  applyObjectTransform(*model, render_model_state);
  glPushMatrix();
  applyModelCenterOffset(*model);
  glCallList(render_lists[model].solid);
  if (render_model_state.detailOverlay && !camera_state.dragging) {
    glCallList(render_lists[model].overlay);
  }
  glPopMatrix();
  drawCoordinateSystem(*model);
  glPopMatrix();
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glFlush();
  SwapBuffers();
}

//*********************************************************************************
// Event Handlers
//*********************************************************************************

//
// OnIdle
// Description:
//      Handles the idle event and keeps the canvas rendering continuously.
// Parameters:
//      event <wxIdleEvent&>: The idle event object.
// Returns:
//      None (void).
//
void GLCanvas::OnIdle(wxIdleEvent& event) {
  event.Skip();
}

//
// OnTimer
// Description:
//      Handles the timer event and drives the render loop at a steady rate.
// Parameters:
//      event <wxTimerEvent&>: The timer event object.
// Returns:
//      None (void).
//
void GLCanvas::OnTimer(wxTimerEvent&) {
  if (IsShownOnScreen()) {
    requestFrame(true);
  }
}

//
// OnPaint
// Description:
//      Handles the paint event, which occurs when the canvas needs to be
//      redrawn. It is responsible for rendering the OpenGL scene.
// Parameters:
//      event <wxPaintEvent&>: The paint event object.
// Returns:
//      None (void).
//
void GLCanvas::OnPaint(wxPaintEvent&) {
  wxPaintDC dc(this);
  SetCurrent(m_context);

  if (!initialized) {
    initGL(GetSize().GetWidth(), GetSize().GetHeight());
    initialized = true;
  }

  const auto now = std::chrono::steady_clock::now();
  const double deltaSeconds = std::min(std::chrono::duration<double>(now - lastFrameTime).count(), 0.05);
  lastFrameTime = now;

  if (checkNavigation(deltaSeconds)) {
    GetParent()->Close(true);
    return;
  }

  advanceFrame(deltaSeconds);
  drawScene();
  displayFPS();
}

//
// OnEraseBackground
// Description:
//      Handles the erase background event.
//      Prevents default background erasing to reduce flickering.
// Parameters:
//      event <wxEraseEvent&>: The erase event object.
// Returns:
//      None (void).
//
void GLCanvas::OnEraseBackground(wxEraseEvent&) {
}

//
// OnKeyDown
// Description:
//      Handles the key down event, which occurs when a key is pressed down.
//      Allows the canvas to respond to user keyboard input.
// Parameters:
//      event <wxKeyEvent&>: The key event object containing information about
//                           the pressed key.
// Returns:
//      None (void).
//
void GLCanvas::OnKeyDown(wxKeyEvent& event) {
  const int key = normalizeKeyCode(static_cast<int>(event.GetKeyCode()));
  if (key >= 0 && key < 500) {
    keys[key] = true;
  }

  if (key == 'R') {
    resetOrientation();
  }
  if (key == 'F') {
    model_state.wireframe = !model_state.wireframe;
  }
  if (key == WXK_SPACE) {
    model_state.autoSpin = !model_state.autoSpin;
  }

  event.Skip();
}

//
// OnKeyUp
// Description:
//      Handles the key up event, which occurs when a key is released.
//      Allows the canvas to respond to user keyboard input.
// Parameters:
//      event <wxKeyEvent&>: The key event object containing information about
//                           the released key.
// Returns:
//      None (void).
//
void GLCanvas::OnKeyUp(wxKeyEvent& event) {
  const int key = normalizeKeyCode(static_cast<int>(event.GetKeyCode()));
  if (key >= 0 && key < 500) {
    keys[key] = false;
  }
  event.Skip();
}

// OnResize
// Description:
//      Handles the resize event and refreshes the OpenGL projection.
// Parameters:
//      event <wxSizeEvent&>: The resize event object.
// Returns:
//      None (void).
//
void GLCanvas::OnResize(wxSizeEvent& event) {
  if (initialized) {
    SetCurrent(m_context);
    establishProjectionMatrix(GetSize().GetWidth(), GetSize().GetHeight());
    requestFrame();
  }
  event.Skip();
}

//
// OnLeftDown
// Description:
//      Begins mouse dragging with the left button.
// Parameters:
//      event <wxMouseEvent&>: The mouse event object.
// Returns:
//      None (void).
//
void GLCanvas::OnLeftDown(wxMouseEvent& event) {
  SetFocus();
  camera_state.dragging = true;
  camera_state.panning = event.ShiftDown();
  camera_state.lastPosition = event.GetPosition();
  lastMouseTime = std::chrono::steady_clock::now();
  CaptureMouse();
}

//
// OnLeftUp
// Description:
//      Ends left-button dragging.
// Parameters:
//      event <wxMouseEvent&>: The mouse event object.
// Returns:
//      None (void).
//
void GLCanvas::OnLeftUp(wxMouseEvent&) {
  camera_state.dragging = false;
  if (HasCapture()) {
    ReleaseMouse();
  }
}

//
// OnRightDown
// Description:
//      Begins mouse dragging with the right button.
// Parameters:
//      event <wxMouseEvent&>: The mouse event object.
// Returns:
//      None (void).
//
void GLCanvas::OnRightDown(wxMouseEvent& event) {
  SetFocus();
  camera_state.dragging = true;
  camera_state.panning = true;
  camera_state.lastPosition = event.GetPosition();
  lastMouseTime = std::chrono::steady_clock::now();
  CaptureMouse();
}

//
// OnRightUp
// Description:
//      Ends right-button dragging.
// Parameters:
//      event <wxMouseEvent&>: The mouse event object.
// Returns:
//      None (void).
//
void GLCanvas::OnRightUp(wxMouseEvent&) {
  camera_state.dragging = false;
  if (HasCapture()) {
    ReleaseMouse();
  }
}

//
// OnMouseMove
// Description:
//      Handles mouse movement while dragging.
// Parameters:
//      event <wxMouseEvent&>: The mouse event object.
// Returns:
//      None (void).
//
void GLCanvas::OnMouseMove(wxMouseEvent& event) {
  if (!camera_state.dragging) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const double deltaSeconds = std::clamp(
      std::chrono::duration<double>(now - lastMouseTime).count(),
      1.0 / 240.0,
      0.05);
  lastMouseTime = now;

  const wxPoint currentPosition = event.GetPosition();
  const wxPoint delta = currentPosition - camera_state.lastPosition;
  camera_state.lastPosition = currentPosition;

  if (camera_state.panning) {
    motion_state.panVelocityX = clampf(
        static_cast<GLfloat>(delta.x) * pan_drag_speed / static_cast<GLfloat>(deltaSeconds),
        -pan_velocity_limit,
        pan_velocity_limit);
    motion_state.panVelocityY = clampf(
        -static_cast<GLfloat>(delta.y) * pan_drag_speed / static_cast<GLfloat>(deltaSeconds),
        -pan_velocity_limit,
        pan_velocity_limit);
  } else {
    motion_state.rotationVelocityY = clampf(
        static_cast<GLfloat>(delta.x) * rotate_drag_speed / static_cast<GLfloat>(deltaSeconds),
        -rotate_velocity_limit,
        rotate_velocity_limit);
    motion_state.rotationVelocityX = clampf(
        static_cast<GLfloat>(delta.y) * rotate_drag_speed / static_cast<GLfloat>(deltaSeconds),
        -rotate_velocity_limit,
        rotate_velocity_limit);
  }

  requestFrame(false);
}

//
// OnMouseWheel
// Description:
//      Handles mouse-wheel zooming.
// Parameters:
//      event <wxMouseEvent&>: The mouse event object.
// Returns:
//      None (void).
//
void GLCanvas::OnMouseWheel(wxMouseEvent& event) {
  motion_state.zoomVelocity += event.GetWheelRotation() > 0 ? 2.4f : -2.4f;
  requestFrame(false);
}

//*********************************************************************************
// Public Setter Functions
//*********************************************************************************

//
// setAutoRotate
// Description:
//      Toggles between autorotating the object and not autorotating it.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void GLCanvas::setAutoRotate() {
  model_state.autoSpin = !model_state.autoSpin;
  requestFrame();
}

//
// resetAngles
// Description:
//      Resets the angles of the object.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void GLCanvas::resetAngles() {
  model_state.rotationX = ModelState{}.rotationX;
  model_state.rotationY = ModelState{}.rotationY;
  requestFrame();
}

//
// resetOrientation
// Description:
//      Resets both the angles and the displacement of the object.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void GLCanvas::resetOrientation() {
  const bool keepWireframe = model_state.wireframe;
  const bool keepDetail = model_state.detailOverlay;
  const bool keepVisible = model_state.modelVisible;
  model_state = ModelState{};
  motion_state = MotionState{};
  model_state.wireframe = keepWireframe;
  model_state.detailOverlay = keepDetail;
  model_state.modelVisible = keepVisible;
  updateStatusText(display_title + " reset.");
  syncRenderState(true);
  requestFrame();
}

//
// toggleWireframe
// Description:
//      Toggles wireframe rendering on or off.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void GLCanvas::toggleWireframe() {
  model_state.wireframe = !model_state.wireframe;
  requestFrame();
}

//
// toggleDetailOverlay
// Description:
//      Toggles the detail overlay on or off.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void GLCanvas::toggleDetailOverlay() {
  model_state.detailOverlay = !model_state.detailOverlay;
  requestFrame();
}

//
// toggleModelVisibility
// Description:
//      Toggles whether the loaded model is visible or not.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void GLCanvas::toggleModelVisibility() {
  model_state.modelVisible = !model_state.modelVisible;
  requestFrame();
}

//
// setChoosenModel
// Description:
//      Switches between loaded models.
//      Triggered externally from 'MyApp'.
// Parameters:
//      index <int>: Index of the model in the list, or -1 for the latest model.
// Returns:
//      None (void).
//
void GLCanvas::setChoosenModel(int index) {
  if (models.empty()) {
    return;
  }

  if (index == -1) {
    choosen_model = static_cast<int>(models.size()) - 1;
  } else if (index >= 0 && index < static_cast<int>(models.size())) {
    choosen_model = index;
  } else {
    return;
  }

  updateLoadedModelInformation(*models[choosen_model]);
}

//
// insertModel
// Description:
//      Inserts a new model into the vector of models.
//      Used for manually loading personal OBJ files.
// Parameters:
//      absolutePath <std::string>: Absolute path to the OBJ file.
// Returns:
//      <bool>: Whether the model was loaded successfully or not.
//
bool GLCanvas::insertModel(const std::string& absolutePath) {
  std::string error;
  if (!loadObjectVisual(fs::path(absolutePath), error)) {
    updateStatusText(error);
    return false;
  }
  requestFrame();
  return true;
}

//
// saveScreenshot
// Description:
//      Saves a BMP screenshot of the current OpenGL canvas.
// Parameters:
//      None (void).
// Returns:
//      None (void).
//
void GLCanvas::saveScreenshot() {
  if (!initialized) {
    return;
  }

  SetCurrent(m_context);
  drawScene();

  const int width = std::max(GetSize().GetWidth(), 1);
  const int height = std::max(GetSize().GetHeight(), 1);
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
    updateStatusText("Could not save screenshot.");
    return;
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

  updateStatusText("Screenshot saved to " + outputPath.string());
}

bool GLCanvas::wireframeEnabled() const {
  return model_state.wireframe;
}

bool GLCanvas::detailOverlayEnabled() const {
  return model_state.detailOverlay;
}

bool GLCanvas::modelVisible() const {
  return model_state.modelVisible;
}

std::string GLCanvas::getDisplayTitle() const {
  return display_title;
}

std::string GLCanvas::getDisplayFormat() const {
  return display_format;
}

std::string GLCanvas::getBoundsLabel() const {
  return bounds_label;
}

std::string GLCanvas::getStatusText() const {
  return status_text;
}

std::size_t GLCanvas::getTriangleCount() const {
  return triangle_count;
}

void GLCanvas::normalizeRotationAngles() {
  if (model_state.rotationX >= 360.0f || model_state.rotationX <= -360.0f) {
    model_state.rotationX = std::fmod(model_state.rotationX, 360.0f);
  }
  if (model_state.rotationY >= 360.0f || model_state.rotationY <= -360.0f) {
    model_state.rotationY = std::fmod(model_state.rotationY, 360.0f);
  }
  if (render_model_state.rotationX >= 360.0f || render_model_state.rotationX <= -360.0f) {
    render_model_state.rotationX = std::fmod(render_model_state.rotationX, 360.0f);
  }
  if (render_model_state.rotationY >= 360.0f || render_model_state.rotationY <= -360.0f) {
    render_model_state.rotationY = std::fmod(render_model_state.rotationY, 360.0f);
  }
}

void GLCanvas::syncRenderState(bool snap) {
  if (snap) {
    render_model_state = model_state;
    motion_state = MotionState{};
    return;
  }

  render_model_state.autoSpin = model_state.autoSpin;
  render_model_state.wireframe = model_state.wireframe;
  render_model_state.modelVisible = model_state.modelVisible;
  render_model_state.detailOverlay = model_state.detailOverlay;
}

void GLCanvas::requestFrame(bool immediate) {
  Refresh(false);
  if (immediate) {
    Update();
  }
}
