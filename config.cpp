#include "config.hpp"

void to_json(nlohmann::json& j, const Config& c) {
  j = nlohmann::json{{"last_monitored_id", c.last_monitored_id},
                     {"mods", c.mods}};
}

void to_json(nlohmann::json& j, const Mod& m) {
  j = nlohmann::json{
      {"game_id", m.game_id}, {"mod_id", m.mod_id}, {"mod_name", m.mod_name}};
}

void from_json(const nlohmann::json& j, Config& c) {
  j.at("last_monitored_id").get_to(c.last_monitored_id);
  j.at("mods").get_to(c.mods);
}

void from_json(const nlohmann::json& j, Mod& m) {
  j.at("game_id").get_to(m.game_id);
  j.at("mod_id").get_to(m.mod_id);
  j.at("mod_name").get_to(m.mod_name);
}

void save_config(const Config& config) {
  std::ofstream file("config.json");
  nlohmann::json j = config;
  file << j;
}

Config load_config() {
  std::ifstream file("config.json");
  nlohmann::json j;
  file >> j;
  return j.get<Config>();
}