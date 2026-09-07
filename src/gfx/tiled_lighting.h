#pragma once
#include <array>
#include <vector>
#include <glad/gl.h>
#include "gfx/tiled_light_grid.h"

namespace apricot {
class TiledLighting {
public:
    TiledLighting() = default;
    TiledLighting(const TiledLighting&) = delete;
    TiledLighting& operator=(const TiledLighting&) = delete;
    bool upload(const std::vector<TrafficSpotLight>& lights, const glm::mat4& vp,
                const glm::mat4& view, int width, int height);
    TrafficLightView view() const { return view_; }
    const TiledLightGrid& grid() const { return grid_; }
    double build_ms() const { return build_ms_; }
    double upload_ms() const { return upload_ms_; }
    void begin_timing(bool lights_on);
    void end_timing();
    const std::array<std::vector<double>,2>& timings() const { return timings_; }
    void destroy();
private:
    bool buffer(int slot,GLenum format,std::size_t texels,std::size_t bytes,const void* data);
    // Rotate storage so uploading this frame never overwrites the immediately
    // previous frame's light lists while the GPU is still reading them.
    std::array<GLuint,12> textures_{};
    std::array<GLuint,12> buffers_{};
    GLint max_texels_=0;
    int texture_base_=0;
    double build_ms_=0, upload_ms_=0;
    TiledLightGrid grid_;
    TrafficLightView view_;
    struct Query { GLuint id=0; bool pending=false; bool on=false; };
    std::array<Query,8> queries_{};
    int active_query_=-1;
    std::array<std::vector<double>,2> timings_;
};
} // namespace apricot
