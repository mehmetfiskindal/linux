#pragma once
/* Sailfish OS system TrueType font provider (Harbour-safe).
 *
 * Supplies Sailfish OS system TTF fonts from /usr/share/fonts/ for generic CSS
 * font families (sans-serif, serif, monospace, system-ui) via
 * FontRegistry::setRuntimeFontProvider so apps without bundled @font-face
 * rules render crisp anti-aliased text instead of the 8x16 bitmap fallback.
 *
 * App-bundled @font-face families always take precedence: lookupFamily returns
 * -1 whenever generated::lookupFontFamily(family) already knows the name, and
 * fontBytes returns nullptr for any non-system family ID. */

#include "graphics/font.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gea::sailfish::fonts {

inline constexpr int kSystemFamilyBase = 10000;
inline constexpr int kSlotSans = 0;
inline constexpr int kSlotSerif = 1;
inline constexpr int kSlotMono = 2;
inline constexpr int kSlotCount = 3;

inline constexpr int kStyleRegular = 0;
inline constexpr int kStyleBold = 1;
inline constexpr int kStyleItalic = 2;
inline constexpr int kStyleBoldItalic = 3;
inline constexpr int kStyleCount = 4;

inline int classifyFontStyle(int weight, bool italic = false)
{
	const bool bold = weight >= 600;
	if (bold && italic) return kStyleBoldItalic;
	if (bold) return kStyleBold;
	if (italic) return kStyleItalic;
	return kStyleRegular;
}

inline std::string normalizeFamilyName(const char *raw)
{
	if (!raw) return {};
	std::string out;
	while (*raw && std::isspace(static_cast<unsigned char>(*raw))) ++raw;
	std::size_t len = std::strlen(raw);
	while (len > 0 && std::isspace(static_cast<unsigned char>(raw[len - 1]))) --len;
	if (len >= 2 && ((raw[0] == '"' && raw[len - 1] == '"') || (raw[0] == '\'' && raw[len - 1] == '\''))) {
		++raw;
		len -= 2;
	}
	out.reserve(len);
	for (std::size_t i = 0; i < len; ++i) {
		out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i]))));
	}
	return out;
}

inline int classifySingleFamily(const std::string &name)
{
	if (name.empty()) return -1;
	if (name == "sans-serif" || name == "sans" || name == "system-ui" || name == "-apple-system" ||
	    name == "ui-sans-serif" || name == "sail sans pro" || name == "sailsanspro" ||
	    name == "liberation sans" || name == "dejavu sans" || name == "helvetica" ||
	    name == "arial" || name == "roboto" || name == "inter") {
		return kSlotSans;
	}
	if (name == "serif" || name == "ui-serif" || name == "liberation serif" || name == "dejavu serif" ||
	    name == "georgia" || name == "times" || name == "times new roman") {
		return kSlotSerif;
	}
	if (name == "monospace" || name == "mono" || name == "ui-monospace" || name == "liberation mono" ||
	    name == "dejavu sans mono" || name == "courier" || name == "courier new" ||
	    name == "menlo" || name == "consolas") {
		return kSlotMono;
	}
	return -1;
}

inline int classifySystemSlot(const char *family)
{
	if (!family || !*family) return -1;
	if (!std::strchr(family, ',')) {
		return classifySingleFamily(normalizeFamilyName(family));
	}
	const char *p = family;
	while (*p) {
		const char *comma = std::strchr(p, ',');
		std::string token = comma ? std::string(p, static_cast<std::size_t>(comma - p)) : std::string(p);
		const int slot = classifySingleFamily(normalizeFamilyName(token.c_str()));
		if (slot >= 0) return slot;
		if (!comma) break;
		p = comma + 1;
	}
	return -1;
}

inline const char *slotCanonicalName(int slot)
{
	switch (slot) {
	case kSlotSans: return "sans-serif";
	case kSlotSerif: return "serif";
	case kSlotMono: return "monospace";
	default: return nullptr;
	}
}

