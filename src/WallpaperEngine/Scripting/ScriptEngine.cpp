#include "ScriptEngine.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Render/Objects/CImage.h"
#include "WallpaperEngine/Data/Model/Object.h"

#include <cstdio>
#include <sstream>

using namespace WallpaperEngine::Scripting;
using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Render::CObject;
using WallpaperEngine::Render::Objects::CImage;
using WallpaperEngine::Render::Wallpapers::CScene;

static std::unique_ptr<ScriptEngine> sScriptEngine;

ScriptEngine& ScriptEngine::instance () {
    if (!sScriptEngine) {
	sScriptEngine = std::unique_ptr<ScriptEngine> (new ScriptEngine ());
    }
    return *sScriptEngine;
}

ScriptEngine::ScriptEngine () {
    this->m_runtime = JS_NewRuntime ();
    if (!this->m_runtime) {
	sLog.error ("ScriptEngine: Failed to create JS runtime");
	return;
    }

    this->m_context = JS_NewContext (this->m_runtime);
    if (!this->m_context) {
	sLog.error ("ScriptEngine: Failed to create JS context");
	JS_FreeRuntime (this->m_runtime);
	this->m_runtime = nullptr;
	return;
    }
}

ScriptEngine::~ScriptEngine () {
    if (this->m_context) {
	JS_FreeContext (this->m_context);
    }
    if (this->m_runtime) {
	JS_FreeRuntime (this->m_runtime);
    }
}

JSValue ScriptEngine::dynamicValueToJS (const DynamicValue& value) const {
    JSContext* ctx = this->m_context;

    switch (value.getType ()) {
	case DynamicValue::String:
	    return JS_NewString (ctx, value.getString ().c_str ());
	case DynamicValue::Float:
	    return JS_NewFloat64 (ctx, value.getFloat ());
	case DynamicValue::Int:
	    return JS_NewInt32 (ctx, value.getInt ());
	case DynamicValue::Boolean:
	    return JS_NewBool (ctx, value.getBool ());
	case DynamicValue::Vec2: {
	    JSValue obj = JS_NewObject (ctx);
	    JS_SetPropertyStr (ctx, obj, "x", JS_NewFloat64 (ctx, value.getVec2 ().x));
	    JS_SetPropertyStr (ctx, obj, "y", JS_NewFloat64 (ctx, value.getVec2 ().y));
	    return obj;
	}
	case DynamicValue::Vec3: {
	    JSValue obj = JS_NewObject (ctx);
	    JS_SetPropertyStr (ctx, obj, "x", JS_NewFloat64 (ctx, value.getVec3 ().x));
	    JS_SetPropertyStr (ctx, obj, "y", JS_NewFloat64 (ctx, value.getVec3 ().y));
	    JS_SetPropertyStr (ctx, obj, "z", JS_NewFloat64 (ctx, value.getVec3 ().z));
	    return obj;
	}
	case DynamicValue::Vec4: {
	    JSValue obj = JS_NewObject (ctx);
	    JS_SetPropertyStr (ctx, obj, "x", JS_NewFloat64 (ctx, value.getVec4 ().x));
	    JS_SetPropertyStr (ctx, obj, "y", JS_NewFloat64 (ctx, value.getVec4 ().y));
	    JS_SetPropertyStr (ctx, obj, "z", JS_NewFloat64 (ctx, value.getVec4 ().z));
	    JS_SetPropertyStr (ctx, obj, "w", JS_NewFloat64 (ctx, value.getVec4 ().w));
	    return obj;
	}
	case DynamicValue::IVec2: {
	    JSValue obj = JS_NewObject (ctx);
	    JS_SetPropertyStr (ctx, obj, "x", JS_NewInt32 (ctx, value.getIVec2 ().x));
	    JS_SetPropertyStr (ctx, obj, "y", JS_NewInt32 (ctx, value.getIVec2 ().y));
	    return obj;
	}
	case DynamicValue::IVec3: {
	    JSValue obj = JS_NewObject (ctx);
	    JS_SetPropertyStr (ctx, obj, "x", JS_NewInt32 (ctx, value.getIVec3 ().x));
	    JS_SetPropertyStr (ctx, obj, "y", JS_NewInt32 (ctx, value.getIVec3 ().y));
	    JS_SetPropertyStr (ctx, obj, "z", JS_NewInt32 (ctx, value.getIVec3 ().z));
	    return obj;
	}
	case DynamicValue::IVec4: {
	    JSValue obj = JS_NewObject (ctx);
	    JS_SetPropertyStr (ctx, obj, "x", JS_NewInt32 (ctx, value.getIVec4 ().x));
	    JS_SetPropertyStr (ctx, obj, "y", JS_NewInt32 (ctx, value.getIVec4 ().y));
	    JS_SetPropertyStr (ctx, obj, "z", JS_NewInt32 (ctx, value.getIVec4 ().z));
	    JS_SetPropertyStr (ctx, obj, "w", JS_NewInt32 (ctx, value.getIVec4 ().w));
	    return obj;
	}
	default:
	    return JS_UNDEFINED;
    }
}

