#pragma once

// MissionDef: the mission creator's shared data contract.
//
// PURE DATA, and that is a requirement rather than an observation. This header
// is the meeting point of three things built in parallel — the actor command
// layer, the objective runtime, the authoring UI — so it must stay free of
// engine dependencies: std and glm, nothing else. Runtime binding (turning a
// MissionDef into spawned actors and live objectives) belongs to the mission
// player, which is not in this tree yet.
//
// NOT LIFTED (PENG-29): the text `.mis` serialiser. It came with a file
// grammar, a loader and disk paths; a data model that outlives its file format
// is worth more than one that drags it along.
//
// Conventions:
//   - Positions are world-space metres.
//   - Headings are degrees, the yaw convention the editor and play mode use.
//   - `model` on an actor is a SYMBOLIC ped-model identifier (e.g. "civ_a").
//     Mapping it to a concrete model index is the runtime's job, deliberately:
//     an authored mission that stored an index would break the day somebody
//     inserted a model.
//   - Actor names are unique per mission and identifier-safe ([A-Za-z0-9_.-]);
//     objective `target` and command `actor` fields refer to them by name. A
//     loader validates the references; an editor enforces the charset at input.

#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace apricot {

// A named, persistent mission ped (spawned at mission start, lives until the
// mission ends or a command/objective removes it).
struct MissionActorDef {
    std::string name;          // unique per mission, identifier-safe
    std::string model;         // symbolic ped model id, identifier-safe
    glm::vec3   pos{0.f};      // spawn position, world metres
    float       heading_deg = 0.f;
};

// The v1 actor command vocabulary (each fires a completion event in the
// runtime). Issued when the owning objective becomes active (`on_start`).
enum class MissionCommandType {
    Goto,          // walk to `pos`
    Wait,          // stand for `seconds`
    Anim,          // play `anim`, optionally looped
    AttackPlayer,  // engage the player
    Flee,          // run from the player
};

struct MissionCommandDef {
    MissionCommandType type = MissionCommandType::Wait;
    std::string        actor;             // target actor name (always set)
    glm::vec3          pos{0.f};          // Goto
    float              seconds = 0.f;     // Wait
    std::string        anim;              // Anim: clip name, identifier-safe
    bool               anim_loop = false; // Anim
};

// Objective kinds (ordered list, exactly one active at a time, DYOM-style).
enum class MissionObjectiveType {
    Reach,     // enter a sphere trigger at `pos` / `radius`
    Kill,      // named actor `target` dies
    Approach,  // get within `radius` of named actor `target`
};

struct MissionObjectiveDef {
    MissionObjectiveType type = MissionObjectiveType::Reach;
    std::string text;            // on-screen display text (may be empty)
    glm::vec3   pos{0.f};        // Reach: trigger centre
    float       radius = 0.f;    // Reach / Approach: trigger radius, > 0
    std::string target;          // Kill / Approach: actor name
    float       timelimit_sec = 0.f;  // > 0 = fail when it expires; 0 = none
    std::vector<MissionCommandDef> on_start;  // fired when this objective starts
};

struct MissionDef {
    std::string slug;    // filename-safe mission id (identifier charset)
    std::string title;   // display title (free text)
    int         version = 1;  // .mis format version this def was loaded as

    glm::vec3 player_start{0.f};
    float     player_heading_deg = 0.f;

    std::vector<MissionActorDef>    actors;      // may be empty (WIP saves)
    std::vector<MissionObjectiveDef> objectives; // ordered; may be empty (WIP)
};

}  // namespace apricot
