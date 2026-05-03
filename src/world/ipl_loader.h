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

bool load_ipl(const std::filesystem::path& path,
              std::vector<InstanceDef>& out_instances);

bool save_ipl(const std::filesystem::path& path,
              const std::vector<InstanceDef>& instances);

// Default on-disk path for a cell IPL beneath a base directory.
//   <base>/cell_<x>_<z>.ipl
std::filesystem::path ipl_path_for_cell(const std::filesystem::path& base,
                                         CellCoord coord);

}  // namespace pengine