DynamicValueUniquePtr ScriptEngine::jsToDynamicValue (JSValue val, DynamicValue::UnderlyingType hint) const {
    JSContext* ctx = this->m_context;
    auto result = std::make_unique<DynamicValue> ();

    if (JS_IsException (val)) {
	return result;
    }

    // scalar types returned directly
    int tag = JS_VALUE_GET_TAG (val);
    if (tag == JS_TAG_INT) {
	int32_t i;
	JS_ToInt32 (ctx, &i, val);
	if (hint == DynamicValue::Float) {
	    result->update (static_cast<float> (i));
	} else {
	    result->update (static_cast<int> (i));
	}
	return result;
    }
    if (tag == JS_TAG_BOOL) {
	result->update (static_cast<bool> (JS_ToBool (ctx, val)));
	return result;
    }
    if (JS_TAG_IS_FLOAT64 (tag)) {
	double d;
	JS_ToFloat64 (ctx, &d, val);
	result->update (static_cast<float> (d));
	return result;
    }

    // object - extract x/y/z/w properties based on the hint type
    if (tag == JS_TAG_OBJECT) {
	auto readFloat = [&] (const char* prop) -> float {
	    JSValue v = JS_GetPropertyStr (ctx, val, prop);
	    double d = 0.0;
	    if (!JS_IsException (v) && !JS_IsUndefined (v)) {
		JS_ToFloat64 (ctx, &d, v);
	    }
	    JS_FreeValue (ctx, v);
	    return static_cast<float> (d);
	};

	auto readInt = [&] (const char* prop) -> int {
	    JSValue v = JS_GetPropertyStr (ctx, val, prop);
	    int32_t i = 0;
	    if (!JS_IsException (v) && !JS_IsUndefined (v)) {
		JS_ToInt32 (ctx, &i, v);
	    }
	    JS_FreeValue (ctx, v);
	    return static_cast<int> (i);
	};

	switch (hint) {
	    case DynamicValue::Vec2:
		result->update (glm::vec2 (readFloat ("x"), readFloat ("y")));
		break;
	    case DynamicValue::Vec3:
		result->update (glm::vec3 (readFloat ("x"), readFloat ("y"), readFloat ("z")));
		break;
	    case DynamicValue::Vec4:
		result->update (
		    glm::vec4 (readFloat ("x"), readFloat ("y"), readFloat ("z"), readFloat ("w")));
		break;
	    case DynamicValue::IVec2:
		result->update (glm::ivec2 (readInt ("x"), readInt ("y")));
		break;
	    case DynamicValue::IVec3:
		result->update (glm::ivec3 (readInt ("x"), readInt ("y"), readInt ("z")));
		break;
	    case DynamicValue::IVec4:
		result->update (
		    glm::ivec4 (readInt ("x"), readInt ("y"), readInt ("z"), readInt ("w")));
		break;
	    default: {
		// try to read as vec3 by default (most common for origin/angles)
		float x = readFloat ("x");
		float y = readFloat ("y");
		float z = readFloat ("z");
		result->update (glm::vec3 (x, y, z));
		break;
	    }
	}
	return result;
    }

    // fallback: try as float
    double d;
    if (JS_ToFloat64 (ctx, &d, val) == 0) {
	result->update (static_cast<float> (d));
    }
    return result;
}

/// Helper to check for and log JS exceptions
static void logJSException (JSContext* ctx, const char* context) {
    JSValue exc = JS_GetException (ctx);
    if (!JS_IsNull (exc) && !JS_IsUndefined (exc)) {
	const char* str = JS_ToCString (ctx, exc);
	if (str) {
	    sLog.error ("ScriptEngine [", context, "]: ", str);
	    JS_FreeCString (ctx, str);
	}
    }
    JS_FreeValue (ctx, exc);
}

