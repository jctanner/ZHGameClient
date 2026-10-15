#pragma once

#include <cstdint>

// Milliseconds of simulated gameplay. Zero is reserved for unset deadlines.
// Integer conversion avoids cumulative rounding drift at 30 frames per second.
inline unsigned int AIControlAdapterSimulationTick(unsigned int frame, unsigned int framesPerSecond)
{
	if (framesPerSecond == 0u) return 1u;
	const unsigned int tick = static_cast<unsigned int>(
		static_cast<std::uint64_t>(frame) * 1000u / framesPerSecond + 1u);
	return tick == 0u ? 1u : tick;
}
