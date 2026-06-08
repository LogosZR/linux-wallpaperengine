#pragma once

#include <map>
#include <string>

#include "DynamicValue.h"
#include "Types.h"

namespace WallpaperEngine::Data::Model {

/**
 * A DynamicValue whose value is computed by evaluating a WallpaperEngine script.
 *
 * Holds the script source, a map of script property names to their DynamicValue
 * pointers (connected to user Properties), and the base value from the JSON.
 * When any connected scriptProperty changes, re-evaluates the script.
 *
 * Many WPE scripts read from `engine.userProperties.<name>` directly without
 * declaring a `scriptProperties` block. For these we also accept a watch list
 * of project-wide properties: any change to one triggers a re-evaluation, and
 * the values are exposed to the script as additional entries in `engine.userProperties`.
 */
class ScriptedDynamicValue : public DynamicValue {
public:
    ScriptedDynamicValue (
	std::string scriptSource,
	std::map<std::string, DynamicValueUniquePtr> scriptProps,
	DynamicValue baseValue,
	std::map<std::string, DynamicValue*> watchProperties = {}
    );

    ~ScriptedDynamicValue () override = default;

private:
    void reevaluate ();

    std::string m_scriptSource;
    std::map<std::string, DynamicValueUniquePtr> m_scriptProps;
    std::map<std::string, DynamicValue*> m_watchProperties;
    DynamicValue m_baseValue;
};
} // namespace WallpaperEngine::Data::Model
