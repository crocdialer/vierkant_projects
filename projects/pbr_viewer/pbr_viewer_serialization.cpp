#include "pbr_viewer_serialization.hpp"
#include "scene_cereal.hpp"

namespace pbr_viewer
{

void save_settings(std::ostream &os, const PBRViewer::settings_t &settings)
{
    cereal::JSONOutputArchive archive(os);
    archive(settings);
}

std::optional<PBRViewer::settings_t> load_settings(std::istream &is)
{
    try
    {
        PBRViewer::settings_t settings = {};
        cereal::JSONInputArchive archive(is);
        archive(settings);
        return settings;
    } catch(const std::exception &) { return {}; }
}

void save_scene_data(std::ostream &os, const scene_data_t &data)
{
    cereal::JSONOutputArchive archive(os);
    archive(data);
}

std::optional<scene_data_t> load_scene_data(std::istream &is)
{
    try
    {
        scene_data_t scene_data;
        cereal::JSONInputArchive archive(is);
        archive(scene_data);
        return scene_data;
    } catch(const std::exception &) { return {}; }
}

}// namespace pbr_viewer
