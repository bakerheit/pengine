#pragma once

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "road/lane_graph.h"
#include "city/city_rng.h"

namespace apricot {

// Small, traffic-owned walking network. Two physical footways per road,
// traversable in either direction. Car turn restrictions are irrelevant here.
struct PedWalkLine {
    std::vector<glm::vec3> points;
    std::vector<float> cum;
    float length = 0.0f;

    void build(std::vector<glm::vec3> p) {
        points = std::move(p);
        cum.assign(points.size(), 0.0f);
        for (std::size_t i = 1; i < points.size(); ++i)
            cum[i] = cum[i - 1] + glm::length(points[i] - points[i - 1]);
        length = cum.empty() ? 0.0f : cum.back();
    }

    LanePose pose(float distance, float lateral = 0.0f) const {
        LanePose result;
        if (points.size() < 2) return result;
        const float d = std::clamp(distance, 0.0f, length);
        auto it = std::upper_bound(cum.begin() + 1, cum.end(), d);
        const std::size_t i = std::min(
            static_cast<std::size_t>(it - cum.begin()) - 1, points.size() - 2);
        const float span = cum[i + 1] - cum[i];
        const float u = span > 1e-5f ? (d - cum[i]) / span : 0.0f;
        result.position = glm::mix(points[i], points[i + 1], u);
        if (span > 1e-5f) result.tangent = (points[i + 1] - points[i]) / span;
        const glm::vec3 right = glm::cross(result.tangent, glm::vec3{0, 1, 0});
        if (glm::length(right) > 1e-5f) result.right = glm::normalize(right);
        // Join the centre at vertices. An offset along a discontinuous segment
        // normal otherwise teleports a walker sideways at every road bend.
        const float blend = std::clamp(std::min(d - cum[i], cum[i + 1] - d) / 3.0f,
                                       0.0f, 1.0f);
        result.position += result.right * lateral * blend;
        return result;
    }
};

struct PedWalkLink {
    uint32_t to = 0;
    bool crossing = false;
    PedWalkLine line;
};

struct PedWalkPath {
    LaneRef lane = kInvalidLane;
    float side = 1.0f;
    PedWalkLine line;
    std::vector<PedWalkLink> links;
};

class PedestrianPaths {
public:
    static constexpr uint32_t invalid = 0xFFFFFFFFu;

    void clear() { paths_.clear(); aliases_.clear(); }
    const PedWalkPath& path(uint32_t id) const { return paths_[id]; }
    bool valid(uint32_t id) const {
        return id < paths_.size() && paths_[id].line.length > 0.1f;
    }
    uint32_t for_lane(LaneRef lane, uint32_t slot) const {
        if (lane >= aliases_.size() || aliases_[lane] == kInvalidLane) return invalid;
        const uint32_t id = aliases_[lane] * 2u + (slot & 1u);
        return valid(id) ? id : invalid;
    }

    // Candidate order comes from physical adjacency, entropy from birth
    // identity and decision count. Neither lane indices nor activation order
    // enter the roll. Prefer continuing round a corner to recrossing a road.
    uint32_t choose(uint32_t id, uint64_t seed, uint64_t birth_key,
                    uint32_t slot, uint32_t decisions) const {
        const auto& links = paths_[id].links;
        const uint64_t h = hash_coord3(seed ^ birth_key,
            static_cast<int32_t>(slot), static_cast<int32_t>(decisions), 0x504544);
        return links.size() > 1 && h % 4u == 0u ? 1u : 0u;
    }

