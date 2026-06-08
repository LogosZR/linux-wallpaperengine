#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Types.h"

extern "C" {
#include "quickjs.h"
}

// Forward decl to avoid pulling Render headers into Scripting layer.
namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Data::Model {
class ScriptedDynamicValue;
}

namespace WallpaperEngine::Scripting {
using namespace WallpaperEngine::Data::Model;

// Opaque handle returned by createLayerScript. 0 means invalid / not created.
using ScriptLayerHandle = int;
static constexpr ScriptLayerHandle kInvalidLayerHandle = 0;

class ScriptEngine {
public:
    static ScriptEngine& instance ();

    ~ScriptEngine ();
    ScriptEngine (const ScriptEngine&) = delete;
    ScriptEngine& operator= (const ScriptEngine&) = delete;

    /**
     * Evaluate a WallpaperEngine script's update() function.
     *
     * @param scriptSource The full JS script text (ES6 module with export function update(value))
     * @param scriptProperties Map of property name to current DynamicValue*
     * @param currentValue The current value to pass to update()
     * @return The modified value from update(), or a copy of currentValue on error
     */
    DynamicValueUniquePtr evaluate (
	const std::string& scriptSource,
	const std::map<std::string, DynamicValue*>& scriptProperties,
	const DynamicValue& currentValue
    );

    // -------------------------------------------------------------------
    // Layer-script API (Phase 2 — dynamic text)
    // -------------------------------------------------------------------
    //
    // Wallpaper Engine text-object scripts follow a lifecycle pattern that
    // cannot be evaluated with the simple `update(value) -> value` contract
    // above. They typically look like:
    //
    //   'use strict';
    //   export var scriptProperties = createScriptProperties()…finish();
    //   export function init()   { /* subscribe to events, cache data    */ }
    //   export function update() { thisLayer.text = computeCurrentText(); }
    //
    // The script mutates a `thisLayer` object in place instead of returning
    // a value, and lifecycle functions are optional. The API below keeps
    // per-layer state alive across frames so `init()` runs once and
    // `update()` re-runs every tick.

    /**
     * Create a persistent "layer script" from a WE text-object script.
     *
     * @param scriptSource The full JS script text.
     * @param initialScriptProps Initial values for scriptProperties entries.
     *        Ownership stays with the caller; we only snapshot current values.
     * @param initialText Initial value of `thisLayer.text` (usually the
     *        static placeholder carried in the JSON).
     * @return A positive handle, or kInvalidLayerHandle if evaluation failed.
     */
    ScriptLayerHandle createLayerScript (
	const std::string& scriptSource,
	const std::map<std::string, DynamicValue*>& initialScriptProps,
	const std::string& initialText
    );

    /**
     * Advance a layer by one frame.
     *
     * On the first call, invokes `init()` (if defined) before `update()`.
     * Updates a `thisScene` context visible to the script (time, fps).
     * Silently no-ops if the handle is invalid.
     */
    void tickLayer (ScriptLayerHandle handle, double time, double deltaTime, double fps);

    /**
     * Read the current value of `thisLayer.text` for the given layer.
     * Returns an empty string if the handle is invalid.
     */
    std::string layerText (ScriptLayerHandle handle);

    /**
     * Tear down a layer: invokes `destroy()` (if defined) and frees state.
     */
    void destroyLayer (ScriptLayerHandle handle);

    // -------------------------------------------------------------------
    // Scene-aware API (Phase 4 — thisScene.getLayer)
    // -------------------------------------------------------------------
    //
    // WPE scripts use `thisScene.getLayer(name)` to look up another object
    // in the same scene and mutate its visibility/alpha at runtime. We
    // need the engine to know which scene is currently active so it can
    // resolve names to CObjects and mutate their backing UserSetting
    // DynamicValues.
    //
    // The lifecycle: the scene calls setScene(this) once after objects
    // are populated, and clearScene() in its destructor. Between those
    // calls, getLayer() resolves names against this scene's m_objects.
    //
    // For simplicity (single wallpaper at a time), we keep one current
    // scene at a time. If multi-monitor with different wallpapers ever
    // calls evaluate() from two scenes, this becomes a stack — but today
    // every wallpaper has its own background id and scenes don't overlap
    // in the same eval call.

    /**
     * Make this scene the active context for thisScene.getLayer() lookups.
     * Stores the pointer and marks the JS layer registry dirty; the actual
     * registry build is lazy and happens on first getLayer() call. This
     * lets ScriptedDynamicValue evaluations during scene parse no-op safely
     * before the scene is fully constructed, and ensures the registry
     * always reflects the current scene state when scripts touch it.
     */
    void setScene (Render::Wallpapers::CScene* scene);

    /** Clear the active scene and tear down the JS layer registry. */
    void clearScene ();

    /**
     * Mark the scene as fully constructed. Releases the gate that defers
     * initial reevaluation of every ScriptedDynamicValue. Called by
     * CScene at the end of its constructor (after all objects parsed and
     * --set-property overrides applied).
     */
    void sceneReady ();

    /**
     * Build (or rebuild) the JS layer registry from the active scene.
     * Idempotent and cheap to call repeatedly; only rebuilds when dirty.
     * Invoked by C accessor functions before resolving a layer name/index.
     */
    void ensureSceneRegistry ();

    /**
     * Track a ScriptedDynamicValue across its lifetime so we can drive
     * lifecycle events (initial eval at sceneReady, per-frame tick).
     * Called by ScriptedDynamicValue ctor/dtor.
     */
    void registerLiveScript (Data::Model::ScriptedDynamicValue* sv);
    void unregisterLiveScript (Data::Model::ScriptedDynamicValue* sv);

    /**
     * Returns true once sceneReady() has fired. Before that, scripts that
     * try to evaluate should defer (queue themselves for first eval) so
     * they don't run against a half-built scene.
     */
    bool isSceneReady () const { return this->m_sceneReady; }

    /**
     * Resolve a layer name to its CObject id within the active scene.
     * Returns 0 if no scene is active or the name doesn't match.
     * Used by the JS↔C accessor callbacks.
     */
    int findObjectIdByName (const std::string& name) const;

    /** Read/write helpers used by the JS proxy callbacks. */
    bool getLayerVisible (int objectId) const;
    void setLayerVisible (int objectId, bool value);
    float getLayerAlpha (int objectId) const;
    void setLayerAlpha (int objectId, float value);

private:
    ScriptEngine ();

    JSValue dynamicValueToJS (const DynamicValue& value) const;
    DynamicValueUniquePtr jsToDynamicValue (JSValue val, DynamicValue::UnderlyingType hint) const;

    // Installs globalThis.__layers and related helpers. Called lazily.
    void ensureLayerRegistry ();

    // Installs the __getLayerVisible / __setLayerVisible / __sceneLayers
    // bootstrap into the JS context. Called from setScene().
    void buildSceneLayerRegistry ();
    void teardownSceneLayerRegistry ();

    JSRuntime* m_runtime = nullptr;
    JSContext* m_context = nullptr;
    ScriptLayerHandle m_nextLayerId = 1;
    bool m_layerRegistryReady = false;
    bool m_sceneRegistryReady = false;
    bool m_sceneRegistryDirty = false;
    bool m_sceneReady = false;
    std::map<ScriptLayerHandle, bool> m_layerInitialized;
    Render::Wallpapers::CScene* m_currentScene = nullptr;
    std::set<Data::Model::ScriptedDynamicValue*> m_liveScripts;
    std::vector<Data::Model::ScriptedDynamicValue*> m_pendingFirstEval;
};
} // namespace WallpaperEngine::Scripting
