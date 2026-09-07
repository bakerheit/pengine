#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "app/emergency_lighting.h"
#include "app/player_car_catalog.h"
#include "app/vehicle_driver_door.h"
#include "app/vehicle_driver_pose.h"
#include "app/vehicle_model_tuning.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr std::array<PlayerCarId, 5> kAttempts{
    PlayerCarId::MunicipalCruiser91A,
    PlayerCarId::MunicipalCruiser91B,
    PlayerCarId::MunicipalCruiser91C,
    PlayerCarId::MunicipalCruiser91D,
    PlayerCarId::MunicipalCruiser91E,
};

uint32_t be32(const unsigned char* bytes) {
    return (uint32_t{bytes[0]} << 24u) | (uint32_t{bytes[1]} << 16u) |
           (uint32_t{bytes[2]} << 8u) | uint32_t{bytes[3]};
}

void rgba_png(const std::string& path, uint32_t expected_size) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::array<unsigned char, 26> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    REQUIRE(file.gcount() == static_cast<std::streamsize>(header.size()));
    constexpr unsigned char signature[]{137, 80, 78, 71, 13, 10, 26, 10};
    for (std::size_t i = 0; i < 8; ++i) REQUIRE(header[i] == signature[i]);
    REQUIRE(std::string(reinterpret_cast<const char*>(header.data() + 12), 4) ==
            "IHDR");
    REQUIRE(be32(header.data() + 16) == expected_size);
    REQUIRE(be32(header.data() + 20) == expected_size);
    REQUIRE(header[24] == 8u);
    REQUIRE(header[25] == 6u);  // true RGBA, not palette metadata
}

uint64_t file_hash(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    uint64_t hash = 1469598103934665603ull;
    char byte = 0;
    while (file.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool contains(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    const auto cross = [](glm::vec2 u, glm::vec2 v) {
        return u.x * v.y - u.y * v.x;
    };
    const float d0 = cross(b - a, p - a);
    const float d1 = cross(c - b, p - b);
    const float d2 = cross(a - c, p - c);
    return (d0 > 1e-6f && d1 > 1e-6f && d2 > 1e-6f) ||
           (d0 < -1e-6f && d1 < -1e-6f && d2 < -1e-6f);
}

bool side_covers(const StaticEmesh& mesh, glm::vec2 zy, float side) {
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3u) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1u]];
        const auto& c = mesh.vertices[mesh.indices[i + 2u]];
        if (side * a.px < .82f || side * b.px < .82f || side * c.px < .82f)
            continue;
        if (contains(zy, {a.pz, a.py}, {b.pz, b.py}, {c.pz, c.py}))
            return true;
    }
    return false;
}

bool top_covers(const StaticEmesh& mesh, glm::vec2 xz, float minimum_y) {
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3u) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1u]];
        const auto& c = mesh.vertices[mesh.indices[i + 2u]];
        if (a.py < minimum_y || b.py < minimum_y || c.py < minimum_y) continue;
        if (contains(xz, {a.px, a.pz}, {b.px, b.pz}, {c.px, c.pz}))
            return true;
    }
    return false;
}

