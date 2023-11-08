#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <vector>

struct Game {
  std::string game_id;
};

struct Mod : Game {
  std::string mod_id;
  std::string mod_name;

  bool operator==(const Mod& other) const {
    return mod_name == other.mod_name && mod_id == other.mod_id &&
           game_id == other.game_id;
  }
};

struct Config {
  std::string last_monitored_id;
  std::vector<Mod> mods;
};

void to_json(nlohmann::json& j, const Config& c);

void to_json(nlohmann::json& j, const Mod& m);

void from_json(const nlohmann::json& j, Config& c);

void from_json(const nlohmann::json& j, Mod& m);

void save_config(const Config& config);

Config load_config();

#endif  // CONFIG_HPP