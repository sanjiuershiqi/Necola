#pragma once
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <windows.h>
#include "../../libs/json.hpp"

namespace NecolaConfig {
	inline std::filesystem::path GameRoot() {
		char exePath[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exePath, MAX_PATH);
		std::filesystem::path path(exePath);
		return path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
	}

	inline std::filesystem::path ConfigPath() { return GameRoot() / "necola" / "FeatureConfig.json"; }
	inline std::filesystem::path ConfigTempPath() { return GameRoot() / "necola" / "FeatureConfig.json.tmp"; }

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
			std::ifstream file(ConfigPath());
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

	inline bool SaveConfig(const nlohmann::json& doc) {
		if (LastLoadFailed()) return false;
		try {
			const auto path = ConfigPath();
			const auto tempPath = ConfigTempPath();
			std::filesystem::create_directories(path.parent_path());
			std::ofstream outFile(tempPath, std::ios::trunc);
			if (outFile.is_open()) {
				outFile << std::setw(4) << doc;
				outFile.flush();
				if (!outFile.good()) {
					outFile.close();
					std::filesystem::remove(tempPath);
					return false;
				}
				outFile.close();
				if (!MoveFileExA(tempPath.string().c_str(), path.string().c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
					std::filesystem::remove(tempPath);
					return false;
				}
				return true;
			}
		} catch (...) { return false; }
		return false;
	}
}
