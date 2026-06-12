#pragma once

#include <iosfwd>
#include <optional>

#include "pbr_viewer.hpp"

namespace pbr_viewer
{

void save_settings(std::ostream &os, const PBRViewer::settings_t &settings);
std::optional<PBRViewer::settings_t> load_settings(std::istream &is);

}// namespace pbr_viewer
