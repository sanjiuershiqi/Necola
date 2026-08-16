#pragma once
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <windows.h>
#include "../../libs/json.hpp"

namespace NecolaConfig {
	constexpr const char* CONFIG_PATH = "necola\\FeatureConfig.json";
	constexpr const char* CONFIG_DIR  = "necola";
	constexpr const char* CONFIG_TEMP_PATH = "necola\\FeatureConfig.json.tmp";

	inline bool& LastLoadFailed() {
		static bool failed = false;
		return failed;
	}

	inline nlohmann::json& EnsureSectionObject(nlohmann::json& doc, const char* section) {
		if (!doc.is_object()) doc = nlohmann::json::object();
		auto& value = doc[section];
		if (!value.is_object()) value = nlohmann::json::object();
		return value;
	}

	inline nlohmann::json LoadConfig() {
		try {
			std::ifstream file(CONFIG_PATH);
			if (!file.is_open()) {
				LastLoadFailed() = false;
				return nlohmann::json{};
			}
			nlohmann::json doc;
			file >> doc;
			LastLoadFailed() = false;
			return doc;
		} catch (...) {
			LastLoadFailed() = true;
			return nlohmann::json{};
		}
	}

	inline void SaveConfig(const nlohmann::json& doc) {
		if (LastLoadFailed()) return;
		try {
			std::filesystem::create_directories(CONFIG_DIR);
			std::ofstream outFile(CONFIG_TEMP_PATH, std::ios::trunc);
			if (outFile.is_open()) {
				outFile << std::setw(4) << doc;
				outFile.flush();
				if (!outFile.good()) {
					outFile.close();
					std::filesystem::remove(CONFIG_TEMP_PATH);
					return;
				}
				outFile.close();
				if (!MoveFileExA(CONFIG_TEMP_PATH, CONFIG_PATH,
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
					std::filesystem::remove(CONFIG_TEMP_PATH);
				}
			}
		} catch (...) {}
	}
}