DynamicValueUniquePtr ScriptEngine::evaluate (
    const std::string& scriptSource,
    const std::map<std::string, DynamicValue*>& scriptProperties,
    const DynamicValue& currentValue
) {
    if (!this->m_context) {
	sLog.error ("ScriptEngine: No JS context available");
	auto fallback = std::make_unique<DynamicValue> ();
	fallback->update (currentValue);
	return fallback;
    }

    JSContext* ctx = this->m_context;

    // Build the scriptProperties object that createScriptProperties() will return
    JSValue propsObj = JS_NewObject (ctx);
    for (const auto& [name, dynVal] : scriptProperties) {
	if (dynVal) {
	    JS_SetPropertyStr (ctx, propsObj, name.c_str (), this->dynamicValueToJS (*dynVal));
	}
    }

    // We need to rewrite the ES6 module into a regular script that we can evaluate
    // and extract the update() function from, because QuickJS module evaluation
    // via JS_EVAL_TYPE_MODULE doesn't easily let us get at exported functions
    // from C in a straightforward way.
    //
    // Strategy: Replace the ES6 module pattern with a plain script that:
    // 1. Has createScriptProperties() available as a global
    // 2. Defines update() in global scope
    // 3. We call update() with the value object
    //
    // The script pattern is always:
    //   'use strict';
    //   export var scriptProperties = createScriptProperties()...finish();
    //   export function update(value) { ... }
    //
    // We transform this to a self-contained IIFE that we evaluate directly.

    // Set createScriptProperties as a global that returns a builder
    // The builder supports .addSlider({...}).addCheckbox({...}).finish()
    // and returns an object with the property values

    // Create the builder as a JS object with fluent methods
    // that ultimately resolves to the propsObj
    std::ostringstream wrapper;
    wrapper << "(function() {\n"
	    << "  var __props = globalThis.__scriptProps;\n"
	    // ── WPE engine global stub ──────────────────────────────────────
	    // Wallpaper Engine scripts expect a global `engine` object with
	    // frametime, registerAudioBuffers, and registerCallback. We stub
	    // these so scripts don't throw ReferenceError at load time. Audio
	    // buffers return zeros (audio reactive not yet wired); frametime
	    // is set per-eval from globalThis.__frametime.
	    << "  var engine = {\n"
	    << "    frametime: globalThis.__frametime || 0.016,\n"
	    << "    runtime: 0,\n"
	    << "    fps: 30,\n"
	    << "    registerAudioBuffers: function(resolution) {\n"
	    << "      resolution = resolution || 64;\n"
	    << "      var left = new Array(resolution); var right = new Array(resolution);\n"
	    << "      for (var i = 0; i < resolution; i++) { left[i] = 0; right[i] = 0; }\n"
	    << "      return [left, right];\n"
	    << "    },\n"
	    << "    registerCallback: function(name, fn) { /* no-op */ },\n"
	    << "    setTimeout: function(fn, ms) { /* no-op */ },\n"
	    << "    userProperties: __props,\n"
	    << "  };\n"
	    // ── thisScene shim with getLayer support ────────────────────────
	    // For evaluate() (property scripts), we don't have time/dt context,
	    // so thisScene is minimal — just getLayer for cross-layer mutation.
	    // getLayer accepts both string names and numeric indices, matching
	    // WPE editor conventions where init() iterates by index.
	    << "  var thisScene = {\n"
	    << "    getLayer: function(arg) {\n"
	    << "      if (typeof arg === 'number') {\n"
	    << "        var arr = globalThis.__sceneLayersByIndex;\n"
	    << "        return (arr && arr[arg]) ? arr[arg] : null;\n"
	    << "      }\n"
	    << "      var l = globalThis.__sceneLayers;\n"
	    << "      return (l && l[arg]) ? l[arg] : null;\n"
	    << "    },\n"
	    << "    getLayerCount: function() {\n"
	    << "      return globalThis.__sceneLayerCount || 0;\n"
	    << "    }\n"
	    << "  };\n"
	    // ── thisLayer stub for property scripts ────────────────────
	    // Property scripts (visible: {script: ...}) sometimes reference
	    // thisLayer.origin / thisLayer.visible to mutate the host layer.
	    // We don't know the host id at parse time without plumbing changes,
	    // so for now we stub a free-standing object. Writes to the stub
	    // are silently lost (no host mutation) but the script runs to its
	    // important logic (thisScene.getLayer(...) toggles).
	    << "  var thisLayer = {\n"
	    << "    visible: true,\n"
	    << "    alpha: 1.0,\n"
	    << "    origin: { x: 0, y: 0, z: 0 },\n"
	    << "    text: ''\n"
	    << "  };\n"
	    // ── Vec3 class stub ─────────────────────────────────────────────
	    << "  function Vec3(x, y, z) { this.x = x||0; this.y = y||0; this.z = z||0; }\n"
	    << "  Vec3.prototype.toString = function() { return this.x+' '+this.y+' '+this.z; };\n"
	    // ── Shared/MediaPlaybackEvent stubs ─────────────────────────────
	    << "  var shared = {};\n"
	    << "  var MediaPlaybackEvent = { state: 0 };\n"
	    // ────────────────────────────────────────────────────────────────
	    << "  function createScriptProperties() {\n"
	    << "    var builder = {\n"
	    << "      addSlider: function(opts) {\n"
	    << "        if (!(opts.name in __props)) __props[opts.name] = opts.value;\n"
	    << "        return builder;\n"
	    << "      },\n"
	    << "      addCheckbox: function(opts) {\n"
	    << "        if (!(opts.name in __props)) __props[opts.name] = opts.value;\n"
	    << "        return builder;\n"
	    << "      },\n"
	    << "      addCombo: function(opts) {\n"
	    << "        if (!(opts.name in __props)) __props[opts.name] = opts.value;\n"
	    << "        return builder;\n"
	    << "      },\n"
	    << "      addColor: function(opts) {\n"
	    << "        if (!(opts.name in __props)) __props[opts.name] = opts.value;\n"
	    << "        return builder;\n"
	    << "      },\n"
	    << "      addText: function(opts) {\n"
	    << "        if (!(opts.name in __props)) __props[opts.name] = opts.value;\n"
	    << "        return builder;\n"
	    << "      },\n"
	    << "      finish: function() { return __props; }\n"
	    << "    };\n"
	    << "    return builder;\n"
	    << "  }\n";

    // Strip 'use strict'; and export keywords, embed the script body
    std::string body = scriptSource;

    // Remove 'use strict'; declarations
    size_t pos;
    while ((pos = body.find ("'use strict';")) != std::string::npos) {
	body.erase (pos, 13);
    }
    while ((pos = body.find ("\"use strict\";")) != std::string::npos) {
	body.erase (pos, 13);
    }

    // Remove export keywords (export var ..., export function ...)
    while ((pos = body.find ("export ")) != std::string::npos) {
	body.erase (pos, 7);
    }

    wrapper << body << "\n"
	    // Some property scripts split state initialization into init()
	    // (e.g., iterating thisScene to build layer index arrays) and
	    // per-frame work into update(). createLayerScript runs init once
	    // and persists state across ticks; evaluate() runs as a fresh IIFE
	    // each call, so we always re-run init() to repopulate the IIFE's
	    // local state before update() reads it. init() failures are
	    // swallowed so update() still gets a chance to run.
	    << "  if (typeof init === 'function') {\n"
	    << "    try { init(); } catch (e) { /* init failure non-fatal */ }\n"
	    << "  }\n"
	    << "  if (typeof update === 'function') return update(globalThis.__currentValue);\n"
	    << "  return globalThis.__currentValue;\n"
	    << "})();\n";

    std::string evalScript = wrapper.str ();

    // Set globals: __scriptProps and __currentValue
    JSValue globalObj = JS_GetGlobalObject (ctx);
    JS_SetPropertyStr (ctx, globalObj, "__scriptProps", JS_DupValue (ctx, propsObj));
    JS_SetPropertyStr (ctx, globalObj, "__currentValue", this->dynamicValueToJS (currentValue));

    // Evaluate
    JSValue result = JS_Eval (ctx, evalScript.c_str (), evalScript.size (), "<script>", JS_EVAL_TYPE_GLOBAL);

    // Clean up globals
    JS_SetPropertyStr (ctx, globalObj, "__scriptProps", JS_UNDEFINED);
    JS_SetPropertyStr (ctx, globalObj, "__currentValue", JS_UNDEFINED);
    JS_FreeValue (ctx, globalObj);
    JS_FreeValue (ctx, propsObj);

    if (JS_IsException (result)) {
	logJSException (ctx, "evaluate");
	JS_FreeValue (ctx, result);
	auto fallback = std::make_unique<DynamicValue> ();
	fallback->update (currentValue);
	return fallback;
    }

    auto dynResult = this->jsToDynamicValue (result, currentValue.getType ());
    JS_FreeValue (ctx, result);
    return dynResult;
}

