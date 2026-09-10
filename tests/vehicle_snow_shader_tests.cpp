#include <cmath>
#include <cstddef>
#include <cstdio>
#include <glm/glm.hpp>
#include "app/snowplow_mesh.h"
#include "test_assert.h"

// Compile the exact shader implementation as C++. This cannot silently keep
// testing an old mask while the rendered windshield uses a different formula.
namespace shader {
using glm::vec2;
using glm::vec3;
using glm::atan;
using glm::length;
using glm::max;
using glm::min;
using glm::smoothstep;
#include "../assets/shaders/vehicle_snow.glsl"
}

namespace {
float remaining(float u, float v, float aspect) {
    return shader::windshield_snow_remaining({u,v,aspect});
}

void clear_views_and_snowy_seals() {
    for (int aspect_step=0;aspect_step<=30;++aspect_step) {
        const float aspect=1.f+static_cast<float>(aspect_step)*.1f;
        // The forward view through each wiper fan is completely transparent
        // to this snow effect, with no translucent frost over the clear area.
        REQUIRE(remaining(.23f,.45f,aspect)==0.f);
        REQUIRE(remaining(.73f,.45f,aspect)==0.f);
        for (int along=0;along<=40;++along) {
            const float t=static_cast<float>(along)/40.f;
            REQUIRE(remaining(.001f,t,aspect)==1.f);
            REQUIRE(remaining(.999f,t,aspect)==1.f);
            REQUIRE(remaining(t,.001f,aspect)==1.f);
            REQUIRE(remaining(t,.999f,aspect)==1.f);
        }
    }
    apricot_test::pass("both forward views clear exactly while the windshield seals retain snow");
}

void swept_edge_is_curved() {
    // At the same height, the first fan reaches its centre but leaves its
    // outer corner snowy. Lower down, that same corner falls inside the arc.
    REQUIRE(remaining(.23f,.84f,2.f)==0.f);
    REQUIRE(remaining(.07f,.84f,2.f)==1.f);
    REQUIRE(remaining(.07f,.65f,2.f)==0.f);
    const float feather=remaining(.07f,.81f,2.f);
    REQUIRE(feather>0.f && feather<1.f);
    // Pane aspect is physical input: the same UV on a square pane fits inside
    // the arc, while its wider-pane counterpart above remains snowy.
    REQUIRE(remaining(.07f,.84f,1.f)==0.f);
    apricot_test::pass("wiper outlines form curved arcs with a narrow feathered edge");
}

void dense_mask_bounds() {
    std::size_t clear=0,snow=0,feather=0;
    for (int aspect_step=0;aspect_step<=30;++aspect_step) {
        const float aspect=1.f+static_cast<float>(aspect_step)*.1f;
        for (int y=0;y<=100;++y) for (int x=0;x<=100;++x) {
            const float value=remaining(static_cast<float>(x)/100.f,
                                        static_cast<float>(y)/100.f,aspect);
            REQUIRE(std::isfinite(value));
            REQUIRE(value>=0.f && value<=1.f);
            if (value==0.f) ++clear;
            else if (value==1.f) ++snow;
            else ++feather;
        }
    }
    REQUIRE(clear>10000u);
    REQUIRE(snow>10000u);
    REQUIRE(feather>1000u);
    std::printf("  mask samples: %zu clear, %zu snowy, %zu feathered\n",clear,snow,feather);
    apricot_test::pass("actual shader mask stays finite and bounded across 31 pane aspect ratios");
}

void plow_tags_only_the_front_windshield() {
    const auto parts=apricot::make_snowplow_meshes();
    std::size_t tagged_vertices=0,tagged_triangles=0,ordinary_vertices=0;
    for (std::size_t part=0;part<parts.size();++part) {
        const auto& mesh=parts[part];
        for (const auto& vertex:mesh.vertices) {
            const auto pane=vertex.material_weights;
            if (pane.w==-1.f) {
                ++tagged_vertices;
                REQUIRE(part==1u);
                REQUIRE(vertex.normal==glm::vec3(0.f,0.f,-1.f));
                REQUIRE_NEAR(vertex.position.z,-1.993f,.00001f);
                REQUIRE(pane.x==0.f || pane.x==1.f);
                REQUIRE(pane.y==0.f || pane.y==1.f);
                REQUIRE_NEAR(pane.z,1.93f/.61f,.00001f);
                REQUIRE_NEAR(vertex.position.x,pane.x*1.93f-.965f,.00001f);
                REQUIRE_NEAR(vertex.position.y,pane.y*.61f+1.665f,.00001f);
                // The original box UV follows its outward face frame. Snow
                // coordinates must not overwrite or mirror that texture map.
                REQUIRE_NEAR(vertex.uv.x,1.f-pane.x,.00001f);
                REQUIRE_NEAR(vertex.uv.y,pane.y,.00001f);
            } else {
                ++ordinary_vertices;
                REQUIRE(pane==glm::vec4(0.f));
            }
        }
        REQUIRE(mesh.indices.size()%3u==0u);
        for (std::size_t i=0;i<mesh.indices.size();i+=3u) {
            int pane_corners=0;
            for (std::size_t corner=0;corner<3u;++corner) {
                REQUIRE(mesh.indices[i+corner]<mesh.vertices.size());
                if (mesh.vertices[mesh.indices[i+corner]].material_weights.w==-1.f)
                    ++pane_corners;
            }
            REQUIRE(pane_corners==0 || pane_corners==3);
            if (pane_corners==3) ++tagged_triangles;
        }
    }
    REQUIRE(tagged_vertices==4u);
    REQUIRE(tagged_triangles==2u);
    REQUIRE(ordinary_vertices>100u);
    apricot_test::pass("only the two outward front windshield triangles carry snow metadata");
}
}

int main() {
    clear_views_and_snowy_seals();
    swept_edge_is_curved();
    dense_mask_bounds();
    plow_tags_only_the_front_windshield();
    return 0;
}
