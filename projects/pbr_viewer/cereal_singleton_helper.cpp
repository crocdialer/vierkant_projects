#include "ziparchive.h"
#include <fstream>
#include <crocore/filesystem.hpp>

#include "pbr_viewer.hpp"

// template class cereal::detail::StaticObject<cereal::detail::Versions>;

// #include <cereal/types/polymorphic.hpp>
// CEREAL_REGISTER_POLYMORPHIC_RELATION(vierkant::CameraControl, vierkant::OrbitCamera)
// CEREAL_REGISTER_POLYMORPHIC_RELATION(vierkant::CameraControl, vierkant::FlyCamera)

// namespace vierkant
// {
// template<class Archive>
// void serialize(Archive &archive, vierkant::CameraControl &camera_control)
// {
//     archive(cereal::make_nvp("enabled", camera_control.enabled));
//     archive(cereal::make_nvp("mouse_sensitivity", camera_control.mouse_sensitivity));
// }

// template<class Archive>
// void serialize(Archive &archive, vierkant::FlyCamera &fly_camera)
// {
//     archive(cereal::base_class<vierkant::CameraControl>(&fly_camera), cereal::make_nvp("position", fly_camera.position),
//             cereal::make_nvp("spherical_coords", fly_camera.spherical_coords),
//             cereal::make_nvp("move_speed", fly_camera.move_speed));
// }

// template<class Archive>
// void serialize(Archive &archive, vierkant::OrbitCamera &orbit_camera)
// {
//     archive(cereal::base_class<vierkant::CameraControl>(&orbit_camera),
//             cereal::make_nvp("spherical_coords", orbit_camera.spherical_coords),
//             cereal::make_nvp("distance", orbit_camera.distance), cereal::make_nvp("look_at", orbit_camera.look_at));
// }
// }// namespace vierkant

// #define CEREAL_NO_RTTI_POLYMORPHISM
// #define CEREAL_DLL_EXPORT
// #include "serialization.hpp"

// // #include <cereal/types/memory.hpp>

// // namespace vierkant::nodes
// // {
// // template<class Archive>
// // void serialize(Archive &archive, vierkant::nodes::node_t &n)
// // {
// //     archive(cereal::make_nvp("name", n.name),
// //             cereal::make_nvp("transform", n.transform),
// //             cereal::make_nvp("offset", n.offset),
// //             cereal::make_nvp("index", n.index),
// //             cereal::make_nvp("parent", n.parent),
// //             cereal::make_nvp("children", n.children));
// // }

// // }// namespace vierkant::nodes

// namespace vierkant::model
// {

// template<class Archive>
// void serialize(Archive &archive, vierkant::model::model_assets_t &mesh_assets)
// {
//     // archive(
//     //         //cereal::make_nvp("geometry_data", mesh_assets.geometry_data),
//     //         cereal::make_nvp("materials", mesh_assets.materials));//,
//     //         //ereal::make_nvp("textures", mesh_assets.textures),
//     //         // cereal::make_nvp("texture_samplers", mesh_assets.texture_samplers),
//     //         //            cereal::make_nvp("lights", mesh_assets.lights),
//     //         //            cereal::make_nvp("cameras", mesh_assets.cameras),
           
//     //        // cereal::make_nvp("root_node", mesh_assets.root_node), cereal::make_nvp("root_bone", mesh_assets.root_bone),
//     //        // cereal::make_nvp("node_animations", mesh_assets.node_animations));
// }

// }// namespace vierkant::model

// template<class Archive>
// void serialize(Archive &ar, PBRViewer::settings_t &settings)
// {
//     ar(cereal::make_nvp("use_validation", settings.use_validation),
//        cereal::make_nvp("use_debug_labels", settings.use_debug_labels),
//        cereal::make_nvp("log_level", settings.log_level), cereal::make_nvp("log_file", settings.log_file),
//        cereal::make_nvp("recent_files", settings.recent_files), cereal::make_nvp("window", settings.window_info),
//        cereal::make_nvp("pbr_settings", settings.pbr_settings),
//        cereal::make_nvp("path_tracer_settings", settings.path_tracer_settings),
//        cereal::make_nvp("draw_ui", settings.draw_ui),
//        cereal::make_optional_nvp("ui_draw_view_controls", settings.ui_draw_view_controls),
//        cereal::make_nvp("font_url", settings.font_url), cereal::make_nvp("ui_scale", settings.ui_scale),
//        cereal::make_nvp("ui_font_scale", settings.ui_font_scale), cereal::make_nvp("draw_grid", settings.draw_grid),
//        cereal::make_nvp("draw_aabbs", settings.draw_aabbs), cereal::make_nvp("draw_physics", settings.draw_physics),
//        cereal::make_nvp("draw_node_hierarchy", settings.draw_node_hierarchy),
//        cereal::make_nvp("path_tracing", settings.path_tracing),
//        cereal::make_nvp("texture_compression", settings.texture_compression),
//        cereal::make_nvp("mesh_buffer_params", settings.mesh_buffer_params),
//        cereal::make_nvp("cache_mesh_bundles", settings.cache_mesh_bundles),
//        cereal::make_nvp("cache_zip_archive", settings.cache_zip_archive),
//        cereal::make_nvp("enable_raytracing_pipeline_features", settings.enable_raytracing_pipeline_features),
//        cereal::make_nvp("enable_ray_query_features", settings.enable_ray_query_features),
//        cereal::make_nvp("enable_mesh_shader_device_features", settings.enable_mesh_shader_device_features),
//     //    cereal::make_nvp("orbit_camera", settings.orbit_camera), cereal::make_nvp("fly_camera", settings.fly_camera),
//        cereal::make_nvp("use_fly_camera", settings.use_fly_camera),
//        cereal::make_optional_nvp("ortho_camera", settings.ortho_camera),
//        cereal::make_nvp("current_guizmo", settings.current_guizmo),
//        cereal::make_nvp("object_overlay_mode", settings.object_overlay_mode),
//        cereal::make_nvp("target_fps", settings.target_fps));
// }

