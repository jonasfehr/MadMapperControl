#pragma once

#include "MidiControlSurface.h" // TimelineGridState / CueGridItem
#include "ofJson.h"
#include <string>
#include <vector>

// Builds the controller cue grid from the MadMapper OSCQuery tree
// (/timelines/<bank>/setup + /timelines/<bank>/by_name).
// Pure JSON→state transformation: no app or device state involved.
class CueGridBuilder {
  public:
	// Inputs
	int rows = 8;
	int cols = 8;
	bool flipTopOrigin = true;
	std::string configuredBank; // preferred bank name (may be overridden by active bank)
	bool followActiveBank = true;

	// Outputs, valid after build()
	std::vector<std::string> availableBanks;
	std::string resolvedBank;

	TimelineGridState build(const ofJson& madMapperJson);
};
