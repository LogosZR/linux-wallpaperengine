#include "ScriptedDynamicValue.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine::Data::Model;

ScriptedDynamicValue::ScriptedDynamicValue (
    std::string scriptSource,
    std::map<std::string, DynamicValueUniquePtr> scriptProps,
    DynamicValue baseValue,
    std::map<std::string, DynamicValue*> watchProperties
) :
    DynamicValue (),
    m_scriptSource (std::move (scriptSource)),
    m_scriptProps (std::move (scriptProps)),
    m_watchProperties (std::move (watchProperties)),
    m_baseValue (std::move (baseValue)) {
    // Listen for changes on each script property
    for (auto& [name, prop] : this->m_scriptProps) {
	if (prop) {
	    prop->listen ([this] (const DynamicValue&) {
		this->reevaluate ();
	    });
	}
    }

    // Listen for changes on global watch-properties so scripts that read
    // engine.userProperties.X directly (no declared scriptProperties block)
    // still re-evaluate when X changes.
    for (auto& [name, prop] : this->m_watchProperties) {
	if (prop) {
	    prop->listen ([this] (const DynamicValue&) {
		this->reevaluate ();
	    });
	}
    }

    // Do an initial evaluation
    this->reevaluate ();
}

void ScriptedDynamicValue::reevaluate () {
    // Build raw pointer map for the engine. scriptProps come first; watch
    // properties fill in any names not already present (script's own
    // declared properties take precedence on collision).
    std::map<std::string, DynamicValue*> propsMap;
    for (const auto& [name, prop] : this->m_scriptProps) {
	propsMap[name] = prop.get ();
    }
    for (const auto& [name, prop] : this->m_watchProperties) {
	if (prop && propsMap.find (name) == propsMap.end ()) {
	    propsMap[name] = prop;
	}
    }

    auto result = WallpaperEngine::Scripting::ScriptEngine::instance ().evaluate (
	this->m_scriptSource, propsMap, this->m_baseValue);

    if (result) {
	this->update (*result);
    }
}