// template<class Archive>
// void serialize(Archive &ar, PBRViewer::mesh_state_t &mesh_state)
// {
//     ar(cereal::make_nvp("mesh_id", mesh_state.mesh_id), cereal::make_nvp("mesh_library", mesh_state.mesh_library),
//        cereal::make_nvp("entry_indices", mesh_state.entry_indices));
// }

// template<class Archive>
// void serialize(Archive &ar, PBRViewer::scene_node_t &scene_node)
// {
//     ar(cereal::make_nvp("name", scene_node.name), cereal::make_optional_nvp("enabled", scene_node.enabled, true),
//        cereal::make_nvp("transform", scene_node.transform), cereal::make_optional_nvp("children", scene_node.children),
//        cereal::make_optional_nvp("scene_id", scene_node.scene_id),
//        cereal::make_optional_nvp("mesh_state", scene_node.mesh_state),
//        cereal::make_optional_nvp("animation_state", scene_node.animation_state),
//        cereal::make_optional_nvp("physics_state", scene_node.physics_state));
// }

// template<class Archive>
// void serialize(Archive &ar, PBRViewer::scene_camera_t &camera)
// {
//     ar(cereal::make_nvp("name", camera.name), cereal::make_nvp("transform", camera.transform),
//        cereal::make_nvp("params", camera.params));
// }

// template<class Archive>
// void serialize(Archive &ar, PBRViewer::scene_data_t &scene_data)
// {
//     ar(cereal::make_optional_nvp("name", scene_data.name),
//        cereal::make_optional_nvp("environment_path", scene_data.environment_path),
//        cereal::make_optional_nvp("scene_paths", scene_data.scene_paths),
//        cereal::make_nvp("model_paths", scene_data.model_paths), cereal::make_nvp("nodes", scene_data.nodes),
//        cereal::make_nvp("scene_roots", scene_data.scene_roots), cereal::make_nvp("cameras", scene_data.cameras),
//        cereal::make_optional_nvp("materials", scene_data.materials));
// }

// #include <cereal/archives/binary.hpp>
// #include <cereal/archives/json.hpp>

void PBRViewer::save_settings(PBRViewer::settings_t settings, const std::filesystem::path &path) const
{
    // // window settings
    // vierkant::Window::create_info_t window_info = {};
    // window_info.size = m_window->size();
    // window_info.position = m_window->position();
    // window_info.fullscreen = m_window->fullscreen();
    // window_info.sample_count = m_window->swapchain().sample_count();
    // window_info.title = m_window->title();
    // window_info.vsync = m_window->swapchain().v_sync();
    // window_info.use_hdr = m_window->swapchain().hdr();
    // settings.window_info = window_info;

    // // logger settings
    // settings.log_level = spdlog::get_level();

    // // target-fps
    // settings.target_fps = static_cast<float>(target_loop_frequency);

    // // camera-control settings
    // settings.use_fly_camera = m_camera_control.current == m_camera_control.fly;
    // settings.orbit_camera = m_camera_control.orbit;
    // settings.fly_camera = m_camera_control.fly;

    // // renderer settings
    // settings.pbr_settings = m_pbr_renderer->settings;
    // if(m_path_tracer) { settings.path_tracer_settings = m_path_tracer->settings; }
    // settings.path_tracing = m_scene_renderer == m_path_tracer;

    // // create and open a character archive for output
    // std::ofstream ofs(path.string());

    // // save data to archive
    // try
    // {
    //     cereal::JSONOutputArchive archive(ofs);

    //     // write class instance to archive
    //     archive(settings);

    // } catch(std::exception &e)
    // {
    //     spdlog::error(e.what());
    // }

    spdlog::debug("save settings: {}", path.string());
}

// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109561 -> hitting a GCC 12 bug
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
std::optional<PBRViewer::settings_t> PBRViewer::load_settings(const std::filesystem::path &path)
{
    // // create and open a character archive for input
    // std::ifstream file_stream(path.string());

    // // load data from archive
    // if(file_stream.is_open())
    // {
    //     try
    //     {
    //         cereal::JSONInputArchive archive(file_stream);

    //         // read class instance from archive
    //         PBRViewer::settings_t settings = {};
    //         archive(settings);
    //         return settings;
    //     } catch(std::exception &e)
    //     {
    //         spdlog::error(e.what());
    //     }

    //     spdlog::debug("loading settings: {}", path.string());
    // }
    return {};
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

void PBRViewer::save_asset_bundle(const vierkant::model::model_assets_t &mesh_assets, const std::filesystem::path &path)
{
    // // save data to archive
    // try
    // {
    //     {
    //         spdlog::stopwatch sw;

    //         // create directory, if necessary
    //         std::filesystem::create_directories(crocore::filesystem::get_directory_part(path));

    //         // create and open a character archive for output
    //         std::ofstream ofs(path.string(), std::ios_base::out | std::ios_base::binary);
    //         spdlog::debug("serializing/writing mesh_buffer_bundle: {}", path.string());
    //         cereal::BinaryOutputArchive archive(ofs);
    //         archive(mesh_assets);
    //         spdlog::debug("done serializing/writing mesh_buffer_bundle: {} ({})", path.string(), sw.elapsed());
    //     }

    //     if(m_settings.cache_zip_archive)
    //     {
    //         spdlog::stopwatch sw;
    //         {
    //             std::unique_lock lock(m_bundle_rw_mutex);
    //             spdlog::debug("adding bundle to compressed archive: {} -> {}", path.string(), g_zip_path);
    //             vierkant::ziparchive zipstream(g_zip_path);
    //             zipstream.add_file(path);
    //         }
    //         spdlog::debug("done compressing bundle: {} -> {} ({})", path.string(), g_zip_path, sw.elapsed());
    //         std::filesystem::remove(path);
    //     }
    // } catch(std::exception &e)
    // {
    //     spdlog::error(e.what());
    // }
}

std::optional<vierkant::model::model_assets_t> PBRViewer::load_asset_bundle(const std::filesystem::path &path)
{
    // {
    //     std::ifstream cache_file(path);

    //     if(cache_file.is_open())
    //     {
    //         try
    //         {
    //             spdlog::debug("loading bundle '{}'", path.string());
    //             vierkant::model::model_assets_t ret;

    //             std::shared_lock lock(m_bundle_rw_mutex);
    //             cereal::BinaryInputArchive archive(cache_file);
    //             archive(ret);
    //             return ret;
    //         } catch(std::exception &e)
    //         {
    //             spdlog::error(e.what());
    //         }
    //     }
    // }

    // vierkant::ziparchive zip(g_zip_path);

    // if(zip.has_file(path))
    // {
    //     try
    //     {
    //         //            spdlog::trace("archive '{}': {}", g_zip_path, zip.contents());
    //         spdlog::debug("loading bundle '{}' from archive '{}'", path.string(), g_zip_path);
    //         vierkant::model::model_assets_t ret;

    //         std::shared_lock lock(m_bundle_rw_mutex);
    //         auto zipstream = zip.open_file(path);
    //         cereal::BinaryInputArchive archive(zipstream);
    //         archive(ret);
    //         return ret;
    //     } catch(std::exception &e)
    //     {
    //         spdlog::error(e.what());
    //     }
    // }
    return {};
}

std::optional<PBRViewer::scene_data_t> PBRViewer::load_scene_data(const std::filesystem::path &path)
{
    // // create and open a character archive for input
    // std::ifstream file_stream(path.string());

    // // load data from archive
    // if(file_stream.is_open())
    // {
    //     try
    //     {
    //         cereal::JSONInputArchive archive(file_stream);
    //         PBRViewer::scene_data_t scene_data;
    //         spdlog::debug("loading scene: {}", path.string());
    //         archive(scene_data);
    //         return scene_data;
    //     } catch(std::exception &e)
    //     {
    //         spdlog::warn(e.what());
    //     }
    // }
    return {};
}

void PBRViewer::save_scene_data(const PBRViewer::scene_data_t& data, const std::filesystem::path &path)
{
    // // save scene_data
    // std::ofstream ofs(path.string());

    // // save data to archive
    // try
    // {
    //     cereal::JSONOutputArchive archive(ofs);
    //     archive(data);

    // } catch(std::exception &e)
    // {
    //     spdlog::error(e.what());
    // }
}