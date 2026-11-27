// Native unit tests for targets/sailfish-os/include/sailfish_fonts.h.
// Verifies family name normalization/classification (including fallback chains),
// early static-constructor installation ahead of default-priority CSS preludes,
// Regular/Bold/Italic/BoldItalic TTF loading and style fallback, future
// RuntimeFontProvider::fontBytesForStyle binding, app-bundled @font-face
// precedence, and clean shutdown.

#include "sailfish_fonts.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>

namespace gea::framework::graphics {

namespace {
const RuntimeFontProvider *g_provider = nullptr;
bool g_appHasSans = false;
}  // namespace

void FontRegistry::setRuntimeFontProvider(const RuntimeFontProvider *provider)
{
	g_provider = provider;
}

int FontRegistry::familyId(const char *family)
{
	if (g_provider) {
		const int id = g_provider->lookupFamily(family);
		if (id >= 0) return id;
	}
	return generated::lookupFontFamily(family);
}

namespace generated {
int lookupFontFamily(const char *family)
{
	if (g_appHasSans && family && std::strcmp(family, "sans-serif") == 0) return 0;
	if (family && std::strcmp(family, "AppCustomFont") == 0) return 1;
	return -1;
}
}  // namespace generated

}  // namespace gea::framework::graphics

namespace {

// Mirrors sailfish_main.cpp's priority-200 constructor vs. Core's default-priority
// (65535) GeaPluginCppPreludeRegistration static CSS constructor.
bool g_earlyProviderWasInstalled = false;

std::string &earlyTmpPath()
{
	static std::string path;
	return path;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor(200)))
void earlyInstallFontsForTest()
{
	earlyTmpPath() = "/tmp/gea-sailfish-fonts-early-" + std::to_string(getpid()) + ".ttf";
	std::ofstream out(earlyTmpPath(), std::ios::binary);
	out.write("EARLY_SERIF_TTF", 15);
	out.close();
	gea::sailfish::fonts::addSearchPath(gea::sailfish::fonts::kSlotSerif, earlyTmpPath());
	gea::sailfish::fonts::install();
}
#endif

struct SimulatedGeaPluginCppPreludeRegistration {
	int resolvedSerifId = -1;
	SimulatedGeaPluginCppPreludeRegistration()
	{
		g_earlyProviderWasInstalled = (gea::framework::graphics::g_provider != nullptr);
		resolvedSerifId = gea::framework::graphics::FontRegistry::familyId("Georgia");
	}
};
SimulatedGeaPluginCppPreludeRegistration g_simulatedStaticCss;

struct FutureProvider4Arg {
	int (*lookupFamily)(const char *family);
	const char *(*familyName)(int familyId);
	const std::uint8_t *(*fontBytes)(int familyId, unsigned long *length);
	const std::uint8_t *(*fontBytesForStyle)(int familyId, int weight, int stretch, unsigned long *length);
};

struct FutureProvider5Arg {
	int (*lookupFamily)(const char *family);
	const char *(*familyName)(int familyId);
	const std::uint8_t *(*fontBytes)(int familyId, unsigned long *length);
	const std::uint8_t *(*fontBytesForStyle)(int familyId, int weight, int stretch, bool italic, unsigned long *length);
};

void writeSyntheticFile(const std::string &path, const char *data)
{
	std::ofstream out(path, std::ios::binary);
	out.write(data, static_cast<std::streamsize>(std::strlen(data)));
}

}  // namespace