// ---------------------------------------------------------------------------
// Layer-script API (Phase 2)
// ---------------------------------------------------------------------------

void ScriptEngine::ensureLayerRegistry () {
    if (this->m_layerRegistryReady || !this->m_context) {
	return;
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);
    JS_SetPropertyStr (ctx, globalObj, "__layers", JS_NewObject (ctx));
    JS_FreeValue (ctx, globalObj);
    this->m_layerRegistryReady = true;
}

ScriptLayerHandle ScriptEngine::createLayerScript (
    const std::string& scriptSource,
    const std::map<std::string, DynamicValue*>& initialScriptProps,
    const std::string& initialText
) {
    if (!this->m_context) {
	sLog.error ("ScriptEngine: No JS context available");
	return kInvalidLayerHandle;
    }
    this->ensureLayerRegistry ();

    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);

    // Seed initial scriptProperties and text as temporary globals the IIFE reads.
    JSValue seedProps = JS_NewObject (ctx);
    for (const auto& [name, dynVal] : initialScriptProps) {
	if (dynVal) {
	    JS_SetPropertyStr (ctx, seedProps, name.c_str (), this->dynamicValueToJS (*dynVal));
	}
    }
    JS_SetPropertyStr (ctx, globalObj, "__layerSeedProps", seedProps);
    JS_SetPropertyStr (ctx, globalObj, "__layerSeedText", JS_NewString (ctx, initialText.c_str ()));

    const ScriptLayerHandle id = this->m_nextLayerId++;

    // Same stripping logic as evaluate(): WE scripts come as ES6 modules but
    // QuickJS is easier to drive as plain script evaluation.
    std::string body = scriptSource;
    size_t pos;
    while ((pos = body.find ("'use strict';")) != std::string::npos) {
	body.erase (pos, 13);
    }
    while ((pos = body.find ("\"use strict\";")) != std::string::npos) {
	body.erase (pos, 13);
    }
    while ((pos = body.find ("export ")) != std::string::npos) {
	body.erase (pos, 7);
    }

    // The IIFE gives every layer its own closure for top-level vars and
    // functions, so two layers that both define `function update()` or a
    // top-level `var scriptProperties` don't clobber each other. Lifecycle
    // hooks are captured into globalThis.__layers[id] so tick/destroy can
    // reach them later. `typeof init === 'function'` is safe even when
    // `init` was never declared — bare-identifier `typeof` never throws.
    std::ostringstream wrapper;
    wrapper << "(function() {\n"
	    << "  var __id = " << id << ";\n"
	    << "  var __props = Object.assign({}, globalThis.__layerSeedProps || {});\n"
	    << "  var thisLayer = { text: String(globalThis.__layerSeedText || '') };\n"
	    << "  var thisScene = {\n"
	    << "    get time()        { var c = globalThis.__sceneCtx; return c ? c.time : 0; },\n"
	    << "    get currentTime() { var c = globalThis.__sceneCtx; return c ? c.time : 0; },\n"
	    << "    get dt()          { var c = globalThis.__sceneCtx; return c ? c.dt   : 0; },\n"
	    << "    get fps()         { var c = globalThis.__sceneCtx; return c ? c.fps  : 60; },\n"
	    << "    getLayer: function(arg) {\n"
	    << "      if (typeof arg === 'number') {\n"
	    << "        var arr = globalThis.__sceneLayersByIndex;\n"
	    << "        return (arr && arr[arg]) ? arr[arg] : null;\n"
	    << "      }\n"
	    << "      var l = globalThis.__sceneLayers;\n"
	    << "      return (l && l[arg]) ? l[arg] : null;\n"
	    << "    },\n"
	    << "    getLayerCount: function() {\n"
	    << "      return globalThis.__sceneLayerCount || 0;\n"
	    << "    }\n"
	    << "  };\n"
	    // Minimal WE `engine` shim. Real Wallpaper Engine exposes a broad API
	    // (media events, audio buffer, user input); we provide just enough for
	    // the common built-in text scripts to run without ReferenceError.
	    // `frametime` is the per-frame delta in seconds (what InsertFPS reads).
	    << "  var engine = {\n"
	    << "    get frametime() { var c = globalThis.__sceneCtx; return c ? c.dt : 0; },\n"
	    << "    get time()      { var c = globalThis.__sceneCtx; return c ? c.time : 0; },\n"
	    << "    fps: 30,\n"
	    << "    runtime: 0,\n"
	    << "    registerAudioBuffers: function(res) {\n"
	    << "      res = res || 64;\n"
	    << "      var left = new Array(res); var right = new Array(res);\n"
	    << "      for(var i=0;i<res;i++){ left[i]=0; right[i]=0; }\n"
	    << "      return [left, right];\n"
	    << "    },\n"
	    << "    registerCallback: function(name, fn) { /* no-op */ },\n"
	    << "    setTimeout: function(fn, ms) { /* no-op */ },\n"
	    << "    userProperties: __props,\n"
	    << "  };\n"
	    << "  function Vec3(x,y,z){ this.x=x||0; this.y=y||0; this.z=z||0; }\n"
	    << "  Vec3.prototype.toString = function(){ return this.x+' '+this.y+' '+this.z; };\n"
	    << "  var shared = {};\n"
	    << "  var MediaPlaybackEvent = { state: 0 };\n"
	    << "  function createScriptProperties() {\n"
	    << "    var builder = {\n"
	    << "      addSlider:   function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	    << "      addCheckbox: function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	    << "      addCombo:    function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	    << "      addColor:    function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	    << "      addText:     function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	    << "      finish:      function(){ return __props; }\n"
	    << "    };\n"
	    << "    return builder;\n"
	    << "  }\n"
	    << body << "\n"
	    // `_tick` wraps the user's `update()` so both WE text conventions work:
	    //   A) `export function update() { thisLayer.text = …; }` (mutates in place)
	    //   B) `export function update(value) { …; return value; }` (returns new text)
	    // We pass the current text in, and if the return value is a string we
	    // adopt it as the new `thisLayer.text`. Non-string / undefined return
	    // leaves `thisLayer.text` as whatever the function assigned itself.
	    << "  globalThis.__layers[__id] = {\n"
	    << "    thisLayer: thisLayer,\n"
	    << "    thisScene: thisScene,\n"
	    << "    _init:    (typeof init    === 'function') ? init    : null,\n"
	    << "    _destroy: (typeof destroy === 'function') ? destroy : null,\n"
	    << "    _tick:    (typeof update  === 'function')\n"
	    << "              ? function() {\n"
	    << "                  var r = update(thisLayer.text);\n"
	    << "                  if (typeof r === 'string') thisLayer.text = r;\n"
	    << "                }\n"
	    << "              : null,\n"
	    << "    _scriptProperties: (typeof scriptProperties !== 'undefined') ? scriptProperties : __props\n"
	    << "  };\n"
	    << "})();\n";

    const std::string evalScript = wrapper.str ();
    JSValue result = JS_Eval (ctx, evalScript.c_str (), evalScript.size (), "<layer-script>", JS_EVAL_TYPE_GLOBAL);

    // Unset seeds so they don't leak into the next createLayerScript call.
    JS_SetPropertyStr (ctx, globalObj, "__layerSeedProps", JS_UNDEFINED);
    JS_SetPropertyStr (ctx, globalObj, "__layerSeedText", JS_UNDEFINED);
    JS_FreeValue (ctx, globalObj);

    if (JS_IsException (result)) {
	logJSException (ctx, "createLayerScript");
	JS_FreeValue (ctx, result);
	return kInvalidLayerHandle;
    }
    JS_FreeValue (ctx, result);

    this->m_layerInitialized[id] = false;
    return id;
}

