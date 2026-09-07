#include "gfx/tiled_lighting.h"
#include "gfx/gl_state.h"
#include "core/log.h"
#include <chrono>

namespace apricot {
bool TiledLighting::buffer(int slot,GLenum format,std::size_t texels,
                           std::size_t bytes,const void* data) {
    const std::size_t i=static_cast<std::size_t>(texture_base_+slot);
    if(!max_texels_) glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&max_texels_);
    if(texels>static_cast<std::size_t>(max_texels_)) {
        AP_ERROR("traffic lighting buffer capacity exceeded; no lights silently dropped"); return false;
    }
    const bool created=!textures_[i];
    if(created) { glGenTextures(1,&textures_[i]); glGenBuffers(1,&buffers_[i]); }
    gl_state::bind_texture(static_cast<GLuint>(4+slot),textures_[i],GL_TEXTURE_BUFFER);
    gl_state::bind_buffer(GL_TEXTURE_BUFFER,buffers_[i]);
    // Orphan storage rather than waiting for last frame's readers. Ordinary
    // glTexSubImage2D updates serialized the macOS driver during profiling.
    const glm::vec4 zero{0};
    if(bytes==0) { bytes=sizeof(zero); data=&zero; }
    glBufferData(GL_TEXTURE_BUFFER,static_cast<GLsizeiptr>(bytes),nullptr,GL_STREAM_DRAW);
    glBufferSubData(GL_TEXTURE_BUFFER,0,static_cast<GLsizeiptr>(bytes),data);
    if(created) glTexBuffer(GL_TEXTURE_BUFFER,format,buffers_[i]);
    return true;
}

bool TiledLighting::upload(const std::vector<TrafficSpotLight>& lights,
                           const glm::mat4& vp,const glm::mat4& camera_view,
                           int width,int height) {
    if(lights.empty() && textures_[static_cast<std::size_t>(texture_base_)]) {
        view_.columns=0;
        grid_.visible_lights=0; grid_.max_cell_lights=0; grid_.indices.clear();
        build_ms_=0; upload_ms_=0;
        for(int slot=0;slot<3;++slot) gl_state::bind_texture(static_cast<GLuint>(4+slot),
            textures_[static_cast<std::size_t>(texture_base_+slot)],GL_TEXTURE_BUFFER);
        return true;
    }
    const auto start=std::chrono::steady_clock::now();
    grid_.build(lights,vp,width,height);
    const auto built=std::chrono::steady_clock::now();
    build_ms_=std::chrono::duration<double,std::milli>(built-start).count();
    texture_base_=(texture_base_+3)%12;
    view_.columns=grid_.visible_lights ? grid_.columns : 0;
    view_.rows=grid_.rows;
    view_.depth_plane=-glm::vec4{camera_view[0][2],camera_view[1][2],camera_view[2][2],camera_view[3][2]};
    static_assert(sizeof(TrafficSpotLight)==sizeof(glm::vec4)*3);
    const bool okay=buffer(0,GL_RGBA32F,lights.size()*3,lights.size()*sizeof(TrafficSpotLight),lights.data()) &&
        buffer(1,GL_RG32UI,grid_.cells.size(),grid_.cells.size()*sizeof(glm::uvec2),grid_.cells.data()) &&
        buffer(2,GL_R32UI,grid_.indices.size(),grid_.indices.size()*sizeof(uint32_t),grid_.indices.data());
    upload_ms_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-built).count();
    if(!okay) view_.columns=0;
    return okay;
}

void TiledLighting::begin_timing(bool lights_on) {
    // Diagnostic runs only: drain preceding work so Apple's queued contexts
    // do not turn the timing interval into several frames of queue latency.
    // Normal play never calls this method or waits for a timer result.
    glFinish();
    active_query_=-1;
    for(std::size_t i=0;i<queries_.size();++i) {
        auto& q=queries_[i];
        if(q.pending) {
            GLint ready=0; glGetQueryObjectiv(q.id,GL_QUERY_RESULT_AVAILABLE,&ready);
            if(ready) {
                GLuint64 ns=0; glGetQueryObjectui64v(q.id,GL_QUERY_RESULT,&ns);
                timings_[q.on?1u:0u].push_back(static_cast<double>(ns)/1e6);
                q.pending=false;
            }
        }
        if(!q.pending && active_query_<0) active_query_=static_cast<int>(i);
    }
    if(active_query_<0) return; // Never stall waiting for the GPU.
    auto& q=queries_[static_cast<std::size_t>(active_query_)];
    if(!q.id) glGenQueries(1,&q.id);
    q.on=lights_on;
    glBeginQuery(GL_TIME_ELAPSED,q.id);
}
void TiledLighting::end_timing() {
    if(active_query_<0) return;
    glEndQuery(GL_TIME_ELAPSED);
    queries_[static_cast<std::size_t>(active_query_)].pending=true;
    active_query_=-1;
}
void TiledLighting::destroy() {
    for(auto& t:textures_) if(t) { glDeleteTextures(1,&t); gl_state::on_texture_deleted(t); t=0; }
    for(auto& b:buffers_) if(b) { glDeleteBuffers(1,&b); gl_state::on_buffer_deleted(b); b=0; }
    for(auto& q:queries_) if(q.id) { glDeleteQueries(1,&q.id); q={}; }
    view_={}; max_texels_=0; texture_base_=0; active_query_=-1;
    timings_={};
}
} // namespace apricot
