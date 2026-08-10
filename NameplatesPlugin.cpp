#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Camera.h>
#include <GWCA/GameEntities/NPC.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/CameraMgr.h>
#include <GWCA/Managers/RenderMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/QuestMgr.h>

#include <ToolboxPlugin.h>
#include <imgui.h>

#include <DirectXMath.h>
#include <vector>
#include <string>
#include <string_view>
#include <sstream>
#include <unordered_map>
#include <cwchar>
#include <optional>
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <array>
#include <utility>

inline std::wstring Utf8ToWide(const std::string& utf8) {
	if (utf8.empty()) return {};
	const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
	if (len <= 0) return {};
	std::wstring out(static_cast<size_t>(len), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), len);
	return out;
}

inline void WideToUtf8Into(const std::wstring& wide, std::string& out) {
	if (wide.empty()) { out.clear(); return; }
	const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	if (len <= 0) { out.clear(); return; }
	out.resize(static_cast<size_t>(len));
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
}

inline std::string WideToUtf8(const std::wstring& wide) {
	std::string out;
	WideToUtf8Into(wide, out);
	return out;
}

inline std::string TruncateWithEllipsis(ImFont* font, float font_size, const std::wstring& name, std::string_view full_utf8, float max_width) {
	const char* full_begin = full_utf8.data();
	const char* full_end = full_begin + full_utf8.size();
	const ImVec2 full_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, full_begin, full_end);
	if (full_size.x <= max_width) return std::string(full_utf8);

	size_t lo = 0, hi = name.size();
	std::string best_utf8 = WideToUtf8(L"...");
	while (lo < hi) {
		const size_t mid = lo + (hi - lo + 1) / 2;
		const std::string candidate_utf8 = WideToUtf8(name.substr(0, mid) + L"...");
		const ImVec2 candidate_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, candidate_utf8.c_str());
		if (candidate_size.x <= max_width) {
			lo = mid;
			best_utf8 = candidate_utf8;
		}
		else {
			hi = mid - 1;
		}
	}
	return best_utf8;
}

inline ImU32 ScaleAlpha(ImU32 color, float mult) {
	if (mult >= 1.f) return color;
	ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
	c.w *= mult;
	return ImGui::ColorConvertFloat4ToU32(c);
}

inline void DrawStatusTriangles(ImDrawList* draw_list, float right_x, float center_y, const GW::AgentLiving* living, float opacity_mult = 1.f) {
	static constexpr ImU32 kEnchantedColor = IM_COL32(224, 253, 94, 255);
	static constexpr ImU32 kHexedColor = IM_COL32(253, 113, 255, 255);
	static constexpr ImU32 kConditionedColor = IM_COL32(160, 117, 85, 255);
	static constexpr float kTriHeight = 8.f;
	static constexpr float kTriWidth = kTriHeight * 1.3f;
	static constexpr float kTriSpacing = kTriWidth + 2.f;
	static constexpr ImU32 kOutlineColor = IM_COL32(0, 0, 0, 255);
	static constexpr float kOutlinePx = 1.0f;

	int count = 0;
	auto make_triangle = [&](float w, float h, float ox, float oy, bool upsidedown, ImVec2& p1, ImVec2& p2, ImVec2& p3) {
		if (upsidedown) {
			p1 = ImVec2(ox, oy);
			p2 = ImVec2(ox + w, oy);
			p3 = ImVec2(ox + w / 2.f, oy + h);
		}
		else {
			p1 = ImVec2(ox, oy + h);
			p2 = ImVec2(ox + w, oy + h);
			p3 = ImVec2(ox + w / 2.f, oy);
		}
	};

	auto draw_tri = [&](ImU32 color, bool upsidedown) {
		const float x = (right_x - count * kTriSpacing) - kTriWidth;
		const float y = center_y - kTriHeight / 2.f;

		ImVec2 op1, op2, op3;
		make_triangle(kTriWidth + kOutlinePx * 2.f, kTriHeight + kOutlinePx * 2.f, x - kOutlinePx, y - kOutlinePx, upsidedown, op1, op2, op3);
		draw_list->AddTriangleFilled(op1, op2, op3, ScaleAlpha(kOutlineColor, opacity_mult));

		ImVec2 p1, p2, p3;
		make_triangle(kTriWidth, kTriHeight, x, y, upsidedown, p1, p2, p3);
		draw_list->AddTriangleFilled(p1, p2, p3, ScaleAlpha(color, opacity_mult));

		++count;
	};

	if (living->GetIsEnchanted()) draw_tri(kEnchantedColor, false);
	if (living->GetIsHexed()) draw_tri(kHexedColor, true);
	if (living->GetIsConditioned()) draw_tri(kConditionedColor, true);
}

inline void DrawOutlinedText(ImDrawList* draw_list, ImFont* font, float font_size, const ImVec2& pos, ImU32 text_color, std::string_view text_utf8, float opacity_mult = 1.f) {
	static constexpr ImU32 kOutlineColor = IM_COL32(0, 0, 0, 255);
	static constexpr float kOutlineOffset = 1.f;
	const ImU32 outline_color = ScaleAlpha(kOutlineColor, opacity_mult);
	text_color = ScaleAlpha(text_color, opacity_mult);
	const char* text_begin = text_utf8.data();
	const char* text_end = text_begin + text_utf8.size();
	draw_list->AddText(font, font_size, ImVec2(pos.x - kOutlineOffset, pos.y), outline_color, text_begin, text_end);
	draw_list->AddText(font, font_size, ImVec2(pos.x + kOutlineOffset, pos.y), outline_color, text_begin, text_end);
	draw_list->AddText(font, font_size, ImVec2(pos.x, pos.y - kOutlineOffset), outline_color, text_begin, text_end);
	draw_list->AddText(font, font_size, ImVec2(pos.x, pos.y + kOutlineOffset), outline_color, text_begin, text_end);
	draw_list->AddText(font, font_size, pos, text_color, text_begin, text_end);
}

template<typename CacheMap>
inline void PruneCache(CacheMap& cache, uint64_t& tick, uint64_t& last_prune, uint64_t interval) {
	++tick;
	if (tick - last_prune < interval) return;
	last_prune = tick;

	for (auto it = cache.begin(); it != cache.end(); ) {
		if (tick - it->second.last_seen_tick >= interval) {
			it = cache.erase(it);
		} else {
			++it;
		}
	}
}

inline std::vector<std::wstring> SplitWords(const std::wstring& text) {
	std::vector<std::wstring> out;
	size_t start = 0;
	while (start <= text.size()) {
		size_t pos = text.find(L' ', start);
		if (pos == std::wstring::npos) pos = text.size();
		if (pos > start) out.emplace_back(text.substr(start, pos - start));
		start = pos + 1;
	}
	return out;
}

class StackYSmoother {
public:
	float Update(uint32_t agent_id, float target_y, float alpha) {
		Entry& e = cache_[agent_id];
		e.last_seen_tick = tick_;
		if (!e.initialized) {
			e.y = target_y;
			e.initialized = true;
		}
		else {
			e.y += (target_y - e.y) * alpha;
		}
		return e.y;
	}

