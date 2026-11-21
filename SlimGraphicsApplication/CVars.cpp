#include "CVars.h"
#include <imgui.h>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <cctype>

namespace
{
	struct Registry
	{
		std::vector<CVarBase*> cvars;
		uint32_t change_counts[32] = {};
	};

	// Function local so cvars constructed during static initialisation of any translation unit find it
	Registry& registry()
	{
		static Registry r;
		return r;
	}

	bool contains_case_insensitive(const std::string& text, const char* filter)
	{
		if (filter == nullptr || filter[0] == '\0')
		{
			return true;
		}
		const std::string f(filter);
		auto it = std::search(text.begin(), text.end(), f.begin(), f.end(), [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
		return it != text.end();
	}

	// Description tooltip and right click reset shared by every cvar widget
	bool item_extras(CVarBase& cvar)
	{
		bool changed = false;
		if (ImGui::IsItemHovered() && !cvar.get_description().empty())
		{
			ImGui::SetTooltip("%s\n(%s)", cvar.get_description().c_str(), cvar.get_name().c_str());
		}
		if (ImGui::BeginPopupContextItem(cvar.get_name().c_str()))
		{
			if (ImGui::MenuItem("Reset to default"))
			{
				cvar.reset();
				changed = true;
			}
			ImGui::EndPopup();
		}
		return changed;
	}

	// The label shown: the last part of the name
	const char* short_label(const std::string& name)
	{
		const size_t dot = name.find_last_of('.');
		return dot == std::string::npos ? name.c_str() : name.c_str() + dot + 1;
	}
}

CVarBase::CVarBase(const char* name_, const char* category_, const char* description_, uint32_t flags_) : name(name_), category(category_), description(description_), flags(flags_)
{
	registry().cvars.push_back(this);
}

void CVarBase::changed()
{
	for (uint32_t bit = 0; bit < 32; bit++)
	{
		if (flags & (1u << bit))
		{
			registry().change_counts[bit]++;
		}
	}
	registry().change_counts[31]++; // Any change
}

// --- Bool

CVarBool::CVarBool(const char* name, const char* category, const char* description, bool default_value_, uint32_t flags) : CVarBase(name, category, description, flags), value(default_value_), default_value(default_value_) {}

void CVarBool::set(bool v)
{
	if (v != value)
	{
		value = v;
		changed();
	}
}

bool CVarBool::imgui()
{
	bool v = value;
	ImGui::PushID(this);
	bool c = ImGui::Checkbox(short_label(get_name()), &v);
	if (c) set(v);
	c |= item_extras(*this);
	ImGui::PopID();
	return c;
}

bool CVarBool::set_json(const nlohmann::json& v)
{
	if (v.is_boolean()) { set(v.get<bool>()); return true; }
	if (v.is_number_integer()) { set(v.get<int>() != 0); return true; }
	return false;
}

bool CVarBool::set_string(const std::string& v)
{
	if (v == "1" || v == "true") { set(true); return true; }
	if (v == "0" || v == "false") { set(false); return true; }
	return false;
}

// --- Int

CVarInt::CVarInt(const char* name, const char* category, const char* description, int default_value_, int min_value_, int max_value_, uint32_t flags)
	: CVarBase(name, category, description, flags), value(default_value_), default_value(default_value_), min_value(min_value_), max_value(max_value_) {}

CVarInt::CVarInt(const char* name, const char* category, const char* description, int default_value_, std::vector<std::string> options_, uint32_t flags)
	: CVarBase(name, category, description, flags), value(default_value_), default_value(default_value_), min_value(0), max_value(static_cast<int>(options_.size()) - 1), options(std::move(options_)) {}

void CVarInt::set(int v)
{
	v = std::clamp(v, min_value, max_value);
	if (v != value)
	{
		value = v;
		changed();
	}
}

bool CVarInt::imgui()
{
	int v = value;
	bool c = false;
	ImGui::PushID(this);
	if (!options.empty())
	{
		if (ImGui::BeginCombo(short_label(get_name()), options[value].c_str()))
		{
			for (int i = 0; i < (int)options.size(); i++)
			{
				if (ImGui::Selectable(options[i].c_str(), i == value))
				{
					v = i;
					c = true;
				}
			}
			ImGui::EndCombo();
		}
	}
	else
	{
		c = ImGui::SliderInt(short_label(get_name()), &v, min_value, max_value);
	}
	if (c) set(v);
	c |= item_extras(*this);
	ImGui::PopID();
	return c;
}

bool CVarInt::set_json(const nlohmann::json& v)
{
	if (v.is_number_integer()) { set(v.get<int>()); return true; }
	if (v.is_string() && !options.empty())
	{
		auto it = std::find(options.begin(), options.end(), v.get<std::string>());
		if (it == options.end()) return false;
		set(static_cast<int>(it - options.begin()));
		return true;
	}
	return false;
}

nlohmann::json CVarInt::get_json() const
{
	if (!options.empty()) return options[value];
	return value;
}

bool CVarInt::set_string(const std::string& v)
{
	if (!options.empty())
	{
		auto it = std::find(options.begin(), options.end(), v);
		if (it != options.end()) { set(static_cast<int>(it - options.begin())); return true; }
	}
	char* end = nullptr;
	const long parsed = std::strtol(v.c_str(), &end, 10);
	if (end == v.c_str() || *end != '\0') return false;
	set(static_cast<int>(parsed));
	return true;
}

// --- Float

CVarFloat::CVarFloat(const char* name, const char* category, const char* description, float default_value_, float min_value_, float max_value_, uint32_t flags)
	: CVarBase(name, category, description, flags), value(default_value_), default_value(default_value_), min_value(min_value_), max_value(max_value_) {}

void CVarFloat::set(float v)
{
	// Sliders clamp; typed values (ctrl+click, JSON, command line) may go beyond the slider range but stay finite
	if (!(v == v)) return;
	if (v != value)
	{
		value = v;
		changed();
	}
}

bool CVarFloat::imgui()
{
	float v = value;
	ImGui::PushID(this);
	const ImGuiSliderFlags slider_flags = (get_flags() & CVarLogScale) ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None;
	bool c = ImGui::SliderFloat(short_label(get_name()), &v, min_value, max_value, "%.4g", slider_flags);
	if (c) set(v);
	c |= item_extras(*this);
	ImGui::PopID();
	return c;
}

bool CVarFloat::set_json(const nlohmann::json& v)
{
	if (!v.is_number()) return false;
	set(v.get<float>());
	return true;
}

bool CVarFloat::set_string(const std::string& v)
{
	char* end = nullptr;
	const float parsed = std::strtof(v.c_str(), &end);
	if (end == v.c_str() || *end != '\0') return false;
	set(parsed);
	return true;
}

// --- Float3

CVarFloat3::CVarFloat3(const char* name, const char* category, const char* description, float x, float y, float z, bool colour_, uint32_t flags)
	: CVarBase(name, category, description, flags), value{ x, y, z }, default_value{ x, y, z }, colour(colour_) {}

void CVarFloat3::set(float x, float y, float z)
{
	if (x != value[0] || y != value[1] || z != value[2])
	{
		value[0] = x; value[1] = y; value[2] = z;
		changed();
	}
}

bool CVarFloat3::imgui()
{
	float v[3] = { value[0], value[1], value[2] };
	ImGui::PushID(this);
	bool c = colour ? ImGui::ColorEdit3(short_label(get_name()), v, ImGuiColorEditFlags_Float) : ImGui::DragFloat3(short_label(get_name()), v, 0.01f);
	if (c) set(v[0], v[1], v[2]);
	c |= item_extras(*this);
	ImGui::PopID();
	return c;
}

bool CVarFloat3::set_json(const nlohmann::json& v)
{
	if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number()) return false;
	set(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
	return true;
}

bool CVarFloat3::set_string(const std::string& v)
{
	float x, y, z;
	if (sscanf_s(v.c_str(), "%f,%f,%f", &x, &y, &z) != 3) return false;
	set(x, y, z);
	return true;
}

// --- Registry

namespace CVars
{
	const std::vector<CVarBase*>& all()
	{
		return registry().cvars;
	}

	CVarBase* find(const std::string& name)
	{
		for (CVarBase* cvar : registry().cvars)
		{
			if (cvar->get_name() == name) return cvar;
		}
		return nullptr;
	}

	uint32_t get_change_count(uint32_t flag)
	{
		uint32_t bit = 31;
		for (uint32_t i = 0; i < 31; i++)
		{
			if (flag == (1u << i)) bit = i;
		}
		return registry().change_counts[bit];
	}

	bool apply_json(const nlohmann::json& values, std::string& error)
	{
		if (!values.is_object())
		{
			error = "cvar settings must be an object of name: value";
			return false;
		}
		for (const auto& [name, value] : values.items())
		{
			CVarBase* cvar = find(name);
			if (cvar == nullptr)
			{
				error = "unknown cvar '" + name + "'";
				return false;
			}
			if (!cvar->set_json(value))
			{
				error = "cvar '" + name + "' cannot be set to " + value.dump();
				return false;
			}
		}
		return true;
	}

	void reset_all(const std::string& prefix)
	{
		for (CVarBase* cvar : registry().cvars)
		{
			if (cvar->get_name().compare(0, prefix.size(), prefix) == 0) cvar->reset();
		}
	}

	bool apply_assignment(const std::string& assignment, std::string& error)
	{
		const size_t equals = assignment.find('=');
		if (equals == std::string::npos)
		{
			error = "expected name=value, got '" + assignment + "'";
			return false;
		}
		const std::string name = assignment.substr(0, equals);
		CVarBase* cvar = find(name);
		if (cvar == nullptr)
		{
			error = "unknown cvar '" + name + "'";
			return false;
		}
		if (!cvar->set_string(assignment.substr(equals + 1)))
		{
			error = "cvar '" + name + "' cannot be set to '" + assignment.substr(equals + 1) + "'";
			return false;
		}
		return true;
	}

	nlohmann::json changed_json(const std::string& prefix)
	{
		nlohmann::json j = nlohmann::json::object();
		for (CVarBase* cvar : registry().cvars)
		{
			if (cvar->get_name().compare(0, prefix.size(), prefix) == 0 && !cvar->is_default()) j[cvar->get_name()] = cvar->get_json();
		}
		return j;
	}

	void imgui_window(const char* window_name, const std::string& prefix)
	{
		if (ImGui::Begin(window_name))
		{
			static char filter[128] = {};
			ImGui::SetNextItemWidth(-160.0f);
			ImGui::InputTextWithHint("##filter", "Search (name or description)", filter, sizeof(filter));
			ImGui::SameLine();
			if (ImGui::Button("Reset All"))
			{
				reset_all(prefix);
			}
			ImGui::SameLine();
			if (ImGui::Button("Copy JSON"))
			{
				ImGui::SetClipboardText(changed_json(prefix).dump(1, '\t').c_str());
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Copies every changed cvar as JSON (for test plans and -cvar switches)");
			}

			// Categories in order of first appearance
			std::vector<std::string> categories;
			for (CVarBase* cvar : registry().cvars)
			{
				if (cvar->get_name().compare(0, prefix.size(), prefix) != 0) continue;
				if (std::find(categories.begin(), categories.end(), cvar->get_category()) == categories.end()) categories.push_back(cvar->get_category());
			}
			const bool filtering = filter[0] != '\0';
			for (const std::string& category : categories)
			{
				bool any = false;
				for (CVarBase* cvar : registry().cvars)
				{
					if (cvar->get_category() == category && cvar->get_name().compare(0, prefix.size(), prefix) == 0 &&
						(contains_case_insensitive(cvar->get_name(), filter) || contains_case_insensitive(cvar->get_description(), filter)))
					{
						any = true;
						break;
					}
				}
				if (!any) continue;
				if (filtering) ImGui::SetNextItemOpen(true);
				if (ImGui::CollapsingHeader(category.c_str()))
				{
					ImGui::PushItemWidth(-200.0f);
					for (CVarBase* cvar : registry().cvars)
					{
						if (cvar->get_category() != category || cvar->get_name().compare(0, prefix.size(), prefix) != 0) continue;
						if (!contains_case_insensitive(cvar->get_name(), filter) && !contains_case_insensitive(cvar->get_description(), filter)) continue;
						const bool modified = !cvar->is_default();
						if (modified) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
						cvar->imgui();
						if (modified) ImGui::PopStyleColor();
					}
					ImGui::PopItemWidth();
				}
			}
		}
		ImGui::End();
	}
}
