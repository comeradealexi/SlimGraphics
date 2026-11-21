#pragma once
#include <nlohmann/json.hpp>
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
	CVarReload = 1 << 1,		// Changing it needs resources or the scene reloaded
	CVarLogScale = 1 << 2,		// Float slider with a logarithmic scale
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

	virtual bool imgui() = 0;										// Returns true when the value changed
	virtual bool set_json(const nlohmann::json& value) = 0;			// False when the value has the wrong type
	virtual nlohmann::json get_json() const = 0;
	virtual bool set_string(const std::string& value) = 0;			// Command line form
	virtual void reset() = 0;
	virtual bool is_default() const = 0;

protected:
	void changed();				// Call after every change of value

private:
	std::string name;
	std::string category;
	std::string description;
	uint32_t flags;
};

class CVarBool : public CVarBase
{
public:
	CVarBool(const char* name, const char* category, const char* description, bool default_value, uint32_t flags = CVarNone);
	operator bool() const { return value; }
	bool get() const { return value; }
	void set(bool v);

	bool imgui() override;
	bool set_json(const nlohmann::json& v) override;
	nlohmann::json get_json() const override { return value; }
	bool set_string(const std::string& v) override;
	void reset() override { set(default_value); }
	bool is_default() const override { return value == default_value; }

private:
	bool value;
	const bool default_value;
};

class CVarInt : public CVarBase
{
public:
	CVarInt(const char* name, const char* category, const char* description, int default_value, int min_value, int max_value, uint32_t flags = CVarNone);
	// An enumeration: the value indexes 'options' (shown as a combo box; JSON may give the option's name)
	CVarInt(const char* name, const char* category, const char* description, int default_value, std::vector<std::string> options, uint32_t flags = CVarNone);
	operator int() const { return value; }
	int get() const { return value; }
	void set(int v);
	const std::vector<std::string>& get_options() const { return options; }

	bool imgui() override;
	bool set_json(const nlohmann::json& v) override;
	nlohmann::json get_json() const override;
	bool set_string(const std::string& v) override;
	void reset() override { set(default_value); }
	bool is_default() const override { return value == default_value; }

private:
	int value;
	const int default_value;
	int min_value;
	int max_value;
	std::vector<std::string> options;
};

class CVarFloat : public CVarBase
{
public:
	CVarFloat(const char* name, const char* category, const char* description, float default_value, float min_value, float max_value, uint32_t flags = CVarNone);
	operator float() const { return value; }
	float get() const { return value; }
	void set(float v);

	bool imgui() override;
	bool set_json(const nlohmann::json& v) override;
	nlohmann::json get_json() const override { return value; }
	bool set_string(const std::string& v) override;
	void reset() override { set(default_value); }
	bool is_default() const override { return value == default_value; }

private:
	float value;
	const float default_value;
	float min_value;
	float max_value;
};

// Three floats: a colour (shown with a colour editor) or a vector
class CVarFloat3 : public CVarBase
{
public:
	CVarFloat3(const char* name, const char* category, const char* description, float x, float y, float z, bool colour, uint32_t flags = CVarNone);
	const float* get() const { return value; }
	float operator[](int i) const { return value[i]; }
	void set(float x, float y, float z);

	bool imgui() override;
	bool set_json(const nlohmann::json& v) override;
	nlohmann::json get_json() const override { return { value[0], value[1], value[2] }; }
	bool set_string(const std::string& v) override;
	void reset() override { set(default_value[0], default_value[1], default_value[2]); }
	bool is_default() const override { return value[0] == default_value[0] && value[1] == default_value[1] && value[2] == default_value[2]; }

private:
	float value[3];
	float default_value[3];
	bool colour;
};

namespace CVars
{
	// Every registered cvar, in registration order
	const std::vector<CVarBase*>& all();
	CVarBase* find(const std::string& name);

	// Incremented by every change of a cvar with the given flag (compare with a stored value to detect changes)
	uint32_t get_change_count(uint32_t flag);

	// Applies {"name": value, ...}. Unknown names and wrong types are reported in 'error' (and stop the apply), so a typo
	// in a test plan cannot silently test a default.
	bool apply_json(const nlohmann::json& values, std::string& error);
	// Resets every cvar whose name starts with prefix
	void reset_all(const std::string& prefix);
	// "name=value" (the -cvar= command line switch)
	bool apply_assignment(const std::string& assignment, std::string& error);
	// Cvars that differ from their defaults, as JSON (to paste into test plans)
	nlohmann::json changed_json(const std::string& prefix);

	// A window of every cvar whose name starts with prefix, grouped by category with a search filter
	void imgui_window(const char* window_name, const std::string& prefix);
}