	void MaybePrune() { PruneCache(cache_, tick_, last_prune_tick_, kPruneIntervalTicks); }

private:
	static constexpr uint64_t kPruneIntervalTicks = 1800;
	struct Entry {
		float y = 0.f;
		bool initialized = false;
		uint64_t last_seen_tick = 0;
	};
	std::unordered_map<uint32_t, Entry> cache_;
	uint64_t tick_ = 0, last_prune_tick_ = 0;
};

inline GW::Constants::ProfessionByte GetAgentProfession(const GW::AgentLiving* living) {
	if (living->primary != GW::Constants::ProfessionByte::None) return living->primary;
	const GW::NPC* npc = GW::Agents::GetNPCByID(living->player_number);
	return npc ? static_cast<GW::Constants::ProfessionByte>(npc->primary) : GW::Constants::ProfessionByte::None;
}

class AgentNameCache {
public:
	struct NameLookup {
		const std::wstring* lower;
		const std::wstring* display;
		const std::vector<std::wstring>* words;
		GW::Constants::ProfessionByte profession;
	};

	NameLookup Get(const GW::AgentLiving* living) {
		Entry& entry = cache_[living->agent_id];
		entry.last_seen_tick = tick_;
		const wchar_t* enc_name = GW::Agents::GetAgentEncName(living->agent_id);
		if (enc_name && wcsncmp(entry.last_enc, enc_name, kMaxEncLen - 1) != 0) {
			wcsncpy_s(entry.last_enc, enc_name, kMaxEncLen - 1);
			entry.buffer[0] = L'\0';
			entry.converted = false;
			entry.truncated_for_width = -1.f;
			GW::UI::AsyncDecodeStr(enc_name, entry.buffer, kBufferLen);
		}
		if (!entry.converted && entry.buffer[0] != L'\0') {
			entry.decoded_display = entry.buffer;
			entry.decoded_lower = entry.buffer;
			std::transform(entry.decoded_lower.begin(), entry.decoded_lower.end(), entry.decoded_lower.begin(), ::towlower);
			entry.decoded_words_lower = SplitWords(entry.decoded_lower);
			WideToUtf8Into(entry.decoded_display, entry.decoded_display_utf8);
			entry.converted = true;
		}
		if (!entry.profession_resolved) {
			entry.profession = GetAgentProfession(living);
			entry.profession_resolved = true;
		}
		return { &entry.decoded_lower, &entry.decoded_display, &entry.decoded_words_lower, entry.profession };
	}

	const std::string& GetTruncated(uint32_t agent_id, ImFont* font, float font_size, float max_width) {
		Entry& entry = cache_[agent_id];
		const float quantized_width = std::floor(max_width / 4.f) * 4.f;
		if (entry.truncated_for_width != quantized_width) {
			entry.truncated_utf8 = TruncateWithEllipsis(font, font_size, entry.decoded_display, entry.decoded_display_utf8, quantized_width);
			entry.truncated_for_width = quantized_width;
		}
		return entry.truncated_utf8;
	}

	void MaybePrune() { PruneCache(cache_, tick_, last_prune_tick_, kPruneIntervalTicks); }

private:
	static constexpr size_t kBufferLen = 256;
	static constexpr size_t kMaxEncLen = 64;
	static constexpr uint64_t kPruneIntervalTicks = 1800;
	struct Entry {
		wchar_t last_enc[kMaxEncLen] = {};
		wchar_t buffer[kBufferLen] = {};
		bool converted = false;
		float truncated_for_width = -1.f;
		uint64_t last_seen_tick = 0;
		std::wstring decoded_lower, decoded_display;
		std::vector<std::wstring> decoded_words_lower;
		std::string decoded_display_utf8, truncated_utf8;
		GW::Constants::ProfessionByte profession = GW::Constants::ProfessionByte::None;
		bool profession_resolved = false;
	};
	std::unordered_map<uint32_t, Entry> cache_;
	uint64_t tick_ = 0, last_prune_tick_ = 0;
};

inline bool IsMinipet(uint16_t player_number) {
	static constexpr std::array<uint16_t, 129> ids = {
		230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259,
		260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289,
		290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320,
		321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350,
		8035, 8344, 8349, 8350, 8351, 8352, 8354, 9038, 9039
	};
	return std::binary_search(ids.begin(), ids.end(), player_number);
}

inline std::vector<std::wstring> ParseSemicolonNameList(const std::string& raw) {
	std::vector<std::wstring> out;
	std::istringstream stream(raw);
	std::string token;
	while (std::getline(stream, token, ';')) {
		const size_t start = token.find_first_not_of(" \t\r\n");
		const size_t end = token.find_last_not_of(" \t\r\n");
		if (start == std::string::npos || end == std::string::npos) continue;

		std::wstring w = Utf8ToWide(token.substr(start, end - start + 1));
		std::transform(w.begin(), w.end(), w.begin(), ::towlower);
		if (!w.empty()) out.push_back(std::move(w));
	}
	std::sort(out.begin(), out.end());
	return out;
}

struct PriorityConfig {
	std::string raw;
	uint32_t color;
};

struct ProfessionColorConfig {
	bool enabled = true;
	uint32_t color = IM_COL32(221, 221, 221, 255);
};

struct NameplateSettings {
	bool show_enemies = true, show_summoned_allies = false, show_friendlies = true;
	bool recolor_quest_nametags = true, recolor_professions = false, fade_enemies_by_range = true, color_nameplate_text_by_combat = true;
	bool recolor_enemy_nameplates_by_profession = false;
	uint32_t combat_text_color = IM_COL32(255, 255, 0, 255);
	float max_range = 3500.0f, bar_width = 200.0f, bar_height = 20.0f, npc_health_threshold = 60.0f, allied_health_threshold = 60.0f;
	float fade_distance_near = 1500.0f, fade_distance_far = 2500.0f;
	float fade_opacity_mid = 0.75f, fade_opacity_far = 0.50f;
	float border_thickness = 1.0f;
	uint32_t enemy_color = IM_COL32(220, 40, 40, 255), quest_color = IM_COL32(255, 179, 71, 255), friendly_color = IM_COL32(0, 255, 152, 255);
	uint32_t target_border_color = IM_COL32(255, 255, 0, 255);
	uint32_t border_color = IM_COL32(0, 0, 0, 180);

	bool color_by_boss = false;
	uint32_t boss_color = IM_COL32(255, 215, 0, 255);

	std::array<ProfessionColorConfig, 11> profession_colors = {{
		{false, IM_COL32(221, 221, 221, 255)},
		{true, IM_COL32(255, 255, 136, 255)},
		{true, IM_COL32(204, 255, 153, 255)},
		{true, IM_COL32(170, 204, 255, 255)},
		{true, IM_COL32(153, 255, 204, 255)},
		{true, IM_COL32(221, 170, 255, 255)},
		{true, IM_COL32(255, 187, 187, 255)},
		{true, IM_COL32(255, 204, 238, 255)},
		{true, IM_COL32(187, 255, 255, 255)},
		{true, IM_COL32(255, 204, 153, 255)},
		{true, IM_COL32(221, 221, 255, 255)}
	}};

