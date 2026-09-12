#pragma once

#include <array>
#include <cstddef>

#include <glad/gl.h>

namespace apricot {

// How long the GPU actually spent on a frame, measured with GL_TIME_ELAPSED
// and WITHOUT ever stalling the pipeline.
//
// That last part is the whole design. The obvious implementation — begin a
// query, end it, read the result — makes the CPU wait for the GPU to drain,
// which is a pipeline stall on every frame. It does not merely cost time, it
// DESTROYS THE THING BEING MEASURED: a stalled frame is serialised, so the
// number you read back describes a frame shape that never happens in normal
// play. TiledLighting::begin_timing() takes exactly that approach on purpose
// and says so, because it runs only under --lighting-benchmark where a frozen
// camera and a serialised frame are the point. This class is the opposite
// case: always on, during real play, where perturbing the frame is the one
// unacceptable outcome.
//
// So: a ring of queries. Each frame takes whichever one is idle, and results
// are collected only once the driver says they are ready. The consequence to
// keep in mind when reading the number — and the reason it is spelled out on
// last_ms() rather than buried here — is that the reading LAGS the frame it
// came from by a frame or two. Over a session that is invisible; on a single
// hitch it means the GPU cost printed beside a spike may belong to its
// neighbour.
class GpuTimer {
public:
    GpuTimer() = default;
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    // Start timing. Silently does nothing when every query is still in flight,
    // which is the correct response: skipping a sample costs one row of
    // diagnostics, and waiting costs the frame.
    void begin() {
        if (active_ >= 0) return;  // unbalanced begin; ignore rather than leak
        collect();
        for (std::size_t i = 0; i < queries_.size(); ++i) {
            if (queries_[i].pending) continue;
            if (!queries_[i].id) glGenQueries(1, &queries_[i].id);
            if (!queries_[i].id) return;
            active_ = static_cast<int>(i);
            glBeginQuery(GL_TIME_ELAPSED, queries_[i].id);
            return;
        }
        // Every query in flight. Never stall waiting for one.
    }

    void end() {
        if (active_ < 0) return;
        glEndQuery(GL_TIME_ELAPSED);
        queries_[static_cast<std::size_t>(active_)].pending = true;
        active_ = -1;
    }

    // The most recent COMPLETED GPU time, in milliseconds. Lags the current
    // frame by one to three frames — see the class comment. Zero until the
    // first result lands, and zero forever on a driver with no timer queries,
    // which is a real possibility and not worth failing a session over.
    double last_ms() const { return last_ms_; }

    // True once any result has ever come back, so a reader can tell "the GPU
    // took no time" apart from "this driver does not do timer queries".
    bool valid() const { return valid_; }

    void destroy() {
        for (auto& q : queries_) {
            if (q.id) glDeleteQueries(1, &q.id);
            q = {};
        }
        active_ = -1;
        last_ms_ = 0.0;
        valid_ = false;
    }

private:
    // Harvest whatever the driver has finished. Asks with
    // GL_QUERY_RESULT_AVAILABLE and believes the answer; it never asks for a
    // result that is not ready, because THAT is the call that blocks.
    void collect() {
        for (auto& q : queries_) {
            if (!q.pending || !q.id) continue;
            GLint ready = 0;
            glGetQueryObjectiv(q.id, GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!ready) continue;
            GLuint64 ns = 0;
            glGetQueryObjectui64v(q.id, GL_QUERY_RESULT, &ns);
            last_ms_ = static_cast<double>(ns) / 1.0e6;
            valid_ = true;
            q.pending = false;
        }
    }

    struct Query {
        GLuint id = 0;
        bool pending = false;
    };

    // Four is enough to cover the deepest queueing a driver does here without
    // the ring ever emptying in normal play.
    std::array<Query, 4> queries_{};
    int active_ = -1;
    double last_ms_ = 0.0;
    bool valid_ = false;
};

}  // namespace apricot
