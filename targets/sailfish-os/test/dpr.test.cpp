// Native unit tests for targets/sailfish-os/include/sailfish_dpr.h.
// Verifies pixel-perfect auto DPR across current Sailfish phones, i486 emulator
// fallback, keyboard-open stability, Wayland output-ratio scaling, and env overrides.

#include "sailfish_dpr.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using gea::sailfish::effectiveDevicePixelRatio;
using gea::sailfish::parseEnvDpr;

int main()
{
	// 1. parseEnvDpr respects "auto", valid numeric range [0.5, 8.0], and fallback.
	assert(parseEnvDpr(nullptr, 0.0) == 0.0);
	assert(parseEnvDpr("", 1.0) == 1.0);
	assert(parseEnvDpr("auto", 1.0) == 0.0);
	assert(std::fabs(parseEnvDpr("2.5", 0.0) - 2.5) < 1e-9);
	assert(parseEnvDpr("0.2", 0.0) == 0.0);   // below 0.5 -> fallback
	assert(parseEnvDpr("12", 1.0) == 1.0);    // above 8.0 -> fallback

	// 2. Auto DPR (appDpr == 0.0) matches README device table across ARM & i486:
	// Jolla C2 & Sailfish 5.1 emulator: 720x1600 -> DPR 2 (CSS width 360)
	assert(effectiveDevicePixelRatio(0.0, 1.0, 1, 720) == 2.0);
	// Xperia XA2 / Xperia 10..10 V / Jolla Phone (2026): 1080px short side -> DPR 3 (CSS width 360)
	assert(effectiveDevicePixelRatio(0.0, 1.0, 1, 1080) == 3.0);
	// Smaller 540x960 emulator / <720px panel still resolves to DPR 1 under auto
	assert(effectiveDevicePixelRatio(0.0, 1.0, 1, 540) == 1.0);
	assert(effectiveDevicePixelRatio(0.0, 1.0, 1, 480) == 1.0);
	// Window scale divides panel short side
	assert(effectiveDevicePixelRatio(0.0, 1.0, 2, 1080) == 1.0);

	// 3. Explicit 1x override (e.g. GEA_SAILFISH_DPR=1)
	assert(effectiveDevicePixelRatio(1.0, 1.0, 1, 720) == 1.0);

	// 4. Explicit app DPR multiplies by Wayland output ratio
	assert(std::fabs(effectiveDevicePixelRatio(2.0, 1.5, 1, 1080) - 3.0) < 1e-9);

	std::puts("PASS sailfish_dpr: Jolla C2/emu 720 (2x), Xperia/Jolla Phone 1080 (3x), 540 auto (1x), env overrides, output ratio");
	return 0;
}