	std::array<PriorityConfig, 2> priorities = {{
		{"", IM_COL32(135, 206, 250, 255)},
		{"", IM_COL32(255, 105, 180, 255)}
	}};
};

class NameplatesPlugin : public ToolboxPlugin {
public:
	NameplatesPlugin() {
		pending_.reserve(256);
		placed_.reserve(256);
		order_.reserve(256);
		GW::UI::RegisterUIMessageCallback(&nametag_hook_entry_, GW::UI::UIMessage::kShowAgentNameTag, OnAgentNameTag);
		GW::UI::RegisterUIMessageCallback(&nametag_hook_entry_, GW::UI::UIMessage::kSetAgentNameTagAttribs, OnAgentNameTag);
		GW::UI::RegisterUIMessageCallback(&quest_hook_entry_, GW::UI::UIMessage::kQuestAdded, OnQuestUpdate);
		GW::UI::RegisterUIMessageCallback(&quest_hook_entry_, GW::UI::UIMessage::kQuestDetailsChanged, OnQuestUpdate);
		GW::UI::RegisterUIMessageCallback(&target_hook_entry_, GW::UI::UIMessage::kChangeTarget, OnTargetChanged);
		GW::GameThread::Enqueue([] {
			GW::UI::SetPreference(GW::UI::FlagPreference::AutoTargetNPCs, false);
		});
	}

	const char* Name() const override { return "Nameplates"; }

	bool* GetVisiblePtr() override { return &visible_; }

	[[nodiscard]] bool HasSettings() const override { return true; }
	void DrawSettings() override;

	void LoadSettings(const wchar_t* folder) override {
		ToolboxPlugin::LoadSettings(folder);
		#define L_SET(var) LoadSetting(#var, settings_.var)
		L_SET(show_enemies); L_SET(max_range); L_SET(bar_width); L_SET(bar_height); L_SET(border_thickness);
		L_SET(fade_distance_near); L_SET(fade_distance_far); L_SET(fade_opacity_mid); L_SET(fade_opacity_far);
		L_SET(npc_health_threshold); L_SET(allied_health_threshold);
		L_SET(show_summoned_allies);
		L_SET(recolor_quest_nametags); L_SET(recolor_professions);
		L_SET(recolor_enemy_nameplates_by_profession);
		L_SET(show_friendlies); L_SET(friendly_color); L_SET(enemy_color); L_SET(quest_color); L_SET(target_border_color); L_SET(border_color);
		L_SET(color_by_boss); L_SET(boss_color);
		L_SET(fade_enemies_by_range); L_SET(color_nameplate_text_by_combat); L_SET(combat_text_color);
		LoadSetting("visible", visible_);
		#undef L_SET

		for (size_t i = 0; i < 2; ++i) {
			const std::string prefix = "priority" + std::to_string(i + 1);
			LoadSetting((prefix + "_raw").c_str(), settings_.priorities[i].raw);
			LoadSetting((prefix + "_color").c_str(), settings_.priorities[i].color);
		}
		for (size_t i = 1; i < settings_.profession_colors.size(); ++i) {
			const std::string prefix = "profession" + std::to_string(i);
			LoadSetting((prefix + "_enabled").c_str(), settings_.profession_colors[i].enabled);
			LoadSetting((prefix + "_color").c_str(), settings_.profession_colors[i].color);
		}
		RefreshPriorityBuffersAndLists();
	}

	void SaveSettings(const wchar_t* folder) override {
		#define S_SET(var) SaveSetting(#var, settings_.var)
		S_SET(show_enemies); S_SET(max_range); S_SET(bar_width); S_SET(bar_height); S_SET(border_thickness);
		S_SET(fade_distance_near); S_SET(fade_distance_far); S_SET(fade_opacity_mid); S_SET(fade_opacity_far);
		S_SET(npc_health_threshold); S_SET(allied_health_threshold);
		S_SET(show_summoned_allies);
		S_SET(recolor_quest_nametags); S_SET(recolor_professions);
		S_SET(recolor_enemy_nameplates_by_profession);
		S_SET(show_friendlies); S_SET(friendly_color); S_SET(enemy_color); S_SET(quest_color); S_SET(target_border_color); S_SET(border_color);
		S_SET(color_by_boss); S_SET(boss_color);
		S_SET(fade_enemies_by_range); S_SET(color_nameplate_text_by_combat); S_SET(combat_text_color);
		SaveSetting("visible", visible_);
		#undef S_SET

		for (size_t i = 0; i < 2; ++i) {
			const std::string prefix = "priority" + std::to_string(i + 1);
			SaveSetting((prefix + "_raw").c_str(), settings_.priorities[i].raw);
			SaveSetting((prefix + "_color").c_str(), settings_.priorities[i].color);
		}
		for (size_t i = 1; i < settings_.profession_colors.size(); ++i) {
			const std::string prefix = "profession" + std::to_string(i);
			SaveSetting((prefix + "_enabled").c_str(), settings_.profession_colors[i].enabled);
			SaveSetting((prefix + "_color").c_str(), settings_.profession_colors[i].color);
		}
		ToolboxPlugin::SaveSettings(folder);
	}

	bool CanTerminate() override { return true; }

	void Terminate() override {
		GW::UI::RemoveUIMessageCallback(&nametag_hook_entry_);
		GW::UI::RemoveUIMessageCallback(&quest_hook_entry_);
		GW::UI::RemoveUIMessageCallback(&target_hook_entry_);
	}

	void Draw(IDirect3DDevice9* ) override { DrawNameplates(); }

private:
	NameplateSettings settings_;
	bool visible_ = true;
	std::optional<bool> last_recolor_professions_state_;
	std::optional<bool> last_recolor_quest_state_;
	std::optional<bool> last_recolor_enemy_profession_state_;
	std::optional<bool> last_show_enemies_state_;
	int last_quest_count_ = -1;
	GW::HookEntry nametag_hook_entry_;
	GW::HookEntry quest_hook_entry_;
	GW::HookEntry target_hook_entry_;

	AgentNameCache name_cache_;
	StackYSmoother stack_y_smoother_;

	struct PriorityState {
		char buf[512] = {};
		std::vector<std::wstring> names;
	};
	std::array<PriorityState, 2> priority_states_;

	static constexpr uint64_t kDiscoveryIntervalMs = 150;
	static constexpr float kNameplateFontSize = 18.f;
	static constexpr float kStackSmoothing = 0.05f;
	static constexpr float kBgTintAmount = 0.3f;
	static constexpr float kBgOpacity = 1.0f;