int main()
{
	using namespace gea::sailfish::fonts;

#if defined(__GNUC__) || defined(__clang__)
	// 0. Early constructor (priority 200) must beat default-priority static CSS init.
	assert(g_earlyProviderWasInstalled);
	assert(g_simulatedStaticCss.resolvedSerifId == kSystemFamilyBase + kSlotSerif);
	shutdown();
	assert(gea::framework::graphics::g_provider == nullptr);
	if (!earlyTmpPath().empty()) std::remove(earlyTmpPath().c_str());
#endif

	// 1. Family normalization, classification, fallback chains, and style classification
	assert(normalizeFamilyName("  'Sans-Serif' ") == "sans-serif");
	assert(normalizeFamilyName("\"Liberation Sans\"") == "liberation sans");
	assert(classifySystemSlot("sans-serif") == kSlotSans);
	assert(classifySystemSlot("system-ui") == kSlotSans);
	assert(classifySystemSlot("Sail Sans Pro") == kSlotSans);
	assert(classifySystemSlot("serif") == kSlotSerif);
	assert(classifySystemSlot("Georgia, serif") == kSlotSerif);
	assert(classifySystemSlot("\"UnknownFont\", sans-serif") == kSlotSans);
	assert(classifySystemSlot("monospace") == kSlotMono);
	assert(classifySystemSlot("UnknownCustomFont") == -1);
	assert(classifySystemSlot("") == -1);
	assert(classifySystemSlot(nullptr) == -1);

	assert(classifyFontStyle(400, false) == kStyleRegular);
	assert(classifyFontStyle(599, false) == kStyleRegular);
	assert(classifyFontStyle(600, false) == kStyleBold);
	assert(classifyFontStyle(700, false) == kStyleBold);
	assert(classifyFontStyle(400, true) == kStyleItalic);
	assert(classifyFontStyle(700, true) == kStyleBoldItalic);

	// 2. Load synthetic Regular, Bold, and Italic TTF files and verify style resolution + fallback
	const std::string baseTmp = "/tmp/gea-sailfish-fonts-test-" + std::to_string(getpid());
	const std::string regPath = baseTmp + "-reg.ttf";
	const std::string boldPath = baseTmp + "-bold.ttf";
	const std::string italicPath = baseTmp + "-italic.ttf";
	const std::string missingPath = baseTmp + "-does-not-exist.ttf";
	writeSyntheticFile(regPath, "FAKE_TTF_BYTES_1234");
	writeSyntheticFile(boldPath, "FAKE_BOLD_TTF_BYTES");
	writeSyntheticFile(italicPath, "FAKE_ITAL_TTF_BYTES");

	addSearchPath(kSlotSans, regPath);
	addSearchPath(kSlotSans, kStyleBold, boldPath);
	addSearchPath(kSlotSans, kStyleItalic, italicPath);
	// Missing BoldItalic custom path (plus verify BoldItalic falls back cleanly to Bold if system absent or matches Bold/BoldItalic)
	addSearchPath(kSlotSans, kStyleBoldItalic, missingPath);
	install();
	assert(gea::framework::graphics::g_provider != nullptr);

	const auto *provider = gea::framework::graphics::g_provider;
	const int sansId = provider->lookupFamily("sans-serif");
	assert(sansId == kSystemFamilyBase + kSlotSans);
	assert(std::strcmp(provider->familyName(sansId), "sans-serif") == 0);
	assert(provider->lookupFamily("\"MissingPrimary\", sans-serif") == sansId);

	unsigned long len = 0;
	const std::uint8_t *bytes = provider->fontBytes(sansId, &len);
	assert(bytes != nullptr);
	assert(len == 19);
	assert(std::memcmp(bytes, "FAKE_TTF_BYTES_1234", 19) == 0);

	// Styled lookup (Regular, Bold, Italic) via public helpers & future RuntimeFontProvider traits
	const std::uint8_t *regBytes = fontBytesForStyle(sansId, 400, 1000, &len);
	assert(regBytes != nullptr && len == 19 && std::memcmp(regBytes, "FAKE_TTF_BYTES_1234", 19) == 0);

	const std::uint8_t *boldBytes = fontBytesForStyle(sansId, 700, 1000, &len);
	assert(boldBytes != nullptr && len == 19 && std::memcmp(boldBytes, "FAKE_BOLD_TTF_BYTES", 19) == 0);

	const std::uint8_t *italicBytes = fontBytesForStyle(sansId, 400, 1000, true, &len);
	assert(italicBytes != nullptr && len == 19 && std::memcmp(italicBytes, "FAKE_ITAL_TTF_BYTES", 19) == 0);

	constexpr auto p4 = detail::makeProvider<FutureProvider4Arg>();
	static_assert(p4.fontBytesForStyle != nullptr);
	const std::uint8_t *p4Bold = p4.fontBytesForStyle(sansId, 800, 1000, &len);
	assert(p4Bold != nullptr && len == 19 && std::memcmp(p4Bold, "FAKE_BOLD_TTF_BYTES", 19) == 0);

	constexpr auto p5 = detail::makeProvider<FutureProvider5Arg>();
	static_assert(p5.fontBytesForStyle != nullptr);
	const std::uint8_t *p5Italic = p5.fontBytesForStyle(sansId, 400, 1000, true, &len);
	assert(p5Italic != nullptr && len == 19 && std::memcmp(p5Italic, "FAKE_ITAL_TTF_BYTES", 19) == 0);

	// BoldItalic fallback when no BoldItalic file is loaded on a synthetic mono slot
	const std::string monoReg = baseTmp + "-mono-reg.ttf";
	const std::string monoBold = baseTmp + "-mono-bold.ttf";
	writeSyntheticFile(monoReg, "MONO_REG_TTF");
	writeSyntheticFile(monoBold, "MONO_BOLD_TTF");
	addSearchPath(kSlotMono, kStyleRegular, monoReg);
	addSearchPath(kSlotMono, kStyleBold, monoBold);
	const int monoId = provider->lookupFamily("monospace");
	assert(monoId == kSystemFamilyBase + kSlotMono);
	const std::uint8_t *monoBi = p5.fontBytesForStyle(monoId, 700, 1000, true, &len);
	assert(monoBi != nullptr && len > 0);
	// On a host without /usr/share/fonts/liberation or dejavu mono bold-italic, falls back to Bold;
	// on a host that does have system Mono BoldItalic TTF, returns valid TTF bytes either way.
	if (len == 13) assert(std::memcmp(monoBi, "MONO_BOLD_TTF", 13) == 0);

	// Unknown / app-bundled family IDs must NOT be served by the system provider.
	assert(provider->fontBytes(0, &len) == nullptr);
	assert(provider->fontBytes(1, &len) == nullptr);
	assert(fontBytesForStyle(0, 700, 1000, &len) == nullptr);
	assert(provider->familyName(0) == nullptr);
	assert(provider->lookupFamily("AppCustomFont") == -1);
	assert(provider->lookupFamily("\"AppCustomFont\", sans-serif") == -1);

	// If an app bundles its own "sans-serif", the app-bundled font wins.
	gea::framework::graphics::g_appHasSans = true;
	assert(provider->lookupFamily("sans-serif") == -1);
	assert(provider->lookupFamily("'sans-serif'") == -1);
	assert(provider->lookupFamily(" \"sans-serif\" ") == -1);
	assert(provider->lookupFamily("UnknownFont, sans-serif") == -1);
	gea::framework::graphics::g_appHasSans = false;

	// Clean shutdown unregisters provider and releases buffers.
	shutdown();
	assert(gea::framework::graphics::g_provider == nullptr);
	std::remove(regPath.c_str());
	std::remove(boldPath.c_str());
	std::remove(italicPath.c_str());
	std::remove(monoReg.c_str());
	std::remove(monoBold.c_str());

	std::puts("PASS sailfish_fonts: early static init, classification, Bold/Italic TTF loading, app-font precedence, clean shutdown");
	return 0;
}
