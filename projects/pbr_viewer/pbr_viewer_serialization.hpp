#pragma once

#include <iosfwd>
#include <optional>

#include "pbr_viewer.hpp"
#include "scene_data.hpp"

namespace pbr_viewer
{

void save_settings(std::ostream &os, const PBRViewer::settings_t &settings);
std::optional<PBRViewer::settings_t> load_settings(std::istream &is);

void save_scene_data(std::ostream &os, const scene_data_t &data);
std::optional<scene_data_t> load_scene_data(std::istream &is);

}// namespace pbr_viewer