void ScriptEngine::tickLayer (ScriptLayerHandle handle, double time, double deltaTime, double fps) {
    if (!this->m_context || handle == kInvalidLayerHandle) {
	return;
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);

    JSValue sceneCtx = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, sceneCtx, "time", JS_NewFloat64 (ctx, time));
    JS_SetPropertyStr (ctx, sceneCtx, "dt",   JS_NewFloat64 (ctx, deltaTime));
    JS_SetPropertyStr (ctx, sceneCtx, "fps",  JS_NewFloat64 (ctx, fps));
    JS_SetPropertyStr (ctx, globalObj, "__sceneCtx", sceneCtx);

    JSValue layers = JS_GetPropertyStr (ctx, globalObj, "__layers");
    JSValue layerObj = JS_GetPropertyUint32 (ctx, layers, static_cast<uint32_t> (handle));
    JS_FreeValue (ctx, layers);

    if (JS_IsUndefined (layerObj) || JS_IsNull (layerObj)) {
	JS_FreeValue (ctx, layerObj);
	JS_FreeValue (ctx, globalObj);
	return;
    }

    auto callHook = [&] (const char* prop, const char* tag) {
	JSValue fn = JS_GetPropertyStr (ctx, layerObj, prop);
	if (JS_IsFunction (ctx, fn)) {
	    JSValue ret = JS_Call (ctx, fn, layerObj, 0, nullptr);
	    if (JS_IsException (ret)) {
		logJSException (ctx, tag);
	    }
	    JS_FreeValue (ctx, ret);
	}
	JS_FreeValue (ctx, fn);
    };

    auto it = this->m_layerInitialized.find (handle);
    if (it != this->m_layerInitialized.end () && !it->second) {
	callHook ("_init", "layer.init");
	it->second = true;
    }
    callHook ("_tick", "layer.update");

    JS_FreeValue (ctx, layerObj);
    JS_FreeValue (ctx, globalObj);
}

