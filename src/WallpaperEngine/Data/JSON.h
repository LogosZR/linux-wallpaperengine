#pragma once

#include <glm/detail/qualifier.hpp>
#include <glm/detail/type_vec1.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

#include "WallpaperEngine/Data/Builders/UserSettingBuilder.h"
#include "WallpaperEngine/Data/Builders/VectorBuilder.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Data/Utils/SFINAE.h"
#include "WallpaperEngine/Logging/Log.h"

namespace WallpaperEngine::Data::JSON {
using namespace WallpaperEngine::Data::Builders;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Utils::SFINAE;

class JsonExtensions;

using JSON = nlohmann::basic_json<
    std::map, std::vector, std::string, bool, std::int64_t, std::uint64_t, double, std::allocator,
    nlohmann::adl_serializer, std::vector<std::uint8_t>, JsonExtensions>;

/**
 * Small extensions class that is used as base class of nlohmann's implementation.
 *
 * Provides shorthand methods to reduce the amount of handling required for specific situations
 * (mainly throwing readable exceptions when a value is missing, optional/default values, user settings...)
 */
class JsonExtensions {
public:
    using base_type = JSON;

    template <typename T, typename std::enable_if_t<is_glm_vec<T>::value, int> = 0> [[nodiscard]] T get () const {
	constexpr int length = GlmVecTraits<T>::length;
	constexpr glm::qualifier qualifier = GlmVecTraits<T>::qualifier;

	// call the specialized version of the function
	return get<length, typename GlmVecTraits<T>::type, qualifier> ();
    }
    template <int length, typename type, glm::qualifier qualifier>
    [[nodiscard]] glm::vec<length, type, qualifier> get () const {
	return VectorBuilder::parse<length, type, qualifier> (this->base ().get<std::string> ());
    }
    [[nodiscard]] base_type require (const std::string& key, const std::string& message) const {
	auto base = this->base ();
	const auto it = base.find (key);

	if (it == base.end ()) {
	    sLog.exception (message, ". Contents: ", base.dump ());
	}

	return *it;
    }
    template <typename T> [[nodiscard]] T require (const std::string& key, const std::string& message) const {
	auto base = this->base ();
	const auto it = base.find (key);

	if (it == base.end ()) {
	    sLog.exception (message, ". Contents: ", base.dump ());
	}

	return (*it);
    }
    [[nodiscard]] std::optional<base_type> optional (const std::string& key) const noexcept {
	auto base = this->base ();
	const auto it = base.find (key);
	auto result = std::optional<base_type> {};

	if (it != base.end () && !it->is_null ()) {
	    result.emplace (*it);
	}

	return result;
    }
    template <typename T> [[nodiscard]] std::optional<T> optional (const std::string& key) const noexcept {
	auto base = this->base ();
	const auto it = base.find (key);

	if (it == base.end () || it->is_null ()) {
	    return std::nullopt;
	}

	// Implicit JSON-to-T conversion can throw nlohmann::json::type_error
	// (e.g. asking for int when the JSON value is a string). Swallowing
	// here keeps the noexcept contract honest — callers either get the
	// converted value or nullopt, never a process termination.
	try {
	    return *it;
	} catch (...) {
	    return std::nullopt;
	}
    }
    template <typename T> [[nodiscard]] T optional (const std::string& key, T defaultValue) const noexcept {
	auto base = this->base ();
	const auto it = base.find (key);

	if (it == base.end () || it->is_null ()) {
	    return defaultValue;
	}

	// Same as above: tolerate type mismatches by falling back to the
	// caller's default. WPE wallpapers are full of fields where one
	// wallpaper uses int and another uses string-formatted vec2 (e.g.
	// `padding`), so we'd rather absorb the difference than crash.
	try {
	    return (*it);
	} catch (...) {
	    return defaultValue;
	}
    }
    [[nodiscard]] UserSettingUniquePtr user (const std::string& key, const Properties& properties) const;
    template <typename T>
    [[nodiscard]] UserSettingUniquePtr
    user (const std::string& key, const Properties& properties, T defaultValue) const {
	const auto value = this->optional (key);

	if (!value.has_value ()) {
	    return UserSettingBuilder::fromValue<T> (defaultValue);
	}

	// performs a second lookup, but handles the actual call to UserSettingParser outside of this header
	// this resolving the include loop
	return this->user (key, properties);
    }

    template <int length, typename type, glm::qualifier qualifier> operator glm::vec<length, type, qualifier> () const {
	return get<length, type, qualifier> ();
    }
    template <typename T, typename std::enable_if_t<is_glm_vec<T>::value> = 0> operator T () const {
	constexpr int length = GlmVecTraits<T>::length;
	constexpr glm::qualifier qualifier = GlmVecTraits<T>::qualifier;

	// call the specialized version of the function
	return operator glm::vec<length, typename GlmVecTraits<T>::type, qualifier> ();
    }

private:
    /**
     * @return The base json object to be used by the extension methods
     */
    [[nodiscard]] const base_type& base () const { return *static_cast<const base_type*> (this); }
};

} // namespace WallpaperEngine::Data::JSON

// ── Lenient JSON preprocessing ───────────────────────────────────────────────
// Wallpaper Engine's editor produces "relaxed JSON" with trailing commas
// (e.g., `["a", "b",]` or `{"x": 1,}`). nlohmann::json is strict and
// rejects these. Strip trailing commas before parsing so wallpapers that
// worked in the Windows WE client also work here.
namespace WallpaperEngine::Data::JSON {

inline std::string stripTrailingCommas (const std::string& input) {
    std::string result;
    result.reserve (input.size ());

    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i < input.size (); ++i) {
	char c = input[i];

	if (escaped) {
	    escaped = false;
	    result += c;
	    continue;
	}

	if (c == '\\' && inString) {
	    escaped = true;
	    result += c;
	    continue;
	}

	if (c == '"') {
	    inString = !inString;
	    result += c;
	    continue;
	}

	if (inString) {
	    result += c;
	    continue;
	}

	// Outside a string: check for trailing comma before ] or }
	if (c == ',') {
	    // Look ahead past whitespace for ] or }
	    size_t j = i + 1;
	    while (j < input.size () && (input[j] == ' ' || input[j] == '\t' || input[j] == '\n' || input[j] == '\r')) {
		++j;
	    }
	    if (j < input.size () && (input[j] == ']' || input[j] == '}')) {
		// Skip this trailing comma
		continue;
	    }
	}

	result += c;
    }

    return result;
}

/// Parse JSON with trailing-comma tolerance. Drop-in replacement for
/// JSON::parse() that preprocesses the input string.
inline JSON parseLenient (const std::string& input) {
    return JSON::parse (stripTrailingCommas (input));
}

} // namespace WallpaperEngine::Data::JSON
