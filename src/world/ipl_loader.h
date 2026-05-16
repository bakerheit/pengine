#pragma once

#include <filesystem>
#include <vector>

#include "world/cell_coord.h"
#include "world/instance_def.h"

namespace pengine {

// Text IPL format (whitespace-delimited, '#' comments, blank lines OK):
//
//   # model  px py pz   qx qy qz qw   sx sy sz   [uv=U,V] [lod=N]
//   20       64 -0.05 0   0 0 0 1     8 0.1 256
//   11       64 12.5  32  0 0 0 1     20 25 18  uv=6.67,8.33
//
// 11 numeric tokens (model_id + position(3) + quat(4) + scale(3)) followed by
// optional key=value pairs. Coordinates are world-space metres.
//
// PBD-053: the first comment line after the header may carry a provenance
// marker `# source=authored` or `# source=generated`. `authored` means a
// user edited this cell via the Map Builder (source-of-truth, write-back
// on evict). `generated` means `generate_city_cell` produced it
// procedurally (regenerable on demand; we do NOT write these to disk in
// the production streamer flow). Files without a `source=` line load as
// `Generated` for back-compat with any pre-PBD-053 on-disk cells.

enum class IplProvenance {
    Generated,
    Authored,
};

// Reads the IPL at `path`. Optionally writes the parsed provenance to
// `*out_provenance` (defaults to `Generated` if the header is absent).
bool load_ipl(const std::filesystem::path& path,
              std::vector<InstanceDef>& out_instances,
              IplProvenance* out_provenance = nullptr);

// Writes the IPL at `path`. `provenance` is stamped into the header so the
// next load can distinguish authored cells (keep on disk) from procedurally
// generated ones (regenerable). Defaults to `Authored` because the
// streamer's two write paths — `save_dirty_cell` and
// `save_all_dirty_cells` — both serve the Map Builder's edit flow, and
// the test round-trip helpers exercise that same path.
bool save_ipl(const std::filesystem::path& path,
              const std::vector<InstanceDef>& instances,
              IplProvenance provenance = IplProvenance::Authored);

// Default on-disk path for a cell IPL beneath a base directory.
//   <base>/cell_<x>_<z>.ipl
std::filesystem::path ipl_path_for_cell(const std::filesystem::path& base,
                                         CellCoord coord);

}  // namespace pengine
