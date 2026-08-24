// GLCanvas.h

//*********************************************************************************
// Header guard
//*********************************************************************************
#ifndef __GLCANVAS_H
#define __GLCANVAS_H

//*********************************************************************************
// Headers
//*********************************************************************************
#define GL_SILENCE_DEPRECATION

#include "Model.h"

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <wx/timer.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <propidl.h>
    #include <gdiplus.h>
    #include <mmsystem.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
    #include <OpenGL/gl.h>
    #include <OpenGL/glu.h>
#else
    #include <GL/gl.h>
    #include <GL/glu.h>
#endif

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

//*********************************************************************************
// Class
//*********************************************************************************
class GLCanvas : public wxGLCanvas {
    public:
        struct CameraState {
            bool dragging = false;
            bool panning = false;
            wxPoint lastPosition;
        };

        struct ModelState {
            float rotationX = -4.6f;
            float rotationY = 41.2f;
            float positionX = 0.0f;
            float positionY = 0.0f;
            float positionZ = 0.0f;
            float zoom = 1.35f;
            bool autoSpin = false;
            bool wireframe = false;
            bool modelVisible = true;
            bool detailOverlay = false;
        };

        struct TextureStore {
            std::map<std::string, GLuint> textures;
            GLuint checkerTexture = 0;
        };

        struct RenderLists {
            GLuint solid = 0;
            GLuint overlay = 0;
        };

        struct MotionState {
            float rotationVelocityX = 0.0f;
            float rotationVelocityY = 0.0f;
            float panVelocityX = 0.0f;
            float panVelocityY = 0.0f;
            float zoomVelocity = 0.0f;
        };

        // Constructors and destructors
        GLCanvas(wxFrame* parent, int* args, const std::filesystem::path& initialPath = {});
        ~GLCanvas() override;

        // Public class functions
        GLvoid establishProjectionMatrix(GLsizei width, GLsizei height);
        GLvoid initGL(GLsizei width, GLsizei height);
        GLvoid displayFPS(GLvoid);
        GLvoid drawScene(GLvoid);
        GLboolean checkNavigation(double deltaSeconds);

        // Event handlers
        void OnIdle(wxIdleEvent& event);
        void OnTimer(wxTimerEvent& event);
        void OnPaint(wxPaintEvent& event);
        void OnEraseBackground(wxEraseEvent& event);
        void OnKeyDown(wxKeyEvent& event);
        void OnKeyUp(wxKeyEvent& event);
        void OnResize(wxSizeEvent& event);
        void OnLeftDown(wxMouseEvent& event);
        void OnLeftUp(wxMouseEvent& event);
        void OnRightDown(wxMouseEvent& event);
        void OnRightUp(wxMouseEvent& event);
        void OnMouseMove(wxMouseEvent& event);
        void OnMouseWheel(wxMouseEvent& event);

        // Setter functions
        void setAutoRotate();
        void resetAngles();
        void resetOrientation();
        void toggleWireframe();
        void toggleDetailOverlay();
        void toggleModelVisibility();
        void setChoosenModel(int index);
        bool insertModel(const std::string& absolutePath);
        void saveScreenshot();

        // State getters used by the wxWidgets UI
        bool wireframeEnabled() const;
        bool detailOverlayEnabled() const;
        bool modelVisible() const;
        std::string getDisplayTitle() const;
        std::string getDisplayFormat() const;
        std::string getBoundsLabel() const;
        std::string getStatusText() const;
        std::size_t getTriangleCount() const;

    private:
        // Private class functions
        void initializeDefaultAssets();
        void loadDefaultAsset();
        bool loadObjectVisual(const std::filesystem::path& path, std::string& error);
        void updateLoadedModelInformation(const object_view::Model& model);
        void ensureModelRenderLists(const object_view::Model& model);
        void advanceFrame(double deltaSeconds);
        void updateStatusText(const std::string& text);
        void normalizeRotationAngles();
        void syncRenderState(bool snap = false);
        void requestFrame(bool immediate = false);

        // Private class members
        bool keys[500];
        bool initialized;

        CameraState camera_state;
        ModelState model_state;
        ModelState render_model_state;
        MotionState motion_state;
        wxTimer* timer;
        wxGLContext m_context;

        std::vector<std::unique_ptr<object_view::Model>> owned_models;
        std::vector<object_view::Model*> models;
        std::vector<std::filesystem::path> sample_assets;
        int choosen_model;

        TextureStore texture_store;
        std::map<const object_view::Model*, RenderLists> render_lists;

        std::filesystem::path loaded_path;
        std::string display_title;
        std::string display_format;
        std::string bounds_label;
        std::string status_text;
        std::size_t triangle_count;

        const GLfloat autoRotateY_speed = 50.0f;
        const GLfloat zoom_speed = 1.6f;
        const GLfloat translate_speed = 4.5f;
        const GLfloat pan_drag_speed = 0.008f;
        const GLfloat rotate_drag_speed = 0.28f;
        const GLfloat rotate_velocity_limit = 380.0f;
        const GLfloat pan_velocity_limit = 2.8f;

        std::chrono::steady_clock::time_point lastFrameTime;
        std::chrono::steady_clock::time_point lastMouseTime;
};

#endif
