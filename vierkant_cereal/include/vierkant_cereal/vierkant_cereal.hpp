#pragma once

#include <iosfwd>
#include <optional>

#include <vierkant/Material.hpp>
#include <vierkant/model/model_loading.hpp>

namespace vierkant_cereal
{

void save(std::ostream &os, const vierkant::model::model_assets_t &assets);
std::optional<vierkant::model::model_assets_t> load_model_assets(std::istream &is);

void save(std::ostream &os, const vierkant::material_data_t &data);
std::optional<vierkant::material_data_t> load_material_data(std::istream &is);

}// namespace vierkant_cereal
