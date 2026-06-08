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
 *
 * Lifecycle: ctor registers self with ScriptEngine and queues for initial
 * evaluation. The actual first eval is deferred until ScriptEngine::sceneReady()
 * fires (after CScene fully constructs, all objects parsed, and --set-property
 * overrides applied). This guarantees scripts run against a populated scene
 * registry rather than a half-built one.
 */
class ScriptedDynamicValue : public DynamicValue {
public:
    ScriptedDynamicValue (
	std::string scriptSource,
	std::map<std::string, DynamicValueUniquePtr> scriptProps,
	DynamicValue baseValue,
	std::map<std::string, DynamicValue*> watchProperties = {}
    );

    ~ScriptedDynamicValue () override;

    /**
     * Re-evaluate this script and propagate the result. Public so
     * ScriptEngine can call it for queued initial evals and per-frame ticks.
     * Idempotent within a frame thanks to a re-entrancy guard.
     */
    void reevaluate ();

    /**
     * Whether this script reads time-aware globals (engine.runtime,
     * engine.frametime, thisScene.time, thisScene.dt). Detected at parse
     * time. Time-aware scripts get re-evaluated every frame; pure
     * property-driven scripts only re-evaluate on property change.
     */
    bool needsTick () const { return this->m_needsTick; }

    /**
     * Stable per-instance id used by the JS engine to namespace this
     * script's persistent closure (top-level vars survive across evals
     * so cycle/timer state accumulates correctly).
     */
    int instanceId () const { return this->m_instanceId; }

private:
    std::string m_scriptSource;
    std::map<std::string, DynamicValueUniquePtr> m_scriptProps;
    std::map<std::string, DynamicValue*> m_watchProperties;
    DynamicValue m_baseValue;
    int m_instanceId = 0;
    bool m_needsTick = false;
    bool m_evaluating = false;  // re-entrancy guard
};
} // namespace WallpaperEngine::Data::Model