    void build(const LaneGraph& graph, float sidewalk_offset) {
        clear();
        paths_.resize(graph.lane_count() * 2u);
        aliases_.assign(graph.lane_count(), kInvalidLane);
        // Include non-walkable arms in adjacency: a corner must never silently
        // cut across a dirt road, freeway ramp or alley mouth.
        std::vector<std::vector<LaneRef>> arms(graph.junction_count());
        for (LaneRef lr = 0; lr < graph.lane_count(); ++lr) {
            const Lane& l = graph.lane(lr);
            if (l.index == 0) arms[l.junction_from].push_back(lr);
            if (!road_class_def(l.cls).sidewalks) continue;
            for (LaneRef candidate : graph.junction(l.junction_from).outgoing) {
                const Lane& c = graph.lane(candidate);
                if (c.edge == l.edge && c.index == 0) {
                    aliases_[lr] = candidate;
                    break;
                }
            }
        }
        for (auto& junction_arms : arms) {
            std::sort(junction_arms.begin(), junction_arms.end(), [&](LaneRef a, LaneRef b) {
                const glm::vec3 ta = graph.pose(a, 0).tangent;
                const glm::vec3 tb = graph.pose(b, 0).tangent;
                const float aa = std::atan2(ta.z, ta.x), ab = std::atan2(tb.z, tb.x);
                return aa == ab ? graph.lane(a).key < graph.lane(b).key : aa < ab;
            });
        }
        auto trim = [&](uint32_t junction, LaneRef own) {
            const glm::vec3 tangent = graph.pose(own, 0).tangent;
            const float half = graph.lane(own).width_m * 0.5f + sidewalk_offset;
            float distance = half + 1.0f;
            for (LaneRef lr : arms[junction]) {
                if (graph.lane(lr).edge == graph.lane(own).edge) continue;
                const glm::vec3 other = graph.pose(lr, 0).tangent;
                const float cosine = glm::dot(tangent, other);
                const float sine = std::fabs(tangent.x * other.z - tangent.z * other.x);
                // Acute junctions need a longer setback so even the near end
                // of a crossing clears the neighbouring carriageway.
                if (cosine > -0.95f)
                    distance = std::max(distance,
                        (graph.lane(lr).width_m * 0.5f + sidewalk_offset + 1.0f +
                         half * std::fabs(cosine)) / std::max(0.05f, sine));
            }
            return distance;
        };
        // Build once in authored direction, then reverse exactly. Opposing
        // walkers must agree on where the same pavement actually is.
        for (LaneRef lr = 0; lr < graph.lane_count(); ++lr) {
            const Lane& l = graph.lane(lr);
            if (aliases_[lr] != lr || !l.forward) continue;
            LaneRef reverse = kInvalidLane;
            for (LaneRef other : arms[l.junction_to])
                if (graph.lane(other).edge == l.edge) reverse = other;
            if (!graph.valid(reverse)) continue;
            for (uint32_t side_index = 0; side_index < 2; ++side_index) {
                const float side = side_index == 0 ? 1.0f : -1.0f;
                const float offset = side * (l.width_m * 0.5f + sidewalk_offset) - l.lateral_offset_m;
                std::vector<glm::vec3> points;
                for (std::size_t i = 0; i < l.centreline.size(); ++i) {
                    const std::size_t before = i == 0 ? 0 : i - 1;
                    const std::size_t after = std::min(i + 1, l.centreline.size() - 1);
                    const glm::vec3 tangent = l.centreline[after] - l.centreline[before];
                    const glm::vec3 right = glm::cross(tangent, glm::vec3{0, 1, 0});
                    if (glm::length(right) <= 1e-5f) continue;
                    points.push_back(l.centreline[i] + glm::normalize(right) * offset);
                }
                PedWalkLine full;
                full.build(std::move(points));
                if (full.length <= 1.0f) continue;
                const float begin = trim(l.junction_from, lr);
                const float end = full.length - trim(l.junction_to, reverse);
                // A tiny road swallowed by its junctions has no safe walking
                // run. Do not shrink the setbacks into live carriageways.
                if (end - begin < 1.0f) continue;
                points = {full.pose(begin).position};
                for (std::size_t i = 1; i + 1 < full.points.size(); ++i)
                    if (full.cum[i] > begin && full.cum[i] < end) points.push_back(full.points[i]);
                points.push_back(full.pose(end).position);
                PedWalkPath& out = paths_[lr * 2u + side_index];
                out.lane = lr;
                out.side = side;
                out.line.build(points);
                std::reverse(points.begin(), points.end());
                PedWalkPath& back = paths_[reverse * 2u + (1u - side_index)];
                back.lane = reverse;
                back.side = -side;
                back.line.build(std::move(points));
            }
        }
        for (uint32_t id = 0; id < paths_.size(); ++id) {
            if (!valid(id)) continue;
            PedWalkPath& p = paths_[id];
            const Lane& lane = graph.lane(p.lane);
            const auto& incident = arms[lane.junction_to];
            const auto reverse_it = std::find_if(incident.begin(), incident.end(), [&](LaneRef lr) {
                return graph.lane(lr).edge == lane.edge;
            });
            if (reverse_it == incident.end()) continue;
            const LaneRef reverse = *reverse_it;
            const std::size_t index = static_cast<std::size_t>(reverse_it - incident.begin());
            const std::size_t next = (index + (p.side > 0 ? incident.size() - 1u : 1u)) % incident.size();
            const uint32_t corner = incident[next] * 2u + (id & 1u);
            if (incident.size() > 1 && valid(corner)) add_corner(id, corner);
            // Crossing is perpendicular to this road at its mouth, never a
            // diagonal car-turn chord through the middle of the junction.
            const uint32_t across = reverse * 2u + (id & 1u);
            if (incident.size() > 1 && valid(across)) {
                PedWalkLink link;
                link.to = across;
                link.crossing = true;
                link.line.build({p.line.points.back(), paths_[across].line.points.front()});
                p.links.push_back(std::move(link));
            }
            if (p.links.empty()) {
                // A dead end or an unsupported acute corner turns around on
                // the SAME footway. No road-centre teleport or endless spin.
                PedWalkLink link;
                link.to = reverse * 2u + (1u - (id & 1u));
                link.line.build({p.line.points.back(), p.line.points.back()});
                p.links.push_back(std::move(link));
            }
        }
    }

private:
    void add_corner(uint32_t from, uint32_t to) {
        const LanePose a = paths_[from].line.pose(paths_[from].line.length);
        const LanePose b = paths_[to].line.pose(0.0f);
        const glm::vec2 ta{a.tangent.x, a.tangent.z}, tb{b.tangent.x, b.tangent.z};
        const glm::vec2 delta{b.position.x - a.position.x, b.position.z - a.position.z};
        auto cross = [](glm::vec2 x, glm::vec2 y) { return x.x * y.y - x.y * y.x; };
        const float det = cross(ta, tb);
        std::vector<glm::vec3> points{a.position};
        if (std::fabs(det) > 0.05f) {
            const float along_a = cross(delta, tb) / det;
            const float along_b = cross(delta, ta) / det;
            // Only join forward rays which meet before the outgoing mouth.
            // Acute/short geometry without room stays disconnected safely.
            if (along_a < 0 || along_b > 0 || along_a > 60 || along_b < -60) return;
            glm::vec3 corner = a.position + a.tangent * along_a;
            corner.y = (a.position.y + b.position.y) * 0.5f;
            points.push_back(corner);
        } else if (glm::dot(ta, tb) < 0.95f) {
            return;
        }
        points.push_back(b.position);
        PedWalkLink link;
        link.to = to;
        link.line.build(std::move(points));
        paths_[from].links.push_back(std::move(link));
    }

    std::vector<PedWalkPath> paths_;
    std::vector<LaneRef> aliases_;
};

}  // namespace apricot