namespace detail {

struct SystemFontSlot {
	bool attempted = false;
	std::vector<std::uint8_t> bytes;
};

struct SystemFontState {
	SystemFontSlot slots[kSlotCount][kStyleCount];
	std::vector<std::string> customPaths[kSlotCount][kStyleCount];
	bool installed = false;
};

inline SystemFontState &state()
{
	static SystemFontState s;
	return s;
}

inline bool readFileBytes(const char *path, std::vector<std::uint8_t> &out)
{
	if (!path || !*path) return false;
	std::FILE *fp = std::fopen(path, "rb");
	if (!fp) return false;
	if (std::fseek(fp, 0, SEEK_END) != 0) {
		std::fclose(fp);
		return false;
	}
	const long size = std::ftell(fp);
	if (size <= 0 || size > 16L * 1024L * 1024L) {
		std::fclose(fp);
		return false;
	}
	std::rewind(fp);
	std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
	const std::size_t got = std::fread(buf.data(), 1, buf.size(), fp);
	std::fclose(fp);
	if (got != buf.size()) return false;
	out = std::move(buf);
	return true;
}

inline const char *const *defaultCandidatesForSlot(int slot, int style = kStyleRegular)
{
	static const char *const kSansRegular[] = {
	    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
	    "/usr/share/fonts/sail-sans-pro/SailSansPro-Light.ttf",
	    nullptr,
	};
	static const char *const kSansBold[] = {
	    "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSansCondensed-Bold.ttf",
	    nullptr,
	};
	static const char *const kSansItalic[] = {
	    "/usr/share/fonts/liberation/LiberationSans-Italic.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSans-Oblique.ttf",
	    nullptr,
	};
	static const char *const kSansBoldItalic[] = {
	    "/usr/share/fonts/liberation/LiberationSans-BoldItalic.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSans-BoldOblique.ttf",
	    nullptr,
	};

	static const char *const kSerifRegular[] = {
	    "/usr/share/fonts/liberation/LiberationSerif-Regular.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSerif.ttf",
	    nullptr,
	};
	static const char *const kSerifBold[] = {
	    "/usr/share/fonts/liberation/LiberationSerif-Bold.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSerif-Bold.ttf",
	    nullptr,
	};
	static const char *const kSerifItalic[] = {
	    "/usr/share/fonts/liberation/LiberationSerif-Italic.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSerif-Italic.ttf",
	    nullptr,
	};
	static const char *const kSerifBoldItalic[] = {
	    "/usr/share/fonts/liberation/LiberationSerif-BoldItalic.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSerif-BoldItalic.ttf",
	    nullptr,
	};

	static const char *const kMonoRegular[] = {
	    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
	    nullptr,
	};
	static const char *const kMonoBold[] = {
	    "/usr/share/fonts/liberation/LiberationMono-Bold.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSansMono-Bold.ttf",
	    nullptr,
	};
	static const char *const kMonoItalic[] = {
	    "/usr/share/fonts/liberation/LiberationMono-Italic.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSansMono-Oblique.ttf",
	    nullptr,
	};
	static const char *const kMonoBoldItalic[] = {
	    "/usr/share/fonts/liberation/LiberationMono-BoldItalic.ttf",
	    "/usr/share/fonts/dejavu/DejaVuSansMono-BoldOblique.ttf",
	    nullptr,
	};

	switch (slot) {
	case kSlotSans:
		switch (style) {
		case kStyleBold: return kSansBold;
		case kStyleItalic: return kSansItalic;
		case kStyleBoldItalic: return kSansBoldItalic;
		default: return kSansRegular;
		}
	case kSlotSerif:
		switch (style) {
		case kStyleBold: return kSerifBold;
		case kStyleItalic: return kSerifItalic;
		case kStyleBoldItalic: return kSerifBoldItalic;
		default: return kSerifRegular;
		}
	case kSlotMono:
		switch (style) {
		case kStyleBold: return kMonoBold;
		case kStyleItalic: return kMonoItalic;
		case kStyleBoldItalic: return kMonoBoldItalic;
		default: return kMonoRegular;
		}
	default: return nullptr;
	}
}

inline bool ensureSlotLoaded(int slot, int style = kStyleRegular)
{
	if (slot < 0 || slot >= kSlotCount || style < 0 || style >= kStyleCount) return false;
	auto &s = state();
	auto &entry = s.slots[slot][style];
	if (entry.attempted) return !entry.bytes.empty();
	entry.attempted = true;

	for (const auto &custom : s.customPaths[slot][style]) {
		if (readFileBytes(custom.c_str(), entry.bytes)) return true;
	}
	if (const char *const *candidates = defaultCandidatesForSlot(slot, style)) {
		for (int i = 0; candidates[i]; ++i) {
			if (readFileBytes(candidates[i], entry.bytes)) return true;
		}
	}
	return false;
}

inline int lookupFamily(const char *family)
{
	if (!family || !*family) return -1;
	// Never shadow an app-bundled @font-face family (exact or quote/space-trimmed).
	if (gea::framework::graphics::generated::lookupFontFamily(family) >= 0) return -1;
	int slot = -1;
	const char *p = family;
	while (*p) {
		const char *comma = std::strchr(p, ',');
		std::string rawToken = comma ? std::string(p, static_cast<std::size_t>(comma - p)) : std::string(p);
		// Trim spaces and outer quotes before probing app-bundled families.
		std::size_t start = 0;
		while (start < rawToken.size() && std::isspace(static_cast<unsigned char>(rawToken[start]))) ++start;
		std::size_t end = rawToken.size();
		while (end > start && std::isspace(static_cast<unsigned char>(rawToken[end - 1]))) --end;
		if (end - start >= 2 &&
		    ((rawToken[start] == '"' && rawToken[end - 1] == '"') ||
		     (rawToken[start] == '\'' && rawToken[end - 1] == '\''))) {
			++start;
			--end;
		}
		const std::string trimmed = rawToken.substr(start, end - start);
		if (!trimmed.empty() && gea::framework::graphics::generated::lookupFontFamily(trimmed.c_str()) >= 0) {
			return -1;
		}
		const int candidate = classifySingleFamily(normalizeFamilyName(trimmed.c_str()));
		if (candidate >= 0) {
			slot = candidate;
			break;
		}
		if (!comma) break;
		p = comma + 1;
	}
	if (slot < 0) return -1;
	if (ensureSlotLoaded(slot, kStyleRegular)) return kSystemFamilyBase + slot;
	// Fall back to sans-serif if serif/monospace font is absent on device.
	if (slot != kSlotSans && ensureSlotLoaded(kSlotSans, kStyleRegular)) return kSystemFamilyBase + kSlotSans;
	return -1;
}

inline const char *familyName(int familyId)
{
	if (familyId < kSystemFamilyBase || familyId >= kSystemFamilyBase + kSlotCount) return nullptr;
	return slotCanonicalName(familyId - kSystemFamilyBase);
}

inline const std::uint8_t *fontBytesForSlotStyle(int slot, int style, unsigned long *length)
{
	if (slot < 0 || slot >= kSlotCount) return nullptr;
	int order[kStyleCount] = {kStyleRegular, kStyleRegular, kStyleRegular, kStyleRegular};
	int count = 0;
	switch (style) {
	case kStyleBoldItalic:
		order[count++] = kStyleBoldItalic;
		order[count++] = kStyleBold;
		order[count++] = kStyleItalic;
		order[count++] = kStyleRegular;
		break;
	case kStyleBold:
		order[count++] = kStyleBold;
		order[count++] = kStyleRegular;
		break;
	case kStyleItalic:
		order[count++] = kStyleItalic;
		order[count++] = kStyleRegular;
		break;
	default:
		order[count++] = kStyleRegular;
		break;
	}
	for (int i = 0; i < count; ++i) {
		if (ensureSlotLoaded(slot, order[i])) {
			const auto &bytes = state().slots[slot][order[i]].bytes;
			if (length) *length = static_cast<unsigned long>(bytes.size());
			return bytes.data();
		}
	}
	return nullptr;
}

inline const std::uint8_t *fontBytes(int familyId, unsigned long *length)
{
	if (familyId < kSystemFamilyBase || familyId >= kSystemFamilyBase + kSlotCount) return nullptr;
	return fontBytesForSlotStyle(familyId - kSystemFamilyBase, kStyleRegular, length);
}

inline const std::uint8_t *fontBytesForStyle(int familyId, int weight, int stretch, unsigned long *length)
{
	(void)stretch;
	if (familyId < kSystemFamilyBase || familyId >= kSystemFamilyBase + kSlotCount) return nullptr;
	return fontBytesForSlotStyle(familyId - kSystemFamilyBase, classifyFontStyle(weight, false), length);
}

inline const std::uint8_t *fontBytesForStyleItalic(int familyId, int weight, int stretch, bool italic, unsigned long *length)
{
	(void)stretch;
	if (familyId < kSystemFamilyBase || familyId >= kSystemFamilyBase + kSlotCount) return nullptr;
	return fontBytesForSlotStyle(familyId - kSystemFamilyBase, classifyFontStyle(weight, italic), length);
}

template <typename Provider = gea::framework::graphics::RuntimeFontProvider>
constexpr Provider makeProvider()
{
	Provider p{};
	p.lookupFamily = lookupFamily;
	p.familyName = familyName;
	p.fontBytes = fontBytes;
	if constexpr (requires(Provider &q) { q.fontBytesForStyle = fontBytesForStyle; }) {
		p.fontBytesForStyle = fontBytesForStyle;
	} else if constexpr (requires(Provider &q) { q.fontBytesForStyle = fontBytesForStyleItalic; }) {
		p.fontBytesForStyle = fontBytesForStyleItalic;
	}
	return p;
}

// Constant-initialized so early-priority static constructors can register it
// before default-priority C++ static CSS registrations execute.
inline constexpr gea::framework::graphics::RuntimeFontProvider kProvider = makeProvider();

}  // namespace detail

