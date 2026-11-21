#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <string>
#include <vector>
#include <cstdint>

// Console variables: named, typed, range checked settings that register themselves when constructed (as globals), so a
// system declares its tweakables next to the code that reads them. Every cvar is shown in an ImGui panel (grouped by
// category), can be set from the command line (-cvar=name=value) and from test plans (JSON objects of name: value), and
// resets to its default.
//
//   static CVarFloat sun_elevation("pbr.sun.elevation", "Sun", "Degrees above the horizon", 50.0f, -5.0f, 90.0f);
//   float e = sun_elevation;
//
// Flags tell the systems reading cvars what a change means (e.g. CVarResetHistory restarts temporal accumulation).

enum CVarFlags : uint32_t
{
	CVarNone = 0,
	CVarResetHistory = 1 << 0,	// Changing it invalidates accumulated / temporal results (path tracer, denoiser histories)
	CVarLogScale = 1 << 1,		// Float slider with a logarithmic scale
};

class CVarBase
{
public:
	CVarBase(const char* name, const char* category, const char* description, uint32_t flags);
	virtual ~CVarBase() = default;

	const std::string& get_name() const { return name; }
	const std::string& get_category() const { return category; }
	const std::string& get_description() const { return description; }
	uint32_t get_flags() const { return flags; }

	bool imgui();												// The widget; true when the value changed
	virtual bool set_json(const nlohmann::json& value) = 0;		// False when the value has the wrong type
	virtual nlohmann::json get_json() const = 0;
	virtual bool set_string(const std::string& value) = 0;		// Command line form
	virtual void reset() = 0;
	virtual bool is_default() const = 0;

protected:
	virtual bool widget(const char* label) = 0;					// Draws the widget and sets a changed value
	void changed();												// Call after every change of value

private:
	std::string name;
	std::string category;
	std::string description;
	uint32_t flags;
};

// The value, its default and change tracking shared by every type; 'sanitise' clamps or rejects a new value
template <typename T>
class CVarValue : public CVarBase
{
public:
	CVarValue(const char* name, const char* category, const char* description, T initial, uint32_t flags)
		: CVarBase(name, category, description, flags), value(initial), default_value(initial) {}

	operator T() const { return value; }
	T get() const { return value; }
	void set(T v)
	{
		if (sanitise(v) && v != value)
		{
			value = v;
			changed();
		}
	}
	void reset() override { set(default_value); }
	bool is_default() const override { return value == default_value; }

protected:
	virtual bool sanitise(T&) const { return true; }

	T value;
	const T default_value;
};

class CVarBool : public CVarValue<bool>
{
public:
	CVarBool(const char* name, const char* category, const char* description, bool default_value, uint32_t flags = CVarNone)
		: CVarValue(name, category, description, default_value, flags) {}

	bool set_json(const nlohmann::json& v) override;
	nlohmann::json get_json() const override { return value; }
	bool set_string(const std::string& v) override;

protected:
	bool widget(const char* label) override;
};

class CVarInt : public CVarValue<int>
{
public:
	CVarInt(const char* name, const char* category, const char* description, int default_value, int min, int max, uint32_t flags = CVarNone)
		: CVarValue(name, category, description, default_value, flags), min_value(min), max_value(max) {}
	// An enumeration: the value indexes 'options' (shown as a combo box; JSON and the command line may give the option's name)
	CVarInt(const char* name, const char* category, const char* description, int default_value, std::vector<std::string> option_names, uint32_t flags = CVarNone)
		: CVarValue(name, category, description, default_value, flags), min_value(0), max_value(static_cast<int>(option_names.size()) - 1), options(std::move(option_names)) {}

	bool set_json(const nlohmann::json& v) override;
	nlohmann::json get_json() const override;
	bool set_string(const std::string& v) override;

protected:
	bool sanitise(int& v) const override;
	bool widget(const char* label) override;

private:
	bool set_option(const std::string& name);

	int min_value;
	int max_value;
	std::vector<std::string> options;
};

class CVarFloat : public CVarValue<float>
{
public:
	CVarFloat(const char* name, const char* category, const char* description, float default_value, float min, float max, uint32_t flags = CVarNone)
		: CVarValue(name, category, description, default_value, flags), min_value(min), max_value(max) {}

	bool set_json(const nlohmann::json& v) override;
	nlohmann::json get_json() const override { return value; }
	bool set_string(const std::string& v) override;

protected:
	// Sliders clamp; typed values (ctrl+click, JSON, command line) may go beyond the slider range but stay finite
	bool sanitise(float& v) const override { return v == v; }
	bool widget(const char* label) override;

private:
	float min_value;
	float max_value;
};

// Three floats: a colour (shown with a colour editor) or a vector
class CVarFloat3 : public CVarValue<std::array<float, 3>>
{
public:
	CVarFloat3(const char* name, const char* category, const char* description, float x, float y, float z, bool is_colour, uint32_t flags = CVarNone)
		: CVarValue(name, category, description, { x, y, z }, flags), colour(is_colour) {}
	float operator[](int i) const { return value[i]; }

	bool set_json(const nlohmann::json& v) override;
	nlohmann::json get_json() const override { return value; }
	bool set_string(const std::string& v) override;

protected:
	bool widget(const char* label) override;

private:
	bool colour;
};

namespace CVars
{
	CVarBase* find(const std::string& name);

	// Incremented by every change of a cvar with the given flag (compare with a stored value to detect changes)
	uint32_t get_change_count(uint32_t flag);

	// Applies {"name": value, ...}. Unknown names and wrong types are reported in 'error' (and stop the apply), so a typo
	// in a test plan cannot silently test a default.
	bool apply_json(const nlohmann::json& values, std::string& error);
	// "name=value" (the -cvar= command line switch)
	bool apply_assignment(const std::string& assignment, std::string& error);
	// Resets every cvar whose name starts with prefix
	void reset_all(const std::string& prefix);
	// Cvars that differ from their defaults, as JSON (to paste into test plans)
	nlohmann::json changed_json(const std::string& prefix);

	// A window of every cvar whose name starts with prefix, grouped by category with a search filter
	void imgui_window(const char* window_name, const std::string& prefix);
}
