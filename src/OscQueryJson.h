#pragma once

#include "ofJson.h"
#include "ofUtils.h"
#include <initializer_list>
#include <optional>
#include <string>

// Small helpers for navigating OSCQuery JSON trees (MadMapper / TouchDesigner).
// Shared between ofApp, CueGridBuilder and friends.
namespace oscq {

	inline const ofJson* jsonGet(const ofJson& root, std::initializer_list<const char*> keys) {
		const ofJson* node = &root;
		for (auto k : keys) {
			if (!node->is_object()) return nullptr;
			auto it = node->find(k);
			if (it == node->end()) return nullptr;
			node = &(*it);
		}
		return node;
	}

	// Resolves "/a/b/c" against the tree, descending through CONTENTS wrappers
	// (falls back to direct child lookup for plain JSON objects).
	inline const ofJson* resolveNodeByPath(const ofJson& root, const std::string& path) {
		if (path.empty() || path[0] != '/') return nullptr;
		const ofJson* node = &root;
		for (auto& token : ofSplitString(path, "/", true, true)) {
			if (!node->is_object()) return nullptr;
			auto contentsIt = node->find("CONTENTS");
			if (contentsIt != node->end() && contentsIt->is_object()) {
				auto childIt = contentsIt->find(token);
				if (childIt != contentsIt->end()) {
					node = &(*childIt);
					continue;
				}
			}
			auto directIt = node->find(token);
			if (directIt == node->end()) return nullptr;
			node = &(*directIt);
		}
		return node;
	}

	inline std::string firstValueString(const ofJson& node) {
		auto it = node.find("VALUE");
		if (it == node.end() || !it->is_array() || it->empty() || !(*it)[0].is_string()) return std::string();
		return (*it)[0].get<std::string>();
	}

	inline std::optional<ofJson> firstValueJson(const ofJson& node) {
		auto it = node.find("VALUE");
		if (it == node.end() || !it->is_array() || it->empty()) return std::nullopt;
		return (*it)[0];
	}

	inline std::optional<int> jsonIntValue(const ofJson& node, std::initializer_list<const char*> keys) {
		for (auto key : keys) {
			auto it = node.find(key);
			if (it == node.end()) continue;
			if (it->is_number_integer()) return it->get<int>();
			if (it->is_number()) return static_cast<int>(std::round(it->get<double>()));
			if (it->is_string()) {
				try {
					return ofToInt(it->get<std::string>());
				} catch (...) {
				}
			}
		}
		return std::nullopt;
	}

	inline std::optional<std::string> jsonStringValue(const ofJson& node, std::initializer_list<const char*> keys) {
		for (auto key : keys) {
			auto it = node.find(key);
			if (it != node.end() && it->is_string()) return it->get<std::string>();
		}
		return std::nullopt;
	}

	inline bool isBindableOscType(const ofJson& node) {
		auto itType = node.find("TYPE");
		if (itType == node.end() || !itType->is_string()) return false;
		const std::string type = itType->get<std::string>();
		return type == "f" || type == "d" || type == "i" || type == "h" || type == "T" || type == "F";
	}

	inline std::string oscNodeDisplayName(const ofJson& node, const std::string& path) {
		auto itDescription = node.find("DESCRIPTION");
		if (itDescription != node.end() && itDescription->is_string()) {
			const auto description = itDescription->get<std::string>();
			if (!description.empty()) return description;
		}
		auto tokens = ofSplitString(path, "/", true, true);
		return tokens.empty() ? path : tokens.back();
	}

} // namespace oscq
