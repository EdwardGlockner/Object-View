#include "Model.h"

#include <filesystem>
#include <iostream>
#include <string>

#include <httplib.h>

namespace fs = std::filesystem;

namespace {

std::string jsonError(const std::string& message) {
  std::string escaped;
  escaped.reserve(message.size());
  for (char ch : message) {
    if (ch == '"' || ch == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return "{\"error\":\"" + escaped + "\"}";
}

void sendJson(httplib::Response& response, const std::string& body, int status = 200) {
  response.status = status;
  response.set_header("Access-Control-Allow-Origin", "*");
  response.set_header("Access-Control-Allow-Headers", "Content-Type");
  response.set_content(body, "application/json");
}

fs::path findWebRoot(const char* overridePath) {
  if (overridePath && *overridePath != '\0') {
    return fs::absolute(overridePath);
  }

  const fs::path current = fs::current_path();
  const fs::path dist = current / "web" / "dist";
  if (fs::exists(dist)) {
    return dist;
  }

  return current / "web";
}

}  // namespace

int main(int argc, char** argv) {
  int port = 8080;
  const char* webRootOverride = nullptr;

  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--port" && index + 1 < argc) {
      port = std::stoi(argv[++index]);
    } else if (arg == "--web-root" && index + 1 < argc) {
      webRootOverride = argv[++index];
    }
  }

  const fs::path webRoot = findWebRoot(webRootOverride);
  httplib::Server server;

  server.Options(".*", [](const httplib::Request&, httplib::Response& response) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Headers", "Content-Type");
  });

  server.Get("/api/health", [](const httplib::Request&, httplib::Response& response) {
    sendJson(response, "{\"status\":\"ok\",\"backend\":\"cpp\"}");
  });

  server.Get("/api/samples", [](const httplib::Request&, httplib::Response& response) {
    sendJson(response,
             "{\"samples\":["
             "{\"id\":\"drone-frame\",\"name\":\"Drone frame\",\"path\":\"/samples/drone-frame/drone_frame.obj\"},"
             "{\"id\":\"calibration-cube\",\"name\":\"Calibration cube\",\"path\":\"/samples/calibration-cube/calibration_cube.obj\"},"
             "{\"id\":\"axis-marker\",\"name\":\"Axis marker\",\"path\":\"/samples/axis-marker/axis_marker.obj\"}"
             "]}");
  });

  server.Post("/api/inspect", [](const httplib::Request& request, httplib::Response& response) {
    if (request.body.empty()) {
      sendJson(response, jsonError("Request body is empty."), 400);
      return;
    }

    object_view::Model model;
    std::string error;
    const std::string sourceName = request.get_header_value("X-Model-Name").empty()
                                       ? "uploaded.obj"
                                       : request.get_header_value("X-Model-Name");

    if (!model.loadFromString(request.body, fs::current_path(), sourceName, error)) {
      sendJson(response, jsonError(error), 422);
      return;
    }

    sendJson(response, object_view::toJson(model.stats()));
  });

  if (fs::exists(webRoot)) {
    server.set_mount_point("/", webRoot.string());
  } else {
    std::cerr << "Warning: web root does not exist: " << webRoot << '\n';
  }

  std::cout << "ObjectView C++ backend listening on http://127.0.0.1:" << port << '\n';
  std::cout << "Serving web root: " << webRoot << '\n';

  if (!server.listen("0.0.0.0", port)) {
    std::cerr << "Could not start server on port " << port << '\n';
    return 1;
  }

  return 0;
}
