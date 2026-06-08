#include "ScriptedDynamicValue.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine::Data::Model;

namespace {
// Cheap heuristic: does the script source reference time-aware globals?
// Time-aware scripts (cycle galleries, audio reactive, animation drivers)
// need per-frame re-evaluation regardless of property changes. Pure
// property-driven scripts skip the per-frame tick to save eval cost.
bool detectNeedsTick (const std::string& src) {
    static const char* triggers[] = {
	"engine.runtime",
	"engine.frametime",
	"engine.time",
	"thisScene.time",
	"thisScene.dt",
	"thisScene.currentTime",
    };
    for (const char* t : triggers) {
	if (src.find (t) != std::string::npos) {
	    return true;
	}
    }
    return false;
}
} // namespace

static int sNextScriptInstanceId = 1;

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
    m_baseValue (std::move (baseValue)),
    m_instanceId (sNextScriptInstanceId++),
    m_needsTick (detectNeedsTick (this->m_scriptSource)) {
    // Listen for changes on each script property. The listener guards
    // against running before sceneReady() — evaluations triggered during
    // scene parse (e.g. by --set-property override application) are
    // deferred so they fire against a fully-built scene.
    auto trigger = [this] (const DynamicValue&) {
	if (Scripting::ScriptEngine::instance ().isSceneReady ()) {
	    this->reevaluate ();
	}
	// else: ignored. ScriptEngine::sceneReady() will fire the queued
	// initial eval once everything is in place; subsequent property
	// changes route here normally.
    };

    for (auto& [name, prop] : this->m_scriptProps) {
	if (prop) {
	    prop->listen (trigger);
	}
    }

    for (auto& [name, prop] : this->m_watchProperties) {
	if (prop) {
	    prop->listen (trigger);
	}
    }

    // Register with the engine so it can drive lifecycle events
    // (sceneReady → first eval, per-frame tick for time-aware scripts).
    Scripting::ScriptEngine::instance ().registerLiveScript (this);
}

ScriptedDynamicValue::~ScriptedDynamicValue () {
    Scripting::ScriptEngine::instance ().unregisterLiveScript (this);
}

void ScriptedDynamicValue::reevaluate () {
    // Re-entrancy guard. A script setting a layer's visibility via
    // setLayerVisible() ends up updating that layer's DynamicValue,
    // which (since watchProperties listens broadly) could trigger
    // reevaluate() recursively. Bail out if we're already inside one.
    if (this->m_evaluating) {
	return;
    }
    this->m_evaluating = true;

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
	this->m_scriptSource, propsMap, this->m_baseValue, this->m_instanceId);

    if (result) {
	this->update (*result);
    }

    this->m_evaluating = false;
}
