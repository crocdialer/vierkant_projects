#include "pbr_viewer_serialization.hpp"

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

}// namespace pbr_viewer