std::string ScriptEngine::layerText (ScriptLayerHandle handle) {
    if (!this->m_context || handle == kInvalidLayerHandle) {
	return {};
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);
    JSValue layers = JS_GetPropertyStr (ctx, globalObj, "__layers");
    JSValue layerObj = JS_GetPropertyUint32 (ctx, layers, static_cast<uint32_t> (handle));

    std::string result;
    if (!JS_IsUndefined (layerObj) && !JS_IsNull (layerObj)) {
	JSValue thisLayer = JS_GetPropertyStr (ctx, layerObj, "thisLayer");
	JSValue textVal = JS_GetPropertyStr (ctx, thisLayer, "text");
	if (!JS_IsUndefined (textVal) && !JS_IsNull (textVal)) {
	    const char* cstr = JS_ToCString (ctx, textVal);
	    if (cstr) {
		result.assign (cstr);
		JS_FreeCString (ctx, cstr);
	    }
	}
	JS_FreeValue (ctx, textVal);
	JS_FreeValue (ctx, thisLayer);
    }

    JS_FreeValue (ctx, layerObj);
    JS_FreeValue (ctx, layers);
    JS_FreeValue (ctx, globalObj);
    return result;
}

void ScriptEngine::destroyLayer (ScriptLayerHandle handle) {
    if (!this->m_context || handle == kInvalidLayerHandle) {
	return;
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);
    JSValue layers = JS_GetPropertyStr (ctx, globalObj, "__layers");
    JSValue layerObj = JS_GetPropertyUint32 (ctx, layers, static_cast<uint32_t> (handle));

    if (!JS_IsUndefined (layerObj) && !JS_IsNull (layerObj)) {
	JSValue fn = JS_GetPropertyStr (ctx, layerObj, "_destroy");
	if (JS_IsFunction (ctx, fn)) {
	    JSValue ret = JS_Call (ctx, fn, layerObj, 0, nullptr);
	    if (JS_IsException (ret)) {
		logJSException (ctx, "layer.destroy");
	    }
	    JS_FreeValue (ctx, ret);
	}
	JS_FreeValue (ctx, fn);
    }
    JS_FreeValue (ctx, layerObj);
    JS_FreeValue (ctx, layers);
    JS_FreeValue (ctx, globalObj);

    // Remove the entry from globalThis.__layers so GC can reclaim its closures.
    const std::string delScript = "delete globalThis.__layers[" + std::to_string (handle) + "];";
    JSValue delResult = JS_Eval (ctx, delScript.c_str (), delScript.size (), "<layer-destroy>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException (delResult)) {
	logJSException (ctx, "layer.destroy.delete");
    }
    JS_FreeValue (ctx, delResult);

    this->m_layerInitialized.erase (handle);
}

