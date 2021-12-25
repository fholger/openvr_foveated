#pragma once
#include <fstream>
#include "json/json.h"

std::ostream& Log();
std::string GetDllPath();

struct Config {
	bool ffrEnabled = false;
	bool preferVrs = true;
	float innerRadius = 0.3f;
	float midRadius = 0.75f;
	float outerRadius = 0.85f;
	bool debugMode = false;
	bool useSharpening = false;
	float sharpness = 0.4f;
	float sharpenRadius = 0.5f;
	bool hotkeysEnabled = true;

	static Config Load() {
		Config config;
		try {
			std::ifstream configFile (GetDllPath() + "\\openvr_mod.cfg");
			if (configFile.is_open()) {
				Json::Value root;
				configFile >> root;
				Json::Value foveated = root.get("foveated", Json::Value());
				config.ffrEnabled = foveated.get("enabled", false).asBool();
				config.preferVrs = foveated.get("preferVariableRateShading", true).asBool();
				config.innerRadius = foveated.get("innerRadius", 0.3f).asFloat();
				config.midRadius = foveated.get("midRadius", 0.75f).asFloat();
				config.outerRadius = foveated.get("outerRadius", 0.85f).asFloat();
				config.debugMode = foveated.get("debugMode", false).asBool();
				config.hotkeysEnabled = foveated.get("enableHotkeys", true).asBool();

				Json::Value sharpen = foveated.get("sharpen", Json::Value());
				config.useSharpening = sharpen.get("enabled", false).asBool();
				config.sharpness = sharpen.get("sharpness", 0.4).asFloat();
				if (config.sharpness < 0) config.sharpness = 0;
				if (config.sharpness > 1) config.sharpness = 1;
				config.sharpenRadius = sharpen.get("radius", 0.5).asFloat();
			}
		} catch (...) {
			Log() << "Could not read config file.\n";
		}
		return config;
	}

	static Config& Instance() {
		static Config instance = Load();
		return instance;
	}
};