void check_attempt(PlayerCarId id, std::set<uint64_t>& body_hashes) {
    const auto& definition = player_car_definition(id);
    REQUIRE(is_municipal_cruiser_91(id));
    REQUIRE(has_animated_driver(id));
    REQUIRE(has_police_lightbar(id));
    REQUIRE(std::string{definition.brand} == "MUNICIPAL");
    REQUIRE(definition.physical_half_wheelbase > 1.40f);
    REQUIRE(definition.physical_half_track == definition.wheel_x);

    const std::string body_path = asset_path(definition.mesh_path);
    const std::string root = body_path.substr(0, body_path.find_last_of('/') + 1u);
    StaticEmesh body, open_body, door;
    REQUIRE(read_static_emesh(body_path, body));
    REQUIRE(read_static_emesh(root + "body_open.emesh", open_body));
    REQUIRE(read_static_emesh(root + "driver_door.emesh", door));
    REQUIRE(body.bounds.valid() && open_body.bounds.valid() && door.bounds.valid());
    REQUIRE(body.indices.size() / 3u >= 650u);
    REQUIRE(body.indices.size() / 3u <= 1800u);
    REQUIRE(open_body.indices.size() / 3u >= 550u);
    REQUIRE(open_body.indices.size() / 3u <= 2400u);
    REQUIRE(door.indices.size() / 3u >= 20u);
    REQUIRE(door.indices.size() / 3u <= 360u);
    REQUIRE(body.bounds.size().x > 1.90f && body.bounds.size().x < 2.30f);
    REQUIRE(body.bounds.size().z > 5.00f && body.bounds.size().z < 5.75f);
    REQUIRE(body.bounds.max.y > 1.68f && body.bounds.max.y < 1.95f);
    REQUIRE_NEAR(body.bounds.max.x, -body.bounds.min.x, .015f);
    REQUIRE(door.bounds.size().z > .80f && door.bounds.size().z < 1.55f);
    REQUIRE(door.bounds.size().y > .65f && door.bounds.size().y < 1.35f);

    for (const auto* mesh : {&body, &open_body, &door}) {
        REQUIRE(!mesh->vertices.empty() && mesh->indices.size() % 3u == 0u);
        for (const auto& vertex : mesh->vertices) {
            REQUIRE(std::isfinite(vertex.px) && std::isfinite(vertex.py) &&
                    std::isfinite(vertex.pz));
            REQUIRE(vertex.u >= 0.f && vertex.u <= 1.f);
            REQUIRE(vertex.v >= 0.f && vertex.v <= 1.f);
        }
    }

    for (const float axle : {definition.wheel_front_z,
                             -definition.wheel_rear_z}) {
        for (const float side : {-1.f, 1.f}) {
            for (const float dz : {-.10f, 0.f, .10f}) {
                REQUIRE(!side_covers(body,
                                     {axle + dz, definition.arch_centre_y},
                                     side));
            }
        }
    }
    REQUIRE(top_covers(body, {0.f, definition.wheel_front_z - .11f},
                             definition.arch_centre_y + .40f));
    REQUIRE(top_covers(body, {0.f, definition.wheel_front_z + .11f},
                             definition.arch_centre_y + .40f));
    REQUIRE(top_covers(body, {0.f, -definition.wheel_rear_z - .11f},
                             definition.arch_centre_y + .38f));

    constexpr std::array<const char*, 6> panes{
        "windshield", "rear_glass", "passenger_glass", "driver_glass",
        "driver_rear_glass", "passenger_rear_glass"};
    for (const char* pane : panes) {
        StaticEmesh glass;
        REQUIRE(read_static_emesh(root + pane + ".emesh", glass));
        REQUIRE(glass.bounds.valid());
        REQUIRE(glass.indices.size() >= 6u && glass.indices.size() <= 72u);
    }

    const auto contract = vehicle_driver_door(id);
    REQUIRE(contract.hinge.x > .85f && contract.hinge.x < 1.15f);
    REQUIRE(contract.handle.z > contract.rear_z &&
            contract.handle.z < contract.front_z);
    Transform closed;
    const Transform opened = vehicle_driver_door_transform(id, closed, 1.f);
    REQUIRE(opened.transform_point(contract.handle).x > contract.handle.x + .45f);
    const auto& driver = vehicle_driver_layout(id);
    REQUIRE(driver.hip.y > .55f && driver.hip.y < 1.05f);
    REQUIRE(driver.hip.z > contract.rear_z && driver.hip.z < contract.front_z);

    rgba_png(asset_path(definition.texture_path), 256u);
    const auto slash = std::string{definition.texture_path}.find_last_of('/');
    const std::string texture_root = asset_path(
        std::string{definition.texture_path}.substr(0, slash + 1u));
    REQUIRE(std::filesystem::is_regular_file(texture_root +
                                             "body-imagegen-source.png"));
    body_hashes.insert(file_hash(body_path));
    std::printf("  %s: body %zu tris, open %zu, door %zu, %.2f x %.2f x %.2f m\n",
                definition.model, body.indices.size() / 3u,
                open_body.indices.size() / 3u, door.indices.size() / 3u,
                body.bounds.size().x, body.bounds.size().y,
                body.bounds.size().z);
}

}  // namespace

int main() {
    std::set<uint64_t> body_hashes;
    for (const auto id : kAttempts) check_attempt(id, body_hashes);
    REQUIRE(body_hashes.size() == kAttempts.size());
    apricot_test::pass(
        "five distinct 1991 police studies keep open wells, articulated doors, separate glass and imagegen atlases");
    return apricot_test::done("municipal_cruiser_91_tests");
}