// ---------------------------------------------------------------------------
// Scene-aware API (Phase 4)
// ---------------------------------------------------------------------------
//
// WPE scripts use `thisScene.getLayer(name).visible = true/false` to toggle
// other layers' visibility from a script attached to one object's `visible`
// field. To support this we need the scripting engine to know which scene
// is active, so it can resolve names to CObjects and mutate their backing
// UserSetting DynamicValues.
//
// The C-side functions below are exposed to QuickJS via JS_NewCFunction.
// The JS side builds proxy objects (one per layer) whose `visible`/`alpha`
// getters/setters call into these.

namespace {

// Helper: pull the singleton out of QuickJS context. Static so we can
// reference it from extern "C"-style C callbacks.
ScriptEngine& engineForCallback () {
    return ScriptEngine::instance ();
}

// JS: __getLayerVisible(id) -> bool
JSValue js_getLayerVisible (JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1) {
	return JS_FALSE;
    }
    int32_t id = 0;
    if (JS_ToInt32 (ctx, &id, argv[0]) != 0) {
	return JS_FALSE;
    }
    return JS_NewBool (ctx, engineForCallback ().getLayerVisible (id));
}

// JS: __setLayerVisible(id, value) -> undefined
JSValue js_setLayerVisible (JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 2) {
	return JS_UNDEFINED;
    }
    int32_t id = 0;
    if (JS_ToInt32 (ctx, &id, argv[0]) != 0) {
	return JS_UNDEFINED;
    }
    const bool val = JS_ToBool (ctx, argv[1]) == 1;
    engineForCallback ().setLayerVisible (id, val);
    return JS_UNDEFINED;
}

// JS: __getLayerAlpha(id) -> number
JSValue js_getLayerAlpha (JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1) {
	return JS_NewFloat64 (ctx, 1.0);
    }
    int32_t id = 0;
    if (JS_ToInt32 (ctx, &id, argv[0]) != 0) {
	return JS_NewFloat64 (ctx, 1.0);
    }
    return JS_NewFloat64 (ctx, engineForCallback ().getLayerAlpha (id));
}

// JS: __setLayerAlpha(id, value) -> undefined
JSValue js_setLayerAlpha (JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 2) {
	return JS_UNDEFINED;
    }
    int32_t id = 0;
    if (JS_ToInt32 (ctx, &id, argv[0]) != 0) {
	return JS_UNDEFINED;
    }
    double val = 1.0;
    if (JS_ToFloat64 (ctx, &val, argv[1]) != 0) {
	val = 1.0;
    }
    engineForCallback ().setLayerAlpha (id, static_cast<float> (val));
    return JS_UNDEFINED;
}

} // anonymous namespace

void ScriptEngine::setScene (CScene* scene) {
    this->m_currentScene = scene;
    if (this->m_context && scene != nullptr) {
	this->buildSceneLayerRegistry ();
    }
}

void ScriptEngine::clearScene () {
    if (this->m_context && this->m_sceneRegistryReady) {
	this->teardownSceneLayerRegistry ();
    }
    this->m_currentScene = nullptr;
}

int ScriptEngine::findObjectIdByName (const std::string& name) const {
    if (this->m_currentScene == nullptr) {
	return 0;
    }
    for (const auto* obj : this->m_currentScene->getObjectsByRenderOrder ()) {
	if (obj == nullptr) {
	    continue;
	}
	if (obj->getObject ().name == name) {
	    return obj->getId ();
	}
    }
    return 0;
}

bool ScriptEngine::getLayerVisible (int objectId) const {
    if (this->m_currentScene == nullptr) {
	return false;
    }
    const CObject* obj = this->m_currentScene->getObject (objectId);
    if (obj == nullptr) {
	return false;
    }
    // Image is the most common case for getLayer toggling. Others can be
    // added later (Sound, Particle, Text) but visibility on those is rare.
    if (obj->is<CImage> ()) {
	const auto& image = obj->as<CImage> ()->getImage ();
	if (image.visible && image.visible->value) {
	    return image.visible->value->getBool ();
	}
    }
    return false;
}

void ScriptEngine::setLayerVisible (int objectId, bool value) {
    if (this->m_currentScene == nullptr) {
	return;
    }
    const CObject* obj = this->m_currentScene->getObject (objectId);
    if (obj == nullptr) {
	return;
    }
    if (obj->is<CImage> ()) {
	const auto& image = obj->as<CImage> ()->getImage ();
	if (image.visible && image.visible->value) {
	    image.visible->value->update (value);
	}
    }
}