	[[nodiscard]] static ImU32 TintedBackground(ImU32 color) {
		const ImVec4 c4 = ImGui::ColorConvertU32ToFloat4(color);
		const ImVec4 bg4(c4.x * kBgTintAmount, c4.y * kBgTintAmount, c4.z * kBgTintAmount, kBgOpacity);
		return ImGui::ColorConvertFloat4ToU32(bg4);
	}
	static constexpr float kZNear = 46.875f;
	static constexpr float kZFar  = 48000.f;

	[[nodiscard]] float GetRangeOpacityMultiplier(float dist_sq) const {
		const float near_sq = settings_.fade_distance_near * settings_.fade_distance_near;
		const float far_sq = settings_.fade_distance_far * settings_.fade_distance_far;
		if (dist_sq <= near_sq) return 1.00f;
		if (dist_sq <= far_sq) return settings_.fade_opacity_mid;
		return settings_.fade_opacity_far;
	}

	void RefreshPriorityBuffersAndLists() {
		for (size_t i = 0; i < 2; ++i) {
			strncpy_s(priority_states_[i].buf, 512, settings_.priorities[i].raw.c_str(), 511);
			priority_states_[i].names = ParseSemicolonNameList(settings_.priorities[i].raw);
		}
	}

	[[nodiscard]] std::optional<ImU32> GetPriorityColor(const std::wstring& name_lower, const std::vector<std::wstring>& words) const {
		if (!name_lower.empty()
			&& std::binary_search(priority_states_[0].names.begin(), priority_states_[0].names.end(), name_lower)) {
			return settings_.priorities[0].color;
		}
		for (const auto& word : words) {
			if (std::binary_search(priority_states_[1].names.begin(), priority_states_[1].names.end(), word)) {
				return settings_.priorities[1].color;
			}
		}
		return std::nullopt;
	}

	struct PendingBar {
		GW::AgentLiving* living = nullptr;
		ImVec2 screen{}, footprint{};
		const std::wstring* name_lower = nullptr;
		const std::wstring* display = nullptr;
		const std::vector<std::wstring>* words = nullptr;
		GW::Constants::ProfessionByte profession = GW::Constants::ProfessionByte::None;
		bool is_targeted = false, is_in_combat = false;
		float natural_y = 0.f;
		float dist_sq_from_me = -1.f;
	};

	struct PlacedRect { float x_min, x_max, y_min, y_max; };

	std::vector<PendingBar> pending_;
	std::vector<PlacedRect> placed_;
	std::vector<size_t> order_;
	std::vector<uint32_t> discovered_agent_ids_;
	uint64_t last_discovery_tick_ = 0;
	std::optional<GW::Constants::MapID> last_known_map_id_;

	static constexpr int kMaxStackResolutionIterations = 32;

