#include "CVars.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace
{
	struct Registry
	{
		std::vector<CVarBase*> cvars;
		uint32_t change_counts[32] = {};	// Per flag bit; the last one counts every change
	};

	// Function local so cvars constructed during static initialisation of any translation unit find it
	Registry& registry()
	{
		static Registry r;
		return r;
	}

	bool has_prefix(const CVarBase& cvar, const std::string& prefix)
	{
		return cvar.get_name().compare(0, prefix.size(), prefix) == 0;
	}

	bool contains_case_insensitive(const std::string& text, const char* filter)
	{
		const std::string f(filter);
		return std::search(text.begin(), text.end(), f.begin(), f.end(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) != text.end();
	}

	// Parses the whole string as a number
	template <typename T>
	bool parse_number(const std::string& text, T (*convert)(const char*, char**), T& out)
	{
		char* end = nullptr;
		out = convert(text.c_str(), &end);
		return end != text.c_str() && *end == '\0';
	}

	long to_long(const char* s, char** end) { return std::strtol(s, end, 10); }
	float to_float(const char* s, char** end) { return std::strtof(s, end); }
}

CVarBase::CVarBase(const char* name_, const char* category_, const char* description_, uint32_t flags_) : name(name_), category(category_), description(description_), flags(flags_)
{
	registry().cvars.push_back(this);
}

void CVarBase::changed()
{
	for (uint32_t bit = 0; bit < 31; bit++)
	{
		registry().change_counts[bit] += (flags >> bit) & 1;
	}
	registry().change_counts[31]++;
}

// The widget with its label (the last part of the name), a description tooltip and a right click reset
bool CVarBase::imgui()
{
	ImGui::PushID(this);
	const size_t dot = name.find_last_of('.');
	bool changed_value = widget(dot == std::string::npos ? name.c_str() : name.c_str() + dot + 1);
	if (ImGui::IsItemHovered() && !description.empty())
	{
		ImGui::SetTooltip("%s\n(%s)", description.c_str(), name.c_str());
	}
	if (ImGui::BeginPopupContextItem(name.c_str()))
	{
		if (ImGui::MenuItem("Reset to default"))
		{
			reset();
			changed_value = true;
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
	return changed_value;
}

// --- Bool

bool CVarBool::widget(const char* label)
{
	bool v = value;
	const bool c = ImGui::Checkbox(label, &v);
	set(v);
	return c;
}

bool CVarBool::set_json(const nlohmann::json& v)
{
	if (!v.is_boolean() && !v.is_number_integer())
	{
		return false;
	}
	set(v.is_boolean() ? v.get<bool>() : v.get<int>() != 0);
	return true;
}

bool CVarBool::set_string(const std::string& v)
{
	const bool on = v == "1" || v == "true";
	if (!on && v != "0" && v != "false")
	{
		return false;
	}
	set(on);
	return true;
}

// --- Int

bool CVarInt::sanitise(int& v) const
{
	v = std::clamp(v, min_value, max_value);
	return true;
}

bool CVarInt::widget(const char* label)
{
	int v = value;
	bool c = false;
	if (options.empty())
	{
		c = ImGui::SliderInt(label, &v, min_value, max_value);
	}
	else if (ImGui::BeginCombo(label, options[value].c_str()))
	{
		for (int i = 0; i < static_cast<int>(options.size()); i++)
		{
			if (ImGui::Selectable(options[i].c_str(), i == value))
			{
				v = i;
				c = true;
			}
		}
		ImGui::EndCombo();
	}
	set(v);
	return c;
}

bool CVarInt::set_option(const std::string& option)
{
	auto it = std::find(options.begin(), options.end(), option);
	if (it == options.end())
	{
		return false;
	}
	set(static_cast<int>(it - options.begin()));
	return true;
}

bool CVarInt::set_json(const nlohmann::json& v)
{
	if (v.is_string())
	{
		return set_option(v.get<std::string>());
	}
	if (!v.is_number_integer())
	{
		return false;
	}
	set(v.get<int>());
	return true;
}

nlohmann::json CVarInt::get_json() const
{
	return options.empty() ? nlohmann::json(value) : nlohmann::json(options[value]);
}

bool CVarInt::set_string(const std::string& v)
{
	long parsed = 0;
	if (set_option(v))
	{
		return true;
	}
	if (!parse_number(v, to_long, parsed))
	{
		return false;
	}
	set(static_cast<int>(parsed));
	return true;
}

// --- Float

bool CVarFloat::widget(const char* label)
{
	float v = value;
	const bool c = ImGui::SliderFloat(label, &v, min_value, max_value, "%.4g", (get_flags() & CVarLogScale) ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
	set(v);
	return c;
}

bool CVarFloat::set_json(const nlohmann::json& v)
{
	if (!v.is_number())
	{
		return false;
	}
	set(v.get<float>());
	return true;
}

bool CVarFloat::set_string(const std::string& v)
{
	float parsed = 0.0f;
	if (!parse_number(v, to_float, parsed))
	{
		return false;
	}
	set(parsed);
	return true;
}

// --- Float3

bool CVarFloat3::widget(const char* label)
{
	std::array<float, 3> v = value;
	const bool c = colour ? ImGui::ColorEdit3(label, v.data(), ImGuiColorEditFlags_Float) : ImGui::DragFloat3(label, v.data(), 0.01f);
	set(v);
	return c;
}

bool CVarFloat3::set_json(const nlohmann::json& v)
{
	if (!v.is_array() || v.size() != 3 || !std::all_of(v.begin(), v.end(), [](const nlohmann::json& x) { return x.is_number(); }))
	{
		return false;
	}
	set({ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() });
	return true;
}

bool CVarFloat3::set_string(const std::string& v)
{
	std::array<float, 3> parsed = {};
	if (sscanf_s(v.c_str(), "%f,%f,%f", &parsed[0], &parsed[1], &parsed[2]) != 3)
	{
		return false;
	}
	set(parsed);
	return true;
}

// --- Registry

namespace CVars
{
	CVarBase* find(const std::string& name)
	{
		auto it = std::find_if(registry().cvars.begin(), registry().cvars.end(), [&](const CVarBase* cvar) { return cvar->get_name() == name; });
		return it != registry().cvars.end() ? *it : nullptr;
	}

	uint32_t get_change_count(uint32_t flag)
	{
		for (uint32_t bit = 0; bit < 31; bit++)
		{
			if (flag == (1u << bit))
			{
				return registry().change_counts[bit];
			}
		}
		return registry().change_counts[31];
	}

	// Sets one cvar from JSON or a command line value, describing what went wrong in 'error'
	template <typename Value, typename Describe>
	bool set_cvar(const std::string& name, const Value& value, bool (CVarBase::*setter)(const Value&), Describe describe, std::string& error)
	{
		CVarBase* cvar = find(name);
		if (cvar == nullptr)
		{
			error = "unknown cvar '" + name + "'";
			return false;
		}
		if (!(cvar->*setter)(value))
		{
			error = "cvar '" + name + "' cannot be set to " + describe(value);
			return false;
		}
		return true;
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
			if (!set_cvar(name, value, &CVarBase::set_json, [](const nlohmann::json& v) { return v.dump(); }, error))
			{
				return false;
			}
		}
		return true;
	}

	bool apply_assignment(const std::string& assignment, std::string& error)
	{
		const size_t equals = assignment.find('=');
		if (equals == std::string::npos)
		{
			error = "expected name=value, got '" + assignment + "'";
			return false;
		}
		return set_cvar(assignment.substr(0, equals), assignment.substr(equals + 1), &CVarBase::set_string, [](const std::string& v) { return "'" + v + "'"; }, error);
	}

	void reset_all(const std::string& prefix)
	{
		for (CVarBase* cvar : registry().cvars)
		{
			if (has_prefix(*cvar, prefix))
			{
				cvar->reset();
			}
		}
	}

	nlohmann::json changed_json(const std::string& prefix)
	{
		nlohmann::json j = nlohmann::json::object();
		for (const CVarBase* cvar : registry().cvars)
		{
			if (has_prefix(*cvar, prefix) && !cvar->is_default())
			{
				j[cvar->get_name()] = cvar->get_json();
			}
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

			// The cvars shown, grouped by category in order of first appearance
			std::vector<std::pair<std::string, std::vector<CVarBase*>>> categories;
			for (CVarBase* cvar : registry().cvars)
			{
				if (!has_prefix(*cvar, prefix) || !(contains_case_insensitive(cvar->get_name(), filter) || contains_case_insensitive(cvar->get_description(), filter)))
				{
					continue;
				}
				auto it = std::find_if(categories.begin(), categories.end(), [&](const auto& c) { return c.first == cvar->get_category(); });
				(it != categories.end() ? it->second : categories.emplace_back(cvar->get_category(), std::vector<CVarBase*>()).second).push_back(cvar);
			}
			for (const auto& [category, cvars] : categories)
			{
				if (filter[0] != '\0')
				{
					ImGui::SetNextItemOpen(true);
				}
				if (ImGui::CollapsingHeader(category.c_str()))
				{
					ImGui::PushItemWidth(-200.0f);
					for (CVarBase* cvar : cvars)
					{
						// Changed values stand out
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