float ScriptEngine::getLayerAlpha (int objectId) const {
    if (this->m_currentScene == nullptr) {
	return 1.0f;
    }
    const CObject* obj = this->m_currentScene->getObject (objectId);
    if (obj == nullptr) {
	return 1.0f;
    }
    if (obj->is<CImage> ()) {
	const auto& image = obj->as<CImage> ()->getImage ();
	if (image.alpha && image.alpha->value) {
	    return image.alpha->value->getFloat ();
	}
    }
    return 1.0f;
}

void ScriptEngine::setLayerAlpha (int objectId, float value) {
    if (this->m_currentScene == nullptr) {
	return;
    }
    const CObject* obj = this->m_currentScene->getObject (objectId);
    if (obj == nullptr) {
	return;
    }
    if (obj->is<CImage> ()) {
	const auto& image = obj->as<CImage> ()->getImage ();
	if (image.alpha && image.alpha->value) {
	    image.alpha->value->update (value);
	}
    }
}

void ScriptEngine::buildSceneLayerRegistry () {
    if (!this->m_context || this->m_currentScene == nullptr) {
	return;
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);

    // Register C accessor functions if not already done. They live on
    // globalThis so all scripts can reach them through their proxies.
    JSValue getVis = JS_NewCFunction (ctx, js_getLayerVisible, "__getLayerVisible", 1);
    JSValue setVis = JS_NewCFunction (ctx, js_setLayerVisible, "__setLayerVisible", 2);
    JSValue getA = JS_NewCFunction (ctx, js_getLayerAlpha, "__getLayerAlpha", 1);
    JSValue setA = JS_NewCFunction (ctx, js_setLayerAlpha, "__setLayerAlpha", 2);
    JS_SetPropertyStr (ctx, globalObj, "__getLayerVisible", getVis);
    JS_SetPropertyStr (ctx, globalObj, "__setLayerVisible", setVis);
    JS_SetPropertyStr (ctx, globalObj, "__getLayerAlpha", getA);
    JS_SetPropertyStr (ctx, globalObj, "__setLayerAlpha", setA);

    // Build the __sceneLayers map: { layerName: { __id, get visible, set visible, get alpha, set alpha } }.
    // Also build __sceneLayersByIndex (array) and __sceneLayerCount so scripts
    // can iterate by integer index as the WPE editor convention does.
    // We construct it via JS rather than the C API because property
    // descriptors with getters/setters are easier in JS syntax.
    std::ostringstream js;
    js << "globalThis.__sceneLayers = {};\n"
       << "globalThis.__sceneLayersByIndex = [];\n"
       << "globalThis.__buildLayerProxy = function(id, name) {\n"
       << "  return {\n"
       << "    __id: id,\n"
       << "    name: name,\n"
       << "    get visible() { return globalThis.__getLayerVisible(id); },\n"
       << "    set visible(v) { globalThis.__setLayerVisible(id, v ? true : false); },\n"
       << "    get alpha()   { return globalThis.__getLayerAlpha(id); },\n"
       << "    set alpha(v)  { globalThis.__setLayerAlpha(id, v); }\n"
       << "  };\n"
       << "};\n";

    int layerIndex = 0;
    for (const auto* obj : this->m_currentScene->getObjectsByRenderOrder ()) {
	if (obj == nullptr) {
	    continue;
	}
	const std::string& name = obj->getObject ().name;
	if (name.empty ()) {
	    continue;
	}
	// Escape quotes/backslashes in the name so JS object key is safe.
	std::string safe;
	safe.reserve (name.size () + 4);
	for (char c : name) {
	    if (c == '\\' || c == '"' || c == '\'') {
		safe.push_back ('\\');
	    }
	    safe.push_back (c);
	}
	js << "globalThis.__sceneLayersByIndex[" << layerIndex << "] = "
	   << "globalThis.__sceneLayers['" << safe << "'] = "
	   << "globalThis.__buildLayerProxy (" << obj->getId () << ", '" << safe << "');\n";
	++layerIndex;
    }
    js << "globalThis.__sceneLayerCount = " << layerIndex << ";\n";

    const std::string boot = js.str ();
    JSValue result = JS_Eval (ctx, boot.c_str (), boot.size (), "<scene-layer-registry>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException (result)) {
	JSValue exception = JS_GetException (ctx);
	const char* msg = JS_ToCString (ctx, exception);
	sLog.error ("ScriptEngine::buildSceneLayerRegistry exception: ", msg ? msg : "(null)");
	if (msg) {
	    JS_FreeCString (ctx, msg);
	}
	JS_FreeValue (ctx, exception);
    }
    JS_FreeValue (ctx, result);

    JS_FreeValue (ctx, globalObj);

    this->m_sceneRegistryReady = true;
}

void ScriptEngine::teardownSceneLayerRegistry () {
    if (!this->m_context) {
	return;
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);
    JS_SetPropertyStr (ctx, globalObj, "__sceneLayers", JS_UNDEFINED);
    JS_FreeValue (ctx, globalObj);
    this->m_sceneRegistryReady = false;
}
