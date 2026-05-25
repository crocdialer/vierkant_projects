#include <vierkant_cereal/vierkant_cereal.hpp>
#include <vierkant_cereal/serialization.hpp>

namespace vierkant_cereal
{

void save(std::ostream &os, const vierkant::model::model_assets_t &assets)
{
    cereal::BinaryOutputArchive archive(os);
    archive(assets);
}

std::optional<vierkant::model::model_assets_t> load_model_assets(std::istream &is)
{
    try
    {
        vierkant::model::model_assets_t ret;
        cereal::BinaryInputArchive archive(is);
        archive(ret);
        return ret;
    } catch(const std::exception &) { return {}; }
}

void save(std::ostream &os, const vierkant::material_data_t &data)
{
    cereal::BinaryOutputArchive archive(os);
    archive(data);
}

std::optional<vierkant::material_data_t> load_material_data(std::istream &is)
{
    try
    {
        vierkant::material_data_t ret;
        cereal::BinaryInputArchive archive(is);
        archive(ret);
        return ret;
    } catch(const std::exception &) { return {}; }
}

}// namespace vierkant_cereal
