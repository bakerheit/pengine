#pragma once

// Shared deterministic shot and actor sampling for the game and editor.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace apricot::cutscene {
struct View {
    glm::vec3 eye{0, 15, 0}, target{0, 14, -1};
    float fov = 50;
};
struct Shot {
    std::string name = "New shot";
    float duration = 5;
    View from, to;
    bool smooth = true;
};
struct ActorKey {
    float time=0;
    glm::vec3 position{};
    float yaw=0, reach=0;
    glm::vec3 left_hand{}, right_hand{};
};
enum class GestureKind { Explain, Dismiss, Shrug, Self, Nod, Glance };
struct Gesture {
    float start=0,duration=2,strength=.7f;
    GestureKind kind=GestureKind::Explain;
};
inline float gesture_weight(const Gesture& gesture,float time) {
    if (time<=gesture.start||time>=gesture.start+gesture.duration) return 0;
    const float phase=(time-gesture.start)/gesture.duration;
    const float attack=std::clamp(phase/.25f,0.f,1.f);
    const float release=std::clamp((1-phase)/.35f,0.f,1.f);
    const auto ease=[](float t){return t*t*(3-2*t);};
    return gesture.strength*ease(attack)*ease(release);
}
struct Actor {
    std::string name = "Actor", mesh, texture, animation;
    glm::vec3 from{}, to{};
    float yaw_from = 0, yaw_to = 0, height = 1.76f;
    float start = 0, end = 5;
    std::string walk_animation;
    std::vector<ActorKey> keys;
    std::vector<Gesture> gestures;
};
struct Cue {
    std::string speaker, text, audio;
    float start = 0, duration = 3;
};
struct Document {
    std::string title = "Halloway - Opening study";
    float daylight = .46f;
    std::vector<Shot> shots;
    std::vector<Actor> actors;
    std::vector<Cue> cues;
    float duration() const {
        float result = 0;
        for (const auto& shot : shots) result += shot.duration;
        return result;
    }
    float shot_start(std::size_t index) const {
        float result = 0;
        for (std::size_t i = 0; i < std::min(index, shots.size()); ++i)
            result += shots[i].duration;
        return result;
    }
};
inline float fraction(float time, float start, float end) {
    return end > start ? std::clamp((time-start)/(end-start), 0.f, 1.f)
                       : (time >= start ? 1.f : 0.f);
}
inline std::size_t shot_at(const Document& doc, float time) {
    float end = 0;
    for (std::size_t i=0; i<doc.shots.size(); ++i) {
        end += doc.shots[i].duration;
        if (time < end) return i;
    }
    return doc.shots.empty() ? 0 : doc.shots.size()-1;
}
inline View sample(const Document& doc, float time) {
    if (doc.shots.empty()) return {};
    const auto index=shot_at(doc,time);
    const auto& shot=doc.shots[index];
    float t=fraction(time,doc.shot_start(index),doc.shot_start(index)+shot.duration);
    if (shot.smooth) t=t*t*(3-2*t);
    return {glm::mix(shot.from.eye,shot.to.eye,t),
            glm::mix(shot.from.target,shot.to.target,t),
            glm::mix(shot.from.fov,shot.to.fov,t)};
}
inline ActorKey actor_key(const Actor& actor,float time) {
    if (actor.keys.empty()) return {};
    if (time<=actor.keys.front().time) return actor.keys.front();
    for (std::size_t i=1;i<actor.keys.size();++i) {
        const auto& a=actor.keys[i-1];const auto& b=actor.keys[i];
        if (time>b.time) continue;
        const float t=fraction(time,a.time,b.time);
        return {time,glm::mix(a.position,b.position,t),
            a.yaw+std::remainder(b.yaw-a.yaw,360.f)*t,glm::mix(a.reach,b.reach,t),
            glm::mix(a.left_hand,b.left_hand,t),glm::mix(a.right_hand,b.right_hand,t)};
    }
    return actor.keys.back();
}
inline float actor_walk_weight(const Actor& actor,float time) {
    for (std::size_t i=1;i<actor.keys.size();++i) {
        const auto& a=actor.keys[i-1];const auto& b=actor.keys[i];
        if (time<a.time||time>=b.time) continue;
        if (glm::length(b.position-a.position)<.05f) return 0;
        return std::min({1.f,(time-a.time)/.18f,(b.time-time)/.18f});
    }
    return 0;
}
inline glm::vec3 actor_position(const Actor& actor,float time) {
    if (!actor.keys.empty()) return actor_key(actor,time).position;
    return glm::mix(actor.from,actor.to,fraction(time,actor.start,actor.end));
}
inline float actor_yaw(const Actor& actor,float time) {
    if (!actor.keys.empty()) return actor_key(actor,time).yaw;
    return actor.yaw_from+std::remainder(actor.yaw_to-actor.yaw_from,360.f)*
           fraction(time,actor.start,actor.end);
}
inline bool finite(glm::vec3 value) {
    return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
}
inline bool valid_view(const View& v) {
    return finite(v.eye)&&finite(v.target)&&std::isfinite(v.fov)&&v.fov>=10&&v.fov<=110&&
           glm::length(v.eye-v.target)>.01f;
}
inline bool validate(const Document& doc,std::string& error) {
    if (doc.shots.empty()||doc.shots.size()>256||doc.actors.size()>64||doc.cues.size()>512) {
        error="Need 1-256 shots, at most 64 actors and 512 dialogue cues.";return false;
    }
    if (!std::isfinite(doc.daylight)||doc.daylight<0||doc.daylight>1) {
        error="Time of day must be between 0 and 1.";return false;
    }
    for (const auto& s:doc.shots) {
        if (!std::isfinite(s.duration)||s.duration<.1f||s.duration>600||
            !valid_view(s.from)||!valid_view(s.to)) {
            error="Invalid shot duration, lens or camera direction.";return false;
        }
        // Eye and target must not cross during the interpolated shot.
        const glm::vec3 a=s.from.target-s.from.eye,b=(s.to.target-s.to.eye)-a;
        const float t=glm::dot(b,b)>0?std::clamp(-glm::dot(a,b)/glm::dot(b,b),0.f,1.f):0;
        if (glm::length(a+t*b)<.01f) {error="Camera crosses its look target.";return false;}
    }
    for (const auto& a:doc.actors) {
        if (a.mesh.empty()||!finite(a.from)||!finite(a.to)||!std::isfinite(a.height)||
            a.height<=.01f||a.height>100||!std::isfinite(a.yaw_from)||!std::isfinite(a.yaw_to)||
            !std::isfinite(a.start)||!std::isfinite(a.end)||a.start<0||a.end<a.start) {
            error="Invalid actor asset, transform or movement range.";return false;
        }
        if (a.keys.size()>2048) {error="Too many actor keyframes.";return false;}
        float previous=-1;
        for (const auto& key:a.keys) {
            if (!std::isfinite(key.time)||key.time<0||key.time<=previous||
                !finite(key.position)||!std::isfinite(key.yaw)||!std::isfinite(key.reach)||
                key.reach<0||key.reach>1||!finite(key.left_hand)||!finite(key.right_hand)) {
                error="Actor keyframes need increasing times and valid hand targets.";return false;
            }
            previous=key.time;
        }
        if (a.gestures.size()>512) {error="Too many gestures.";return false;}
        for (const auto& gesture:a.gestures) {
            const int kind=static_cast<int>(gesture.kind);
            if (!std::isfinite(gesture.start)||gesture.start<0||!std::isfinite(gesture.duration)||
                gesture.duration<.2f||gesture.duration>60||!std::isfinite(gesture.strength)||
                gesture.strength<0||gesture.strength>1||kind<0||kind>5) {
                error="Invalid gesture timing, strength or style.";return false;
            }
        }
    }
    for (const auto& c:doc.cues) {
        if (!std::isfinite(c.start)||!std::isfinite(c.duration)||c.start<0||c.duration<=0) {
            error="Invalid dialogue timing.";return false;
        }
    }
    error.clear();return true;
}
inline void write_vec(std::ostream& out,glm::vec3 v) {out<<v.x<<' '<<v.y<<' '<<v.z<<' ';}
inline void read_vec(std::istream& in,glm::vec3& v) {in>>v.x>>v.y>>v.z;}
inline void write_view(std::ostream& out,const View& v) {
    write_vec(out,v.eye);write_vec(out,v.target);out<<v.fov<<' ';
}
inline void read_view(std::istream& in,View& v) {read_vec(in,v.eye);read_vec(in,v.target);in>>v.fov;}
inline std::string encode(const Document& doc) {
    std::ostringstream out;out<<std::setprecision(9)<<"APRICOT_CUTSCENE 3\n";
    out<<std::quoted(doc.title)<<' '<<doc.daylight<<'\n';
    out<<doc.shots.size()<<' '<<doc.actors.size()<<' '<<doc.cues.size()<<'\n';
    for (const auto& s:doc.shots) {
        out<<std::quoted(s.name)<<' '<<s.duration<<' '<<s.smooth<<' ';
        write_view(out,s.from);write_view(out,s.to);out<<'\n';
    }
    for (const auto& a:doc.actors) {
        out<<std::quoted(a.name)<<' '<<std::quoted(a.mesh)<<' '<<std::quoted(a.texture)<<' '
           <<std::quoted(a.animation)<<' ';
        write_vec(out,a.from);write_vec(out,a.to);
        out<<a.yaw_from<<' '<<a.yaw_to<<' '<<a.height<<' '<<a.start<<' '<<a.end<<' '
           <<std::quoted(a.walk_animation)<<' '<<a.keys.size()<<' '<<a.gestures.size()<<'\n';
        for (const auto& key:a.keys) {
            out<<key.time<<' ';write_vec(out,key.position);out<<key.yaw<<' '<<key.reach<<' ';
            write_vec(out,key.left_hand);write_vec(out,key.right_hand);out<<'\n';
        }
        for (const auto& gesture:a.gestures)
            out<<gesture.start<<' '<<gesture.duration<<' '<<gesture.strength<<' '
               <<static_cast<int>(gesture.kind)<<'\n';
    }
    for (const auto& c:doc.cues)
        out<<std::quoted(c.speaker)<<' '<<std::quoted(c.text)<<' '<<std::quoted(c.audio)<<' '
           <<c.start<<' '<<c.duration<<'\n';
    return out.str();
}
inline bool decode(const std::string& input,Document& out,std::string& error) {
    std::istringstream in(input);Document draft;std::string magic;int version=0;
    std::size_t shots=0,actors=0,cues=0;
    in>>magic>>version>>std::quoted(draft.title)>>draft.daylight>>shots>>actors>>cues;
    if (!in||magic!="APRICOT_CUTSCENE"||version<1||version>3||shots<1||shots>256||actors>64||cues>512) {
        error="Unsupported or invalid cutscene document.";return false;
    }
    draft.shots.resize(shots);draft.actors.resize(actors);draft.cues.resize(cues);
    for (auto& s:draft.shots) {
        in>>std::quoted(s.name)>>s.duration>>s.smooth;read_view(in,s.from);read_view(in,s.to);
    }
    for (auto& a:draft.actors) {
        in>>std::quoted(a.name)>>std::quoted(a.mesh)>>std::quoted(a.texture)>>std::quoted(a.animation);
        read_vec(in,a.from);read_vec(in,a.to);
        in>>a.yaw_from>>a.yaw_to>>a.height>>a.start>>a.end;
        if (version>=2) {
            std::size_t count=0;in>>std::quoted(a.walk_animation)>>count;
            if (!in||count>2048) {error="Invalid actor keyframe count.";return false;}
            std::size_t gestures=0;
            if (version>=3) in>>gestures;
            if (!in||gestures>512) {error="Invalid gesture count.";return false;}
            a.keys.resize(count);
            for (auto& key:a.keys) {
                in>>key.time;read_vec(in,key.position);in>>key.yaw>>key.reach;
                read_vec(in,key.left_hand);read_vec(in,key.right_hand);
            }
            a.gestures.resize(gestures);
            for (auto& gesture:a.gestures) {
                int kind=0;in>>gesture.start>>gesture.duration>>gesture.strength>>kind;
                gesture.kind=static_cast<GestureKind>(kind);
            }
        }
    }
    for (auto& c:draft.cues)
        in>>std::quoted(c.speaker)>>std::quoted(c.text)>>std::quoted(c.audio)>>c.start>>c.duration;
    if (!in) {error="Incomplete cutscene document; current scene kept.";return false;}
    in>>std::ws;
    if (!in.eof()) {error="Unexpected content after cutscene.";return false;}
    if (!validate(draft,error)) return false;
    out=std::move(draft);return true;
}
inline bool load(const std::filesystem::path& path,Document& out,std::string& error) {
    std::ifstream in(path,std::ios::binary);
    if (!in) {error="Cannot open cutscene.";return false;}
    std::ostringstream text;text<<in.rdbuf();return decode(text.str(),out,error);
}
inline bool save(const std::filesystem::path& path,const Document& doc,std::string& error) {
    if (!validate(doc,error)) return false;
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(),ec);
    if (ec) {error="Cannot create project folder.";return false;}
    auto temporary=path;temporary+=".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<encode(doc);out.close();
     if (!out) {error="Could not write cutscene; previous save kept.";return false;}}
    std::filesystem::rename(temporary,path,ec);
    if (ec) {error="Could not replace cutscene save; previous save kept.";return false;}
    error.clear();return true;
}
} // namespace apricot::cutscene
