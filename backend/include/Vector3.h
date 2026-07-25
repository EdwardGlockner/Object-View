#pragma once

#include <cmath>

namespace object_view {

struct Vector3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  Vector3() = default;
  Vector3(double xValue, double yValue, double zValue)
      : x(xValue), y(yValue), z(zValue) {}

  Vector3 operator+(const Vector3& other) const {
    return {x + other.x, y + other.y, z + other.z};
  }

  Vector3 operator-(const Vector3& other) const {
    return {x - other.x, y - other.y, z - other.z};
  }

  Vector3& operator+=(const Vector3& other) {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
  }

  Vector3 operator/(double value) const {
    return {x / value, y / value, z / value};
  }

  double length() const {
    return std::sqrt(x * x + y * y + z * z);
  }
};

}  // namespace object_view
