#pragma once
#include <fstream>
#include "PostProcessor.h"
#include "json/json.h"

std::ostream& Log();
std::wstring GetDllPath();

struct Config {
	bool ffrEnabled = false;
	bool useVrs = false;
	float innerRadius = 0.5f;
	float midRadius = 0.8f;
	float outerRadius = 1.0f;
	bool debugMode = false;
	bool useSharpening = false;
	float sharpness = 0.4f;
	float sharpenRadius = 0.5f;
	bool hotkeysEnabled = true;
	bool hotkeysRequireCtrl = false;
	bool hotkeysRequireAlt = false;
	bool hotkeysRequireShift = false;
	int hotkeyToggleFfr = VK_F1;
	int hotkeyToggleDebugMode = VK_F2;
	int hotkeyDecreaseSharpness = VK_F3;
	int hotkeyIncreaseSharpness = VK_F4;
	int hotkeyDecreaseRadius = VK_F5;
	int hotkeyIncreaseRadius = VK_F6;
	int hotkeyCaptureOutput = VK_F7;
	int hotkeyToggleUseVrs = VK_F8;
	int hotkeySelectInnerRadius = '1';
	int hotkeySelectMidRadius = '2';
	int hotkeySelectOuterRadius = '3';
	int hotkeySelectSharpenRadius = '4';

	static Config Load() {
		Config config;
		try {
			std::ifstream configFile (GetDllPath() + L"\\openvr_mod.cfg");
			if (configFile.is_open()) {
				Json::Value root;
				configFile >> root;
				Json::Value foveated = root.get("foveated", Json::Value());
				config.ffrEnabled = foveated.get("enabled", false).asBool();
				config.useVrs = foveated.get("useVariableRateShading", false).asBool();
				config.innerRadius = foveated.get("innerRadius", 0.6f).asFloat();
				config.midRadius = foveated.get("midRadius", 0.8f).asFloat();
				config.outerRadius = foveated.get("outerRadius", 1.0f).asFloat();
				config.debugMode = foveated.get("debugMode", false).asBool();
				Json::Value hotkeys = foveated.get("hotkeys", Json::Value());
				config.hotkeysEnabled = hotkeys.get("enabled", true).asBool();
				config.hotkeysRequireCtrl = hotkeys.get("requireCtrl", false).asBool();
				config.hotkeysRequireAlt = hotkeys.get("requireAlt", false).asBool();
				config.hotkeysRequireShift = hotkeys.get("requireShift", false).asBool();
				config.hotkeyToggleFfr = hotkeys.get("toggleFFR", VK_F1).asInt();
				config.hotkeyToggleDebugMode = hotkeys.get("toggleDebugMode", VK_F2).asInt();
				config.hotkeyDecreaseSharpness = hotkeys.get("decreaseSharpness", VK_F3).asInt();
				config.hotkeyIncreaseSharpness = hotkeys.get("increaseSharpness", VK_F4).asInt();
				config.hotkeyDecreaseRadius = hotkeys.get("decreaseRadius", VK_F5).asInt();
				config.hotkeyIncreaseRadius = hotkeys.get("increaseRadius", VK_F6).asInt();
				config.hotkeyCaptureOutput = hotkeys.get("captureOutput", VK_F7).asInt();
				config.hotkeyToggleUseVrs = hotkeys.get("toggleUseVRS", VK_F8).asInt();
				config.hotkeySelectInnerRadius = hotkeys.get("selectInnerRadius", '1').asInt();
				config.hotkeySelectMidRadius = hotkeys.get("selectMidRadius", '2').asInt();
				config.hotkeySelectOuterRadius = hotkeys.get("selectOuterRadius", '3').asInt();
				config.hotkeySelectSharpenRadius = hotkeys.get("selectSharpenRadius", '4').asInt();

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