	void ResolveStacking(std::vector<PendingBar>& items) {
		static constexpr float kGap = 2.f;
		static constexpr float kMaxPushMultiplier = 4.f;
		static constexpr float kSortEpsilon = 1.f;

		order_.resize(items.size());
		for (size_t i = 0; i < items.size(); ++i) order_[i] = i;
		std::sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
			const float ya = items[a].screen.y;
			const float yb = items[b].screen.y;
			if (std::fabs(ya - yb) > kSortEpsilon) return ya < yb;
			return items[a].living->agent_id < items[b].living->agent_id;
		});

		placed_.clear();
		placed_.reserve(items.size());

		for (size_t oi : order_) {
			PendingBar& item = items[oi];
			if (item.footprint.x <= 0.f || item.footprint.y <= 0.f) continue;

			const float half_w = item.footprint.x / 2.f;
			const float x_min = item.screen.x - half_w;
			const float x_max = item.screen.x + half_w;
			const float natural_top = item.screen.y;
			const float max_push = item.footprint.y * kMaxPushMultiplier;

			float cur_top = natural_top;
			bool moved = true;
			int iterations = 0;
			while (moved && iterations < kMaxStackResolutionIterations) {
				moved = false;
				++iterations;
				for (const auto& p : placed_) {
					const float y_min = cur_top;
					const float y_max = cur_top + item.footprint.y;
					const bool overlap_x = x_min < p.x_max && x_max > p.x_min;
					const bool overlap_y = y_min < p.y_max && y_max > p.y_min;
					if (overlap_x && overlap_y) {
						const float candidate_top = p.y_min - item.footprint.y - kGap;
						if (natural_top - candidate_top > max_push) {
							moved = false;
							break;
						}
						cur_top = candidate_top;
						moved = true;
					}
				}
			}

			item.screen.y += (cur_top - natural_top);
			placed_.push_back({x_min, x_max, cur_top, cur_top + item.footprint.y});
		}
	}

	void DrawNameplates() {
		GW::AgentArray* agents = GW::Agents::GetAgentArray();
		if (!agents || !agents->valid()) return;

		GW::AgentLiving* me = GW::Agents::GetControlledCharacter();
		GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();
		const bool in_outpost = GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost;
		const bool left_clicked_this_frame = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

		const GW::Constants::MapID current_map_id = GW::Map::GetMapID();
		if (!last_known_map_id_.has_value() || *last_known_map_id_ != current_map_id) {
			last_known_map_id_ = current_map_id;
			discovered_agent_ids_.clear();
			last_discovery_tick_ = 0;
		}

		RefreshAllNametagsOnChange(last_recolor_professions_state_, settings_.recolor_professions);
		RefreshAllNametagsOnChange(last_recolor_quest_state_, settings_.recolor_quest_nametags, true);
		RefreshAllNametagsOnChange(last_recolor_enemy_profession_state_, settings_.recolor_enemy_nameplates_by_profession);
		RefreshAllNametagsOnChange(last_show_enemies_state_, settings_.show_enemies);

		if (const auto quest_log = GW::QuestMgr::GetQuestLog()) {
			const int quest_count = static_cast<int>(quest_log->size());
			if (last_quest_count_ != -1 && last_quest_count_ != quest_count) {
				RefreshAllNametags();
				RefreshTargetedNametagViaRetarget();
			}
			last_quest_count_ = quest_count;
		}

		if (in_outpost || (!settings_.show_enemies && !settings_.show_friendlies)) return;

		const uint64_t now = GetTickCount64();
		if (now - last_discovery_tick_ >= kDiscoveryIntervalMs) {
			last_discovery_tick_ = now;
			DiscoverQualifyingAgents(agents, me);
		}

		DirectX::XMMATRIX view_proj;
		float viewport_width, viewport_height;
		if (!BuildFrameProjection(view_proj, viewport_width, viewport_height)) return;

		ImFont* font = ImGui::GetFont();

		GatherPendingBars(me, target, view_proj, viewport_width, viewport_height);
		ResolveStacking(pending_);
		ApplyStackSmoothing();

		ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
		const PendingBar* target_pb = nullptr;
		
		for (const auto& pb : pending_) {
			if (pb.is_targeted) {
				target_pb = &pb;
				continue;
			}
			DrawBar(draw_list, pb, font, left_clicked_this_frame);
		}
		
		if (target_pb) {
			DrawBar(draw_list, *target_pb, font, left_clicked_this_frame);
		}

		name_cache_.MaybePrune();
		stack_y_smoother_.MaybePrune();
	}

	void DiscoverQualifyingAgents(GW::AgentArray* agents, GW::AgentLiving* me) {
		discovered_agent_ids_.clear();
		const float max_range_sq = settings_.max_range * settings_.max_range;

		for (GW::Agent* agent : *agents) {
			if (!agent) continue;
			if (!agent->GetIsLivingType()) continue;

			GW::AgentLiving* living = agent->GetAsAgentLiving();
			if (!living) continue;

			if (living->GetIsDead()) continue;
			if (me && living->agent_id == me->agent_id) continue;
			float dist_sq = -1.f;
			if (!WithinRange(living, me, max_range_sq, dist_sq)) continue;
			if (IsMinipet(living->player_number)) continue;

			if (!settings_.show_summoned_allies
				&& (living->allegiance == GW::Constants::Allegiance::Spirit_Pet
					|| living->allegiance == GW::Constants::Allegiance::Minion)) continue;

			if (!ShouldShowAllegiance(living->allegiance)) continue;

			const bool is_npc = living->allegiance == GW::Constants::Allegiance::Neutral
				|| living->allegiance == GW::Constants::Allegiance::Npc_Minipet;
			const bool is_allied = living->allegiance == GW::Constants::Allegiance::Ally_NonAttackable
				|| living->allegiance == GW::Constants::Allegiance::Spirit_Pet
				|| living->allegiance == GW::Constants::Allegiance::Minion;

			if (is_npc) {
				if (living->hp * 100.f > settings_.npc_health_threshold) continue;
			}
			else if (is_allied) {
				if (living->hp * 100.f > settings_.allied_health_threshold) continue;
			}

			discovered_agent_ids_.push_back(living->agent_id);
		}
	}

	void GatherPendingBars(GW::AgentLiving* me, GW::AgentLiving* target,
							const DirectX::XMMATRIX& view_proj,
							float viewport_width, float viewport_height) {
		pending_.clear();
		const float max_range_sq = settings_.max_range * settings_.max_range;

		for (size_t i = 0; i < discovered_agent_ids_.size(); ) {
			const uint32_t agent_id = discovered_agent_ids_[i];
			GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
			GW::AgentLiving* living = agent ? agent->GetAsAgentLiving() : nullptr;
			if (!living || living->GetIsDead()) {
				discovered_agent_ids_[i] = discovered_agent_ids_.back();
				discovered_agent_ids_.pop_back();
				continue;
			}

			float dist_sq = -1.f;
			if (!WithinRange(living, me, max_range_sq, dist_sq)) {
				discovered_agent_ids_[i] = discovered_agent_ids_.back();
				discovered_agent_ids_.pop_back();
				continue;
			}

			ImVec2 screen;
			if (!WorldToScreen(living, view_proj, viewport_width, viewport_height, screen)) {
				++i;
				continue;
			}

			const auto name_lookup = name_cache_.Get(living);

			PendingBar pb;
			pb.living = living;
			pb.screen = screen;
			pb.natural_y = screen.y;
			pb.name_lower = name_lookup.lower;
			pb.display = name_lookup.display;
			pb.words = name_lookup.words;
			pb.profession = name_lookup.profession;
			pb.is_targeted = target && living->agent_id == target->agent_id;
			pb.dist_sq_from_me = dist_sq;

			if (living->GetInCombatStance()) {
				pb.is_in_combat = true;
			}
			else if (me && living->GetIsMoving()) {
				const float dx = living->pos.x - me->pos.x;
				const float dy = living->pos.y - me->pos.y;
				pb.is_in_combat = (dx * dx + dy * dy) <= GW::Constants::SqrRange::Earshot;
			}
			else {
				pb.is_in_combat = false;
			}
			pb.footprint = ImVec2(settings_.bar_width, settings_.bar_height);

			pending_.push_back(std::move(pb));
			++i;
		}
	}

	void ApplyStackSmoothing() {
		for (auto& pb : pending_) {
			const float target_offset = pb.screen.y - pb.natural_y;
			const float smoothed_offset = stack_y_smoother_.Update(pb.living->agent_id, target_offset, kStackSmoothing);
			pb.screen.y = pb.natural_y + smoothed_offset;
		}
	}

	[[nodiscard]] bool ShouldShowAllegiance(GW::Constants::Allegiance allegiance) const {
		switch (allegiance) {
			case GW::Constants::Allegiance::Enemy:
				return settings_.show_enemies;
			case GW::Constants::Allegiance::Ally_NonAttackable:
			case GW::Constants::Allegiance::Spirit_Pet:
			case GW::Constants::Allegiance::Minion:
			case GW::Constants::Allegiance::Neutral:
			case GW::Constants::Allegiance::Npc_Minipet:
				return settings_.show_friendlies;
			default:
				return false;
		}
	}

	[[nodiscard]] bool WithinRange(const GW::AgentLiving* living, const GW::Agent* me, float max_range_sq, float& out_dist_sq) const {
		if (!me) { out_dist_sq = -1.f; return true; }
		const float dx = living->pos.x - me->pos.x;
		const float dy = living->pos.y - me->pos.y;
		out_dist_sq = dx * dx + dy * dy;
		return out_dist_sq <= max_range_sq;
	}

	[[nodiscard]] bool BuildFrameProjection(DirectX::XMMATRIX& out_view_proj,
							  float& out_viewport_width, float& out_viewport_height) const {
		const auto cam = GW::CameraMgr::GetCamera();
		if (!cam) return false;

		using namespace DirectX;
		const XMVECTOR eye_pos = XMVectorSet(cam->position.x, cam->position.y, cam->position.z, 0.f);
		const XMVECTOR look_at = XMVectorSet(cam->look_at_target.x, cam->look_at_target.y, cam->look_at_target.z, 0.f);
		const XMVECTOR up      = XMVectorSet(0.f, 0.f, -1.f, 0.f);

		const XMMATRIX view = XMMatrixLookAtLH(eye_pos, look_at, up);

		out_viewport_width  = static_cast<float>(GW::Render::GetViewportWidth());
		out_viewport_height = static_cast<float>(GW::Render::GetViewportHeight());
		if (out_viewport_width <= 0.f || out_viewport_height <= 0.f) return false;

		const float fov = GW::Render::GetFieldOfView();
		const float aspect = out_viewport_width / out_viewport_height;
		const XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspect, kZNear, kZFar);

		out_view_proj = view * proj;
		return true;
	}

	[[nodiscard]] bool WorldToScreen(const GW::AgentLiving* living, const DirectX::XMMATRIX& view_proj,
					   float viewport_width, float viewport_height, ImVec2& out) const {
		using namespace DirectX;

		const XMVECTOR world_pos = XMVectorSet(living->pos.x, living->pos.y, living->name_tag_z, 1.f);
		const XMVECTOR clip_pos = XMVector4Transform(world_pos, view_proj);
		float clip_arr[4];
		XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(clip_arr), clip_pos);

		if (clip_arr[3] <= kZNear) return false;

		const float inv_w = 1.f / clip_arr[3];
		out.x = ((clip_arr[0] * inv_w) * 0.5f + 0.5f) * viewport_width;
		out.y = (1.f - ((clip_arr[1] * inv_w) * 0.5f + 0.5f)) * viewport_height;
		return true;
	}

	void CheckClickToTarget(const ImVec2& rect_min, const ImVec2& rect_max, const GW::AgentLiving* living, bool left_clicked_this_frame) const {
		if (!left_clicked_this_frame) return;
		if (ImGui::IsMouseHoveringRect(rect_min, rect_max, false)) {
			const uint32_t agent_id = living->agent_id;
			GW::GameThread::Enqueue([agent_id] {
				GW::Agents::ChangeTarget(agent_id);
			});
		}
	}

	static void ShowHelpMarker(const char* help) {
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
	}

	static void RightAlignNextItem(float item_width) {
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - item_width);
	}

	static void DrawCheckboxWithColorRightAligned(const char* label, bool& toggle, uint32_t& color, const char* color_id) {
		ImGui::Checkbox(label, &toggle);
		RightAlignNextItem(ImGui::GetFrameHeight());
		ImVec4 color_vec = ImGui::ColorConvertU32ToFloat4(color);
		if (ImGui::ColorEdit3(color_id, &color_vec.x, ImGuiColorEditFlags_NoInputs)) {
			color = ImGui::ColorConvertFloat4ToU32(color_vec);
		}
	}

	void DrawProfessionCell(size_t index) {
		ProfessionColorConfig& cfg = settings_.profession_colors[index];
		ImGui::PushID(static_cast<int>(index));
		const bool was_enabled = cfg.enabled;
		if (!was_enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
		ImGui::Checkbox("##enabled", &cfg.enabled);
		ImGui::SameLine();
		ImVec4 color_vec = ImGui::ColorConvertU32ToFloat4(cfg.color);
		if (ImGui::ColorEdit3("##color", &color_vec.x, ImGuiColorEditFlags_NoInputs)) {
			cfg.color = ImGui::ColorConvertFloat4ToU32(color_vec);
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(GW::Constants::GetProfessionAcronym(static_cast<GW::Constants::Profession>(index)));
		if (!was_enabled) ImGui::PopStyleVar();
		ImGui::PopID();
	}

	void DrawBar(ImDrawList* draw_list, const PendingBar& pb, ImFont* font, bool left_clicked_this_frame) {
		const GW::AgentLiving* living = pb.living;

		const float hp_pct = std::clamp(living->hp, 0.f, 1.f);
		const float bar_width = settings_.bar_width;
		const float bar_height = settings_.bar_height;

		const ImVec2 top_left(pb.screen.x - bar_width / 2.f, pb.screen.y);
		const ImVec2 bottom_right(top_left.x + bar_width, top_left.y + bar_height);
		const ImVec2 fill_bottom_right(top_left.x + bar_width * hp_pct, bottom_right.y);

		const auto priority_color = GetPriorityColor(*pb.name_lower, *pb.words);
		ImU32 fill_color;
		if (priority_color) {
			fill_color = *priority_color;
		}
		else if (settings_.color_by_boss && living->GetHasBossGlow()) {
			fill_color = settings_.boss_color;
		}
		else {
			std::optional<ImU32> profession_fill_color;
			if (settings_.recolor_enemy_nameplates_by_profession && living->allegiance == GW::Constants::Allegiance::Enemy) {
				profession_fill_color = TryGetProfessionColor(pb.profession);
			}
			fill_color = profession_fill_color ? *profession_fill_color : ColorFor(living->allegiance);
		}

		const ImU32 bg_color = TintedBackground(fill_color);
		const ImU32 border_color = pb.is_targeted ? settings_.target_border_color : settings_.border_color;

		float opacity_mult = 1.f;
		if (settings_.fade_enemies_by_range && living->allegiance == GW::Constants::Allegiance::Enemy && !pb.is_targeted) {
			opacity_mult = GetRangeOpacityMultiplier(pb.dist_sq_from_me);
		}

		draw_list->AddRectFilled(top_left, bottom_right, ScaleAlpha(bg_color, opacity_mult));
		draw_list->AddRectFilled(top_left, fill_bottom_right, ScaleAlpha(fill_color, opacity_mult));
		draw_list->AddRect(top_left, bottom_right, ScaleAlpha(border_color, opacity_mult), 0.f, 0, settings_.border_thickness);
		DrawStatusTriangles(draw_list, bottom_right.x - 8.f, top_left.y + bar_height / 2.f, living, opacity_mult);
		CheckClickToTarget(top_left, bottom_right, living, left_clicked_this_frame);

		if (!pb.display->empty() && font) {
			const float font_size = kNameplateFontSize;
			constexpr float kPadding = 6.f;
			const float max_text_width = bar_width * 0.8f - kPadding;

			if (max_text_width > 0.f) {
				const std::string& clipped_utf8 = name_cache_.GetTruncated(living->agent_id, font, font_size, max_text_width);
				const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, clipped_utf8.c_str());

				const float text_x = top_left.x + kPadding;
				const float text_y = top_left.y + (bar_height - text_size.y) / 2.f;

				static constexpr ImU32 kNormalTextColor = IM_COL32(255, 255, 255, 255);
				const bool is_enemy_in_combat = settings_.color_nameplate_text_by_combat
					&& living->allegiance == GW::Constants::Allegiance::Enemy && pb.is_in_combat;
				const ImU32 name_text_color = is_enemy_in_combat ? settings_.combat_text_color : kNormalTextColor;
				DrawOutlinedText(draw_list, font, font_size, ImVec2(text_x, text_y), name_text_color, clipped_utf8, opacity_mult);
			}
		}
	}

	[[nodiscard]] ImU32 ColorFor(GW::Constants::Allegiance allegiance) const {
		switch (allegiance) {
			case GW::Constants::Allegiance::Enemy:
				return settings_.enemy_color;
			case GW::Constants::Allegiance::Spirit_Pet:
			case GW::Constants::Allegiance::Minion:
				return IM_COL32(40, 200, 60, 255);
			default:
				return settings_.friendly_color;
		}
	}

	[[nodiscard]] std::optional<ImU32> TryGetProfessionColor(GW::Constants::ProfessionByte prof) const {
		const size_t index = static_cast<size_t>(prof);
		if (index == 0 || index >= settings_.profession_colors.size()) return std::nullopt;
		const auto& cfg = settings_.profession_colors[index];
		return cfg.enabled ? std::optional<ImU32>(cfg.color) : std::nullopt;
	}

	static void FlashFlagPreference(GW::UI::FlagPreference pref) {
		const bool current = GW::UI::GetPreference(pref);
		GW::UI::SetPreference(pref, !current);
		GW::UI::SetPreference(pref, current);
	}

	static void RefreshAllNametags() {
		GW::GameThread::Enqueue([] {
			FlashFlagPreference(GW::UI::FlagPreference::AlwaysShowAllyNames);
			FlashFlagPreference(GW::UI::FlagPreference::AlwaysShowFoeNames);
		});
	}

	static void RefreshAllNametagsOnChange(std::optional<bool>& last_state, bool current_state, bool also_retarget = false) {
		if (last_state.has_value() && *last_state != current_state) {
			RefreshAllNametags();
			if (also_retarget) RefreshTargetedNametagViaRetarget();
		}
		last_state = current_state;
	}

	static void OnAgentNameTag(GW::HookStatus* status, GW::UI::UIMessage msgid, void* wParam, void*) {
		if (msgid != GW::UI::UIMessage::kShowAgentNameTag && msgid != GW::UI::UIMessage::kSetAgentNameTagAttribs) return;
		auto* self = static_cast<NameplatesPlugin*>(ToolboxPluginInstance());
		self->HandleAgentNameTag(status, static_cast<GW::UI::AgentNameTagInfo*>(wParam));
	}

	static void RefreshTargetedNametagViaRetarget() {
		GW::GameThread::Enqueue([] {
			const uint32_t target_id = GW::Agents::GetTargetId();
			if (target_id == 0) return;
			GW::Agent* target_agent = GW::Agents::GetAgentByID(target_id);
			GW::AgentLiving* target_living = target_agent ? target_agent->GetAsAgentLiving() : nullptr;
			if (!target_living || target_living->allegiance == GW::Constants::Allegiance::Enemy) return;
			GW::Agents::ChangeTarget(0u);
			GW::Agents::ChangeTarget(target_id);
		});
	}

	static void OnQuestUpdate(GW::HookStatus*, GW::UI::UIMessage msgid, void*, void*) {
		if (msgid != GW::UI::UIMessage::kQuestAdded
			&& msgid != GW::UI::UIMessage::kQuestDetailsChanged) return;
		RefreshAllNametags();
		RefreshTargetedNametagViaRetarget();
	}

	static void OnTargetChanged(GW::HookStatus*, GW::UI::UIMessage msgid, void* wParam, void*) {
		if (msgid != GW::UI::UIMessage::kChangeTarget) return;
		const auto* packet = static_cast<GW::UI::UIPacket::kChangeTarget*>(wParam);
		if (!packet) return;
		if (!packet->has_evaluated_target_changed && !packet->has_auto_target_changed && !packet->has_manual_target_changed) return;
		RefreshAllNametags();
	}

	void HandleAgentNameTag(GW::HookStatus* status, GW::UI::AgentNameTagInfo* tag) {
		if (!tag) return;

		GW::Agent* agent = GW::Agents::GetAgentByID(tag->agent_id);
		GW::AgentLiving* living = agent ? agent->GetAsAgentLiving() : nullptr;
		if (!living) return;

		const bool is_enemy = living->allegiance == GW::Constants::Allegiance::Enemy;

		if (is_enemy && settings_.show_enemies && status) {
			status->blocked = true;
			return;
		}

		if (is_enemy) {
			const auto name_lookup = name_cache_.Get(living);
			if (const auto color = GetPriorityColor(*name_lookup.lower, *name_lookup.words)) {
				tag->text_color = *color;
				return;
			}
			if (settings_.recolor_enemy_nameplates_by_profession) {
				if (const auto color = TryGetProfessionColor(name_lookup.profession)) {
					tag->text_color = *color;
				}
			}
			return;
		}

		if (settings_.recolor_quest_nametags && living->GetHasQuest()) {
			tag->text_color = settings_.quest_color;
			return;
		}

		if (settings_.recolor_professions
			&& living->allegiance == GW::Constants::Allegiance::Ally_NonAttackable) {
			if (const auto color = TryGetProfessionColor(GetAgentProfession(living))) {
				tag->text_color = *color;
			}
		}
	}

	void DrawPriorityInput(const char* input_id, const char* hint, const char* color_id, uint32_t& color, char* buf, std::string& raw, std::vector<std::wstring>& names) {
		ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x));
		if (ImGui::InputTextWithHint(input_id, hint, buf, 512)) {
			raw = buf;
			names = ParseSemicolonNameList(raw);
		}
		ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImVec4 color_vec = ImGui::ColorConvertU32ToFloat4(color);
		if (ImGui::ColorEdit3(color_id, &color_vec.x, ImGuiColorEditFlags_NoInputs)) {
			color = ImGui::ColorConvertFloat4ToU32(color_vec);
		}
	}

	void DrawSettingsInternal() {
		ImGui::SeparatorText("Explorable Areas");

		DrawCheckboxWithColorRightAligned("Show enemy nameplates", settings_.show_enemies, settings_.enemy_color, "##color_show_enemies");
		DrawCheckboxWithColorRightAligned("Show friendly nameplates", settings_.show_friendlies, settings_.friendly_color, "##color_friendly");

		ImGui::Checkbox("Show summoned friendly nameplates", &settings_.show_summoned_allies);
		ShowHelpMarker("Show spirits, minions & summoning stones, minipets are always hidden");

		ImGui::Checkbox("Color nameplate text by combat status", &settings_.color_nameplate_text_by_combat);
		ShowHelpMarker("Enemies that are in-combat stance regardless of distance have their name colored, \nenemies within earshot and are moving are also colored this way");
		RightAlignNextItem(ImGui::GetFrameHeight());
		ImVec4 combat_text_color_vec = ImGui::ColorConvertU32ToFloat4(settings_.combat_text_color);
		if (ImGui::ColorEdit3("##color_combat_text", &combat_text_color_vec.x, ImGuiColorEditFlags_NoInputs)) {
			settings_.combat_text_color = ImGui::ColorConvertFloat4ToU32(combat_text_color_vec);
		}

		ImGui::Checkbox("Color by boss", &settings_.color_by_boss);
		ShowHelpMarker("Overrides other nameplate coloring (except Priority) for agents with the boss glow");
		RightAlignNextItem(ImGui::GetFrameHeight());
		ImVec4 boss_color_vec = ImGui::ColorConvertU32ToFloat4(settings_.boss_color);
		if (ImGui::ColorEdit3("##color_by_boss", &boss_color_vec.x, ImGuiColorEditFlags_NoInputs)) {
			settings_.boss_color = ImGui::ColorConvertFloat4ToU32(boss_color_vec);
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Priority nameplate coloring");
		ShowHelpMarker("Priority 1 matches full names exactly, semicolon-separated. \nPriority 2 matches whole words only, e.g. \"Monk\" matches \"Charr Monk\" but not \"Charrmonk\".");

		static constexpr std::array<const char*, 2> kPriorityHints = {
			"Keeper of Souls; Kournan Taskmaster",
			"monk; healer; priest; mender"
		};
		static constexpr std::array<const char*, 2> kPriorityInputIds = { "##priority_input_0", "##priority_input_1" };
		static constexpr std::array<const char*, 2> kPriorityColorIds = { "##priority_color_0", "##priority_color_1" };
		for (size_t i = 0; i < 2; ++i) {
			DrawPriorityInput(kPriorityInputIds[i], kPriorityHints[i], kPriorityColorIds[i], settings_.priorities[i].color, priority_states_[i].buf, settings_.priorities[i].raw, priority_states_[i].names);
		}

		ImGui::Spacing();
		const bool alpha_enabled = settings_.fade_enemies_by_range;
		ImGui::Checkbox("Use nameplate alpha", &settings_.fade_enemies_by_range);
		ShowHelpMarker("Fades enemy nameplates in steps based on distance, using the thresholds and opacity below.");

		if (!alpha_enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);

		ImGui::TextUnformatted("Fade distance thresholds");
		float fade_distances[2] = { settings_.fade_distance_near, settings_.fade_distance_far };
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat2("##fade_distance_thresholds", fade_distances, 5.f, 0.f, 5000.f, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
			if (fade_distances[1] < fade_distances[0]) fade_distances[1] = fade_distances[0];
			settings_.fade_distance_near = fade_distances[0];
			settings_.fade_distance_far = fade_distances[1];
		}

		ImGui::TextUnformatted("Fade opacity");
		float fade_opacities[2] = { settings_.fade_opacity_mid * 100.f, settings_.fade_opacity_far * 100.f };
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat2("##fade_opacity", fade_opacities, 0.5f, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
			settings_.fade_opacity_mid = fade_opacities[0] / 100.f;
			settings_.fade_opacity_far = fade_opacities[1] / 100.f;
		}

		char fade_summary[160];
		snprintf(fade_summary, sizeof(fade_summary), "0-%.0f: 100%%  \xe2\x80\xa2  %.0f-%.0f: %.0f%%  \xe2\x80\xa2  %.0f+: %.0f%%",
			settings_.fade_distance_near,
			settings_.fade_distance_near, settings_.fade_distance_far, settings_.fade_opacity_mid * 100.f,
			settings_.fade_distance_far, settings_.fade_opacity_far * 100.f);
		const float fade_summary_width = ImGui::CalcTextSize(fade_summary).x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (ImGui::GetContentRegionAvail().x - fade_summary_width) / 2.f));
		ImGui::TextUnformatted(fade_summary);

		if (!alpha_enabled) ImGui::PopStyleVar();

		ImGui::Separator();

		ImGui::TextUnformatted("Nameplate border thickness");
		const float border_thickness_width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x) / 2.f;
		ImGui::SetNextItemWidth(border_thickness_width);
		ImGui::DragFloat("##border_thickness", &settings_.border_thickness, 0.02f, 1.0f, 3.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

		const float swatch_w = ImGui::GetFrameHeight();
		const float default_label_w = ImGui::CalcTextSize("Default Color").x;
		const float target_label_w = ImGui::CalcTextSize("Target Color").x;
		const float inner_spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float border_group_width = swatch_w + inner_spacing + default_label_w + inner_spacing * 2.f + swatch_w + inner_spacing + target_label_w;
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - border_group_width);

		ImVec4 border_color_vec = ImGui::ColorConvertU32ToFloat4(settings_.border_color);
		if (ImGui::ColorEdit3("##color_border", &border_color_vec.x, ImGuiColorEditFlags_NoInputs)) {
			settings_.border_color = ImGui::ColorConvertFloat4ToU32(border_color_vec);
		}
		ImGui::SameLine(0.f, inner_spacing);
		ImGui::TextUnformatted("Default Color");
		ImGui::SameLine(0.f, inner_spacing * 2.f);
		ImVec4 target_border_color_vec = ImGui::ColorConvertU32ToFloat4(settings_.target_border_color);
		if (ImGui::ColorEdit3("##color_target_border", &target_border_color_vec.x, ImGuiColorEditFlags_NoInputs)) {
			settings_.target_border_color = ImGui::ColorConvertFloat4ToU32(target_border_color_vec);
		}
		ImGui::SameLine(0.f, inner_spacing);
		ImGui::TextUnformatted("Target Color");

		ImGui::TextUnformatted("NPC & ally visibility threshold");
		ShowHelpMarker("0 = off, 100 = on");
		int thresholds[2] = {
			static_cast<int>(std::lround(settings_.npc_health_threshold)),
			static_cast<int>(std::lround(settings_.allied_health_threshold))
		};
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragInt2("##npc_ally_threshold", thresholds, 1.f, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp)) {
			settings_.npc_health_threshold = static_cast<float>(thresholds[0]);
			settings_.allied_health_threshold = static_cast<float>(thresholds[1]);
		}

		ImGui::TextUnformatted("Max range");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##max_range", &settings_.max_range, 5.f, 500.f, 5000.f, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
			settings_.max_range = std::round(settings_.max_range);
		}

		ImGui::TextUnformatted("Bar width & height");
		const float half_width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x) / 2.f;
		ImGui::PushItemWidth(half_width);
		if (ImGui::DragFloat("##bar_width", &settings_.bar_width, 1.f, 50.f, 300.f, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
			settings_.bar_width = std::round(settings_.bar_width);
		}
		ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
		if (ImGui::DragFloat("##bar_height", &settings_.bar_height, 0.2f, 15.f, 30.f, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
			settings_.bar_height = std::round(settings_.bar_height);
		}
		ImGui::PopItemWidth();

		ImGui::SeparatorText("All Areas");

		DrawCheckboxWithColorRightAligned("Color quest-giver nametags", settings_.recolor_quest_nametags, settings_.quest_color, "##color_quest");

		ImGui::Checkbox("Color ally nametags by profession", &settings_.recolor_professions);

		ImGui::Checkbox("Color enemy nameplates by profession", &settings_.recolor_enemy_nameplates_by_profession);
		ShowHelpMarker("Works on Players/Heroes/Henchmen (nametags) and enemy nameplates. Uses the profession colors below - if a monster's profession can't be determined, its normal color is used instead.");

		ImGui::TextUnformatted("Profession colors");
		ShowHelpMarker("Used by 'Color ally nametags by profession' and 'Color enemy nameplates by profession' above. Defaults match the classic ally-nametag profession colors.");

		if (ImGui::BeginTable("##profession_colors_table", 5)) {
			for (int c = 0; c < 5; ++c) {
				ImGui::TableSetupColumn("##pcol", ImGuiTableColumnFlags_WidthStretch);
			}
			for (size_t row = 0; row < 2; ++row) {
				ImGui::TableNextRow();
				for (size_t col = 0; col < 5; ++col) {
					ImGui::TableNextColumn();
					DrawProfessionCell(row * 5 + col + 1);
				}
			}
			ImGui::EndTable();
		}
	}
};

void NameplatesPlugin::DrawSettings() {
	ToolboxPlugin::DrawSettings();
	DrawSettingsInternal();
}

DLLAPI ToolboxPlugin* ToolboxPluginInstance() {
	static NameplatesPlugin instance;
	return &instance;
}
