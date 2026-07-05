#include "CueGridBuilder.h"
#include "OscQueryJson.h"
#include "ofMain.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

using namespace oscq;

namespace {
	std::vector<std::string> timelineBankNames(const ofJson& root) {
		std::vector<std::string> names;
		auto* timelinesNode = jsonGet(root, {"CONTENTS", "timelines", "CONTENTS"});
		if (!timelinesNode || !timelinesNode->is_object()) return names;
		for (auto it = timelinesNode->begin(); it != timelinesNode->end(); ++it) {
			if (!it.value().is_object()) continue;
			if (it.key() == "editor" || it.key() == "active_bank") continue;

			auto contentsIt = it.value().find("CONTENTS");
			if (contentsIt == it.value().end() || !contentsIt->is_object()) continue;

			bool hasSetup = contentsIt->find("setup") != contentsIt->end();
			bool hasByName = contentsIt->find("by_name") != contentsIt->end();
			if (hasSetup && hasByName) names.push_back(it.key());
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	std::optional<ofJson> parseCueSetupValue(const ofJson& root,
									 const ofJson* bankContents,
									 std::string raw,
									 int depth = 0) {
		if (depth > 4) return std::nullopt;
		raw = ofTrim(raw);
		if (raw.empty() || raw == "[]" || raw == "{}") return std::nullopt;

		if (raw.front() == '[' || raw.front() == '{') {
			return ofJson::parse(raw);
		}

		if (raw.front() == '"') {
			auto parsed = ofJson::parse(raw);
			if (parsed.is_string()) {
				return parseCueSetupValue(root, bankContents, parsed.get<std::string>(), depth + 1);
			}
			if (parsed.is_array() || parsed.is_object()) return parsed;
		}

		if (raw.front() == '/') {
			auto* referenced = resolveNodeByPath(root, raw);
			if (!referenced) return std::nullopt;
			auto referencedValue = firstValueString(*referenced);
			if (!referencedValue.empty()) {
				return parseCueSetupValue(root, bankContents, referencedValue, depth + 1);
			}
			if (referenced->is_array() || referenced->is_object()) return *referenced;
			return std::nullopt;
		}

		if (bankContents && bankContents->is_object()) {
			auto it = bankContents->find(raw);
			if (it != bankContents->end()) {
				auto referencedValue = firstValueString(*it);
				if (!referencedValue.empty()) {
					return parseCueSetupValue(root, bankContents, referencedValue, depth + 1);
				}
				if (it->is_array() || it->is_object()) return *it;
			}
		}

		auto* editorContents = jsonGet(root, {"CONTENTS", "timelines", "CONTENTS", "editor", "CONTENTS"});
		if (editorContents && editorContents->is_object()) {
			auto editorIt = editorContents->find(raw);
			if (editorIt != editorContents->end()) {
				auto referencedValue = firstValueString(*editorIt);
				if (!referencedValue.empty()) {
					return parseCueSetupValue(root, bankContents, referencedValue, depth + 1);
				}
				if (editorIt->is_array() || editorIt->is_object()) return *editorIt;
			}
		}

		return std::nullopt;
	}

	std::string resolveCueBankName(const ofJson& root,
							  const std::vector<std::string>& availableBanks,
							  const std::string& configuredBank,
							  bool followActiveBank) {
		auto containsBank = [&](const std::string& bankName) {
			return std::find(availableBanks.begin(), availableBanks.end(), bankName) != availableBanks.end();
		};

		if (followActiveBank) {
			auto* activeBankNode = jsonGet(root, {"CONTENTS", "timelines", "CONTENTS", "active_bank"});
			auto activeBank = activeBankNode ? firstValueString(*activeBankNode) : std::string();
			if (!activeBank.empty() && containsBank(activeBank)) return activeBank;
		}

		if (!configuredBank.empty() && containsBank(configuredBank)) return configuredBank;
		return availableBanks.empty() ? std::string() : availableBanks.front();
	}

	int normalizeCueIndex(int index) {
		if (index >= 1 && index <= 8) return index - 1;
		return index;
	}

	unsigned char jsonColorComponent(const ofJson& value) {
		if (!value.is_number()) return 0;
		double component = value.get<double>();
		if (component <= 1.0) component *= 255.0;
		return static_cast<unsigned char>(ofClamp(std::round(component), 0.0, 255.0));
	}

	ofColor parseCueColorValue(const ofJson& value) {
		if (value.is_array() && value.size() >= 3) {
			return ofColor(jsonColorComponent(value[0]),
						   jsonColorComponent(value[1]),
						   jsonColorComponent(value[2]));
		}
		if (value.is_object()) {
			auto red = jsonIntValue(value, {"r", "red"});
			auto green = jsonIntValue(value, {"g", "green"});
			auto blue = jsonIntValue(value, {"b", "blue"});
			if (red && green && blue) {
				return ofColor(static_cast<unsigned char>(*red), static_cast<unsigned char>(*green), static_cast<unsigned char>(*blue));
			}
		}
		if (value.is_number_integer()) {
			auto rgb = static_cast<uint32_t>(value.get<int>());
			return ofColor(static_cast<unsigned char>((rgb >> 16) & 0xFF),
						   static_cast<unsigned char>((rgb >> 8) & 0xFF),
						   static_cast<unsigned char>(rgb & 0xFF));
		}
		if (value.is_string()) {
			auto colorText = ofToLower(value.get<std::string>());
			if (!colorText.empty() && colorText[0] == '#') {
				return ofColor::fromHex(ofHexToInt(colorText.substr(1)));
			}
			if (colorText.rfind("0x", 0) == 0) {
				return ofColor::fromHex(ofHexToInt(colorText.substr(2)));
			}
			if (colorText == "red") return ofColor(255, 0, 0);
			if (colorText == "green") return ofColor(0, 255, 0);
			if (colorText == "blue") return ofColor(0, 128, 255);
			if (colorText == "yellow") return ofColor(255, 220, 0);
			if (colorText == "orange") return ofColor(255, 140, 0);
			if (colorText == "white") return ofColor::white;
		}
		return ofColor::white;
	}

	ofColor parseCueColor(const ofJson& node) {
		for (auto key : {"color", "colour", "fill", "rgb"}) {
			auto it = node.find(key);
			if (it != node.end()) return parseCueColorValue(*it);
		}
		auto red = jsonIntValue(node, {"r", "red"});
		auto green = jsonIntValue(node, {"g", "green"});
		auto blue = jsonIntValue(node, {"b", "blue"});
		if (red && green && blue) {
			return ofColor(static_cast<unsigned char>(*red), static_cast<unsigned char>(*green), static_cast<unsigned char>(*blue));
		}
		return ofColor::white;
	}

	bool tryBuildCueItem(const ofJson& node,
					 const std::string& fallbackName,
					 const std::unordered_map<std::string, std::string>& addressByName,
					 const std::string& fallbackOscPrefix,
					 int gridRows,
					 bool flipTopOrigin,
					 CueGridItem& outCue) {
		if (!node.is_object()) return false;

		auto name = jsonStringValue(node, {"name", "cue_name", "cue", "id", "label", "title"});
		std::string cueName = name ? *name : fallbackName;
		if (cueName.empty()) return false;

		auto column = jsonIntValue(node, {"column", "col", "x"});
		auto row = jsonIntValue(node, {"row", "line", "y"});
		auto posIt = node.find("position");
		if ((!column || !row) && posIt != node.end() && posIt->is_object()) {
			if (!column) column = jsonIntValue(*posIt, {"column", "col", "x"});
			if (!row) row = jsonIntValue(*posIt, {"row", "line", "y"});
		}
		if (!column || !row) return false;

		outCue.name = cueName;
		outCue.column = normalizeCueIndex(*column);
		int mappedRow = normalizeCueIndex(*row);
		if (flipTopOrigin && gridRows > 0) {
			mappedRow = (gridRows - 1) - mappedRow;
		}
		outCue.row = mappedRow;
		outCue.color = parseCueColor(node);
		outCue.isPlaying = node.value("is_playing", false);
		outCue.isLastStarted = node.value("is_last_started", false);
		auto oscIt = addressByName.find(cueName);
		outCue.oscAddress = oscIt != addressByName.end()
			? oscIt->second
			: fallbackOscPrefix + "/" + cueName + "/play_from_beginning";
		return outCue.isValid();
	}

	void collectCueItems(const ofJson& node,
					 const std::string& fallbackName,
					 const std::unordered_map<std::string, std::string>& addressByName,
					 const std::string& fallbackOscPrefix,
					 int gridRows,
					 bool flipTopOrigin,
					 std::vector<CueGridItem>& cues) {
		if (node.is_array()) {
			for (const auto& entry : node) {
				collectCueItems(entry, fallbackName, addressByName, fallbackOscPrefix, gridRows, flipTopOrigin, cues);
			}
			return;
		}
		if (!node.is_object()) return;

		CueGridItem cue;
		if (tryBuildCueItem(node, fallbackName, addressByName, fallbackOscPrefix, gridRows, flipTopOrigin, cue)) {
			cues.push_back(cue);
			return;
		}

		for (auto it = node.begin(); it != node.end(); ++it) {
			collectCueItems(it.value(), it.key(), addressByName, fallbackOscPrefix, gridRows, flipTopOrigin, cues);
		}
	}
}

TimelineGridState CueGridBuilder::build(const ofJson& madMapperJson) {
	TimelineGridState state;
	state.rows = rows;
	state.cols = cols;

	availableBanks = timelineBankNames(madMapperJson);
	resolvedBank = resolveCueBankName(madMapperJson, availableBanks, configuredBank, followActiveBank);
	if (resolvedBank.empty()) return state;

	auto* bankContents = jsonGet(madMapperJson, {"CONTENTS", "timelines", "CONTENTS", resolvedBank.c_str(), "CONTENTS"});
	auto* setupNode = bankContents ? jsonGet(*bankContents, {"setup"}) : nullptr;
	auto* byNameNode = bankContents ? jsonGet(*bankContents, {"by_name", "CONTENTS"}) : nullptr;
	if (!bankContents || !setupNode || !byNameNode) return state;

	std::unordered_map<std::string, std::string> addressByName;
	for (auto it = byNameNode->begin(); it != byNameNode->end(); ++it) {
		if (!it.value().is_object()) continue;
		const ofJson* playFromBeginning = jsonGet(it.value(), {"CONTENTS", "play_from_beginning", "FULL_PATH"});
		const ofJson* play = jsonGet(it.value(), {"CONTENTS", "play", "FULL_PATH"});
		const ofJson* fullPath = jsonGet(it.value(), {"FULL_PATH"});
		if (playFromBeginning && playFromBeginning->is_string()) {
			addressByName[it.key()] = playFromBeginning->get<std::string>();
		} else if (play && play->is_string()) {
			addressByName[it.key()] = play->get<std::string>();
		} else if (fullPath && fullPath->is_string()) {
			addressByName[it.key()] = fullPath->get<std::string>() + "/play_from_beginning";
		}
	}

	ofJson setupJson;
	try {
		auto setupValue = firstValueJson(*setupNode);
		if (!setupValue) return state;
		if (setupValue->is_string()) {
			auto setupJsonOpt = parseCueSetupValue(madMapperJson, bankContents, setupValue->get<std::string>());
			if (!setupJsonOpt) return state;
			setupJson = *setupJsonOpt;
		} else if (setupValue->is_array() || setupValue->is_object()) {
			setupJson = *setupValue;
		} else {
			return state;
		}
	} catch (const std::exception& exception) {
		ofLogWarning("CueGridBuilder") << "Failed to parse cue setup JSON: " << exception.what();
		return state;
	}

	collectCueItems(setupJson,
					std::string(),
					addressByName,
					"/timelines/" + resolvedBank + "/by_name",
					rows,
					flipTopOrigin,
					state.cells);

	// Deduplicate by grid position (last one wins), then sort row-major.
	std::unordered_map<std::string, CueGridItem> deduped;
	for (const auto& cue : state.cells) {
		if (!cue.isValid()) continue;
		deduped[ofToString(cue.row) + ":" + ofToString(cue.column)] = cue;
	}

	state.cells.clear();
	for (const auto& entry : deduped) {
		state.cells.push_back(entry.second);
	}

	std::sort(state.cells.begin(), state.cells.end(), [](const CueGridItem& left, const CueGridItem& right) {
		if (left.row != right.row) return left.row < right.row;
		return left.column < right.column;
	});

	state.bankName = resolvedBank;
	return state;
}