// Optional override for unit tests or custom search paths.
inline void addSearchPath(int slot, int style, const std::string &path)
{
	if (slot < 0 || slot >= kSlotCount || style < 0 || style >= kStyleCount || path.empty()) return;
	auto &s = detail::state();
	s.customPaths[slot][style].push_back(path);
	s.slots[slot][style].attempted = false;
	s.slots[slot][style].bytes.clear();
}

inline void addSearchPath(int slot, const std::string &path)
{
	addSearchPath(slot, kStyleRegular, path);
}

inline const std::uint8_t *fontBytesForStyle(int familyId, int weight, int stretch, unsigned long *length)
{
	return detail::fontBytesForStyle(familyId, weight, stretch, length);
}

inline const std::uint8_t *fontBytesForStyle(int familyId, int weight, int stretch, bool italic, unsigned long *length)
{
	return detail::fontBytesForStyleItalic(familyId, weight, stretch, italic, length);
}

inline void install()
{
	auto &s = detail::state();
	if (s.installed) return;
	s.installed = true;
	gea::framework::graphics::FontRegistry::setRuntimeFontProvider(&detail::kProvider);
}

inline void shutdown()
{
	auto &s = detail::state();
	if (s.installed) {
		gea::framework::graphics::FontRegistry::setRuntimeFontProvider(nullptr);
		s.installed = false;
	}
	for (int i = 0; i < kSlotCount; ++i) {
		for (int j = 0; j < kStyleCount; ++j) {
			s.slots[i][j].attempted = false;
			s.slots[i][j].bytes.clear();
			s.customPaths[i][j].clear();
		}
	}
}

}  // namespace gea::sailfish::fonts
