#pragma once

#include <type_traits>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace tine {

#define CHECK_COMPONENT_POD(Component)                                                             \
    static_assert(std::is_pod<Component>::value, #Component "must be pod type")

struct TransformComponent {
    glm::mat4 transform;
};
CHECK_COMPONENT_POD(TransformComponent);

struct CameraComponent {
    glm::mat4 projection_matrix;
    glm::mat4 view_matrix;
    void set_orthographic(float left, float right, float bottom, float top, float z_near,
                          float z_far) {
        projection_matrix = glm::ortho(left, right, bottom, top, z_near, z_far);
    }
    void set_perspective(float fovy, float aspect, float z_near, float z_far) {
        projection_matrix = glm::perspective(glm::radians(fovy), aspect, z_near, z_far);
    }
    void look_at(const glm::vec3 &eye, const glm::vec3 &target, const glm::vec3 &up) {
        view_matrix = glm::lookAt(eye, target, up);
    }
};
CHECK_COMPONENT_POD(CameraComponent);

// TODO
struct MeshComponent {};
CHECK_COMPONENT_POD(MeshComponent);

// TODO
struct MaterialComponent {};
CHECK_COMPONENT_POD(MaterialComponent);

} // namespace tine