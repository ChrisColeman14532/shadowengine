#pragma once

#include <string>
#include <glm/glm.hpp>

namespace CoreEngine {

    struct Vector3 {
        float x, y, z;
        Vector3() : x(0), y(0), z(0) {}
        Vector3(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}
        Vector3(glm::vec3 v) : x(v.x), y(v.y), z(v.z) {}
        Vector3 operator-(const Vector3& other) const { return Vector3(x - other.x, y - other.y, z - other.z); }
        Vector3 operator+(const Vector3& other) const { return Vector3(x + other.x, y + other.y, z + other.z); }
    };

    struct EngineInfo {
        int majorVersion;
        int minorVersion;
        std::string name;
    };

} // namespace CoreEngine
