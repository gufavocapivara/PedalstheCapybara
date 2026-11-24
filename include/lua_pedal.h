// lua_pedal.h - Pedais e amps escritos em Lua, com suporte a imagens.
//
// Imagens podem vir de:
//   - Plugins/<script>_assets/...        (plugin em pasta)
//   - dentro de um .pctp/.pp            (pacote ZIP, sem extrair)
//   - assets do editor                   (bytes em memoria, preview imediato)
#pragma once
#include <atomic>
#include <lua.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assets.h"
#include "amps.h"
#include "plugins.h"
#include "pedals.h"
#include "textures.h"

namespace gp {

    enum LuaWidget { kButtonSpin = 1, kSlider = 2 };
    using LuaAssetMap = std::map<std::string, std::vector<unsigned char>>;

    class LuaPedal : public Pedal {
    public:
        static constexpr int kMaxParams = 128;

        std::string bar;
        std::string error;
        std::vector<int> widgets;
        std::atomic<bool> broken{ false };
        bool isTest = false;
        float uiW = 0.0f, uiH = 0.0f;
        bool oversample = false;
        char rtErr[256] = { 0 };

        TextureSet tex;
        std::filesystem::path assetsDir;
        std::filesystem::path scriptPath;
        std::string rootPrefix;
        std::string scriptEntry;
        std::string scriptStem;
        std::shared_ptr<PluginPackage> package;
        std::shared_ptr<const LuaAssetMap> memoryAssets;
        std::vector<std::string> missingAssets;
        TextureCache* texCache = nullptr;

        // Plugin em pasta (Plugins/foo.lua).
        explicit LuaPedal(const std::filesystem::path& script, TextureCache* cache = nullptr)
            : Pedal("Lua Pedal"), texCache(cache), scriptPath(script) {
            scriptEntry = script.filename().string();
            scriptStem = script.stem().string();
            assetsDir = script.parent_path() / (scriptStem + "_assets");
            instanceId_ = nextInstanceId_++;
            if (!setup()) return;
            std::string source;
            if (!readScriptFile(script, source)) {
                error = "Could not read " + script.filename().string();
                return;
            }
            const std::string chunk = "@" + script.filename().string();
            init(luaL_loadbuffer(L, source.data(), source.size(), chunk.c_str()));
        }

        // Plugin dentro de um .pctp/.pp.
        LuaPedal(std::shared_ptr<PluginPackage> pkg, std::string root, std::string entry,
                 TextureCache* cache = nullptr)
            : Pedal("Lua Pedal"), texCache(cache), rootPrefix(std::move(root)),
              scriptEntry(std::move(entry)), package(std::move(pkg)) {
            scriptStem = pathStem(scriptEntry);
            instanceId_ = nextInstanceId_++;
            if (!setup()) return;
            if (!package) { error = "Package is not available."; return; }
            std::string source, err;
            if (!package->readText(scriptEntry, source, err)) { error = err; return; }
            const std::string chunk = "@" + pathBasename(scriptEntry);
            init(luaL_loadbuffer(L, source.data(), source.size(), chunk.c_str()));
        }

        // Codigo do editor (com assets em memoria para preview).
        LuaPedal(const std::string& source, const char* chunkName,
                 TextureCache* cache = nullptr,
                 std::shared_ptr<const LuaAssetMap> memory = nullptr)
            : Pedal("Lua Pedal"), texCache(cache), scriptStem("editor"),
              memoryAssets(std::move(memory)) {
            assetsDir = std::filesystem::current_path();
            instanceId_ = nextInstanceId_++;
            if (!setup()) return;
            init(luaL_loadbuffer(L, source.data(), source.size(), chunkName));
        }

        ~LuaPedal() override {
            if (texCache) {
                // Solta exatamente as referências adquiridas por loadImage().
                for (TextureId id : loadedTextures_) texCache->release(id);
                loadedTextures_.clear();
            }
            if (L) lua_close(L);
        }

        void prepare(float sr) override {
            Pedal::prepare(sr);
            if (!L || broken.load(std::memory_order_relaxed)) return;

            const float runSR = oversample ? sampleRate * 2.0f : sampleRate;
            lua_pushnumber(L, runSR);
            lua_setglobal(L, "SAMPLE_RATE");
            if (prepRef != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, prepRef);
                lua_pushnumber(L, runSR);
                if (lua_pcall(L, 1, 0, 0) != LUA_OK) setRuntimeError();
            }

            if (oversample) {
                const float fc = sampleRate * 0.45f;
                osUp1.lowpass(runSR, fc, 0.54119610f);
                osUp2.lowpass(runSR, fc, 1.30656296f);
                osDn1.lowpass(runSR, fc, 0.54119610f);
                osDn2.lowpass(runSR, fc, 1.30656296f);
                osUp1.reset(); osUp2.reset(); osDn1.reset(); osDn2.reset();
                osBuf.assign(kMaxBlock * 2, 0.0f);
            }
        }

        void reset() override {
            if (!L || broken.load(std::memory_order_relaxed) || resetRef == LUA_NOREF) return;
            lua_rawgeti(L, LUA_REGISTRYINDEX, resetRef);
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) setRuntimeError();
        }

        void process(float* x, int n) override {
            if (!L || broken.load(std::memory_order_relaxed) || procRef == LUA_NOREF) return;
            if (n <= 0) return;
            const int np = (int)params.size();
            if (oversample && blockMode) processOversampled(x, n, np);
            else if (blockMode) processBlock(x, n, np);
            else processSample(x, n, np);
        }

        // Carrega uma imagem pelo nome, tentando todas as pastas convencionais.
        TextureId loadImage(const std::string& rawName, const std::string& rawModule = {}) {
            if (!texCache) return 0;
            std::string name = normalizeZipPath(rawName);
            if (name.empty()) return 0;
            const std::string module = rawModule.empty() ? scriptStem : normalizeZipPath(rawModule);

            // 1) assets do editor (bytes em memoria)
            if (memoryAssets) {
                const auto* entry = findMemoryAsset(name);
                if (entry) {
                    const std::string key = "editor:" + std::to_string(instanceId_) + ":" + entry->first;
                    TextureId id = texCache->loadFromMemory(key, entry->second.data(), entry->second.size());
                    if (id) loadedTextures_.push_back(id);
                    return id;
                }
                addMissing(rawName);
                return 0;
            }

            // 2) dentro de um .pctp/.pp
            if (package) {
                const PackageEntry* entry = package->findAsset(name, module, rootPrefix, scriptStem);
                if (!entry) { addMissing(rawName); return 0; }
                std::vector<unsigned char> bytes;
                std::string err;
                if (!package->readEntry(entry->name, bytes, err)) { addMissing(rawName); return 0; }
                const std::string key = package->displayName() + "#" + entry->name;
                TextureId id = texCache->loadFromMemory(key, bytes.data(), bytes.size());
                if (id) loadedTextures_.push_back(id);
                return id;
            }

            // 3) plugin em pasta
            std::vector<std::filesystem::path> candidates;
            const auto base = scriptPath.parent_path();
            auto add = [&](const std::filesystem::path& p) {
                if (!p.empty()) candidates.push_back(p);
            };
            add(assetsDir / name);
            if (!base.empty()) {
                add(base / module / name);
                add(base / (module + "_assets") / name);
                add(base / (scriptStem + "_assets") / name);
                add(base / "assets" / name);
                add(base / "images" / name);
                add(base / name);
            }
            std::error_code ec;
            add(std::filesystem::current_path(ec) / name);
            add(std::filesystem::current_path(ec) / "assets" / name);

            for (const auto& p : candidates) {
                if (std::filesystem::exists(p, ec) && !ec) {
                    TextureId id = texCache->load(p);
                    if (id) { loadedTextures_.push_back(id); return id; }
                }
            }
            addMissing(rawName);
            return 0;
        }

    private:
        lua_State* L = nullptr;
        int pedalRef = LUA_NOREF, procRef = LUA_NOREF, resetRef = LUA_NOREF, prepRef = LUA_NOREF;
        int bufRef = LUA_NOREF;
        bool blockMode = true;
        Biquad osUp1, osUp2, osDn1, osDn2;
        std::vector<float> osBuf;
        std::vector<TextureId> loadedTextures_;
        std::uint64_t instanceId_ = 0;
        inline static std::atomic<std::uint64_t> nextInstanceId_{ 1 };

        static bool readScriptFile(const std::filesystem::path& path, std::string& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            std::stringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        void addMissing(const std::string& name) {
            if (std::find(missingAssets.begin(), missingAssets.end(), name) == missingAssets.end())
                missingAssets.push_back(name);
        }

        const std::pair<const std::string, std::vector<unsigned char>>* findMemoryAsset(const std::string& name) const {
            if (!memoryAssets) return nullptr;
            auto exact = memoryAssets->find(name);
            if (exact != memoryAssets->end()) return &*exact;
            const std::string wanted = lowerAscii(pathBasename(name));
            for (const auto& kv : *memoryAssets) {
                if (lowerAscii(pathBasename(kv.first)) == wanted) return &kv;
            }
            return nullptr;
        }

        void setRuntimeError() {
            const char* m = lua_tostring(L, -1);
            std::snprintf(rtErr, sizeof(rtErr), "%s", m ? m : "error");
            lua_settop(L, 0);
            broken.store(true, std::memory_order_release);
        }

        void processBlock(float* x, int n, int np) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, procRef);
            lua_rawgeti(L, LUA_REGISTRYINDEX, bufRef);
            for (int i = 0; i < n; ++i) {
                lua_pushnumber(L, x[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_pushinteger(L, n);
            for (auto& p : params) lua_pushnumber(L, p->nextBlock(n, sampleRate));
            if (lua_pcall(L, 2 + np, 1, 0) != LUA_OK) { setRuntimeError(); return; }
            if (lua_istable(L, -1)) {
                for (int i = 0; i < n; ++i) {
                    lua_rawgeti(L, -1, i + 1);
                    if (lua_isnumber(L, -1)) {
                        float y = (float)lua_tonumber(L, -1);
                        x[i] = std::isfinite(y) ? y : 0.0f;
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }

        void processOversampled(float* x, int n, int np) {
            if ((size_t)n * 2 > osBuf.size()) return;
            float* up = osBuf.data();
            for (int i = 0; i < n; ++i) {
                up[2 * i]     = osUp2.process(osUp1.process(2.0f * x[i]));
                up[2 * i + 1] = osUp2.process(osUp1.process(0.0f));
            }

            lua_rawgeti(L, LUA_REGISTRYINDEX, procRef);
            lua_rawgeti(L, LUA_REGISTRYINDEX, bufRef);
            for (int i = 0; i < 2 * n; ++i) {
                lua_pushnumber(L, up[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_pushinteger(L, 2 * n);
            for (auto& p : params) lua_pushnumber(L, p->nextBlock(n, sampleRate));
            if (lua_pcall(L, 2 + np, 1, 0) != LUA_OK) { setRuntimeError(); return; }
            if (lua_istable(L, -1)) {
                for (int i = 0; i < 2 * n; ++i) {
                    lua_rawgeti(L, -1, i + 1);
                    if (lua_isnumber(L, -1)) {
                        float y = (float)lua_tonumber(L, -1);
                        up[i] = std::isfinite(y) ? y : 0.0f;
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);

            for (int i = 0; i < n; ++i) {
                float a = osDn2.process(osDn1.process(up[2 * i]));
                float b = osDn2.process(osDn1.process(up[2 * i + 1]));
                x[i] = 0.5f * (a + b);
            }
        }

        void processSample(float* x, int n, int np) {
            for (int i = 0; i < n; ++i) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, procRef);
                lua_pushnumber(L, x[i]);
                for (auto& p : params) lua_pushnumber(L, p->next());
                if (lua_pcall(L, 1 + np, 1, 0) != LUA_OK) { setRuntimeError(); return; }
                if (lua_isnumber(L, -1)) {
                    float y = (float)lua_tonumber(L, -1);
                    x[i] = std::isfinite(y) ? y : 0.0f;
                }
                lua_pop(L, 1);
            }
        }

        bool setup() {
            L = luaL_newstate();
            if (!L) { error = "not enough memory for Lua"; return false; }
            openSafeLibs();
            registerApi();
            lua_checkstack(L, kMaxParams + 2 * kMaxBlock + 64);
            return true;
        }

        void init(int loadResult) {
            if (loadResult != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) { fail(); return; }
            if (pedalRef == LUA_NOREF) { error = "the script did not call np() or newamp()"; return; }
            lua_createtable(L, kMaxBlock * 2, 0);
            bufRef = luaL_ref(L, LUA_REGISTRYINDEX);
            procRef = fieldRef("process");
            resetRef = fieldRef("reset");
            prepRef = fieldRef("prepare");
            if (procRef == LUA_NOREF) error = "define pedal.process = function(...) ... end";
            lua_gc(L, LUA_GCCOLLECT, 0);
        }

        void fail() {
            const char* m = lua_tostring(L, -1);
            error = m ? m : "unknown script error";
            lua_pop(L, 1);
        }

        int fieldRef(const char* key) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, pedalRef);
            lua_getfield(L, -1, key);
            int ref = LUA_NOREF;
            if (lua_isfunction(L, -1)) ref = luaL_ref(L, LUA_REGISTRYINDEX);
            else lua_pop(L, 1);
            lua_pop(L, 1);
            return ref;
        }

        void openSafeLibs() {
            static const luaL_Reg libs[] = {
                { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table },
                { LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math },
                { LUA_UTF8LIBNAME, luaopen_utf8 }, { nullptr, nullptr } };
            for (const luaL_Reg* l = libs; l->func; ++l) { luaL_requiref(L, l->name, l->func, 1); lua_pop(L, 1); }
            lua_pushnil(L); lua_setglobal(L, "dofile");
            lua_pushnil(L); lua_setglobal(L, "loadfile");
        }

        void reg(const char* name, lua_CFunction f) {
            lua_pushlightuserdata(L, this);
            lua_pushcclosure(L, f, 1);
            lua_setglobal(L, name);
        }
        void method(const char* key, lua_CFunction f) {
            lua_pushlightuserdata(L, this);
            lua_pushcclosure(L, f, 1);
            lua_setfield(L, -2, key);
        }

        void addCommonMethods() {
            method("instance", l_instance);
            method("color", l_color);
            method("linkbar", l_linkbar);
            method("image", l_image);
            method("basetexture", l_basetexture);
            method("texture", l_basetexture);
            method("knobtexture", l_knobtexture);
            method("knobshadowtexture", l_knobshadowtexture);
            method("ledtexture", l_ledtexture);
            method("footswitchtexture", l_footswitchtexture);
            method("labeltexture", l_labeltexture);
            method("overlaytexture", l_overlaytexture);
            method("oversample", l_oversample);
            method("size", l_size);
            method("labels", l_labels);
        }

        void registerApi() {
            reg("newassetsbar", l_newassetsbar);
            reg("np", l_np);
            reg("newamp", l_newamp);
            reg("loadassets", l_loadassets);
            lua_pushinteger(L, kButtonSpin); lua_setglobal(L, "BUTTONSPIN");
            lua_pushinteger(L, kSlider);     lua_setglobal(L, "SLIDER");
            lua_pushnumber(L, 48000);        lua_setglobal(L, "SAMPLE_RATE");
            lua_getglobal(L, "math");
            lua_pushcfunction(L, l_tanh); lua_setfield(L, -2, "tanh");
            lua_pop(L, 1);
            lua_pushcfunction(L, l_clamp);  lua_setglobal(L, "clamp");
            lua_pushcfunction(L, l_db2lin); lua_setglobal(L, "db2lin");
        }

        static LuaPedal* self(lua_State* L) { return (LuaPedal*)lua_touserdata(L, lua_upvalueindex(1)); }
        static int firstArg(lua_State* L) { return lua_istable(L, 1) ? 1 : 0; }

        static int l_loadassets(lua_State* L) {
            LuaPedal* s = self(L);
            const char* module = luaL_optstring(L, 1, "");
            lua_newtable(L);

            lua_pushlightuserdata(L, s);
            lua_pushstring(L, module);
            lua_pushcclosure(L, l_loadimage, 2);
            lua_setfield(L, -2, "loadimage");

            lua_pushlightuserdata(L, s);
            lua_pushstring(L, module);
            lua_pushcclosure(L, l_imagepast, 2);
            lua_setfield(L, -2, "imagepast");
            return 1;
        }

        static int l_imagepast(lua_State* L) {
            LuaPedal* s = self(L);
            const char* module = lua_tostring(L, lua_upvalueindex(2));
            lua_newtable(L);
            lua_pushlightuserdata(L, s);
            lua_pushstring(L, module ? module : "");
            lua_pushcclosure(L, l_loadimage, 2);
            lua_setfield(L, -2, "loadimage");
            return 1;
        }

        static int l_loadimage(lua_State* L) {
            LuaPedal* s = self(L);
            const char* name = luaL_checkstring(L, 1);
            const char* module = lua_tostring(L, lua_upvalueindex(2));
            lua_pushinteger(L, (lua_Integer)s->loadImage(name, module ? module : ""));
            return 1;
        }

        static int l_newamp(lua_State* L) {
            LuaPedal* s = self(L);
            if (s->pedalRef != LUA_NOREF) return luaL_error(L, "use np() or newamp() only once per script");
            s->isAmp = true;
            s->name = luaL_optstring(L, 1, "New Amp");
            lua_newtable(L);
            s->addCommonMethods();
            lua_pushvalue(L, -1);
            s->pedalRef = luaL_ref(L, LUA_REGISTRYINDEX);
            return 1;
        }

        static int l_newassetsbar(lua_State* L) {
            luaL_checkstring(L, 1);
            lua_settop(L, 1);
            return 1;
        }

        static int l_np(lua_State* L) {
            LuaPedal* s = self(L);
            if (s->pedalRef != LUA_NOREF) return luaL_error(L, "use np() or newamp() only once per script");
            s->isAmp = false;
            s->name = luaL_optstring(L, 1, "Lua Pedal");
            lua_newtable(L);
            s->addCommonMethods();
            lua_pushvalue(L, -1);
            s->pedalRef = luaL_ref(L, LUA_REGISTRYINDEX);
            return 1;
        }

        static int l_instance(lua_State* L) {
            LuaPedal* s = self(L);
            int a = firstArg(L);
            int kind = (int)luaL_checkinteger(L, a + 1);
            const char* nm = luaL_checkstring(L, a + 2);
            float mn = (float)luaL_optnumber(L, a + 3, 0.0);
            float mx = (float)luaL_optnumber(L, a + 4, 1.0);
            float df = (float)luaL_optnumber(L, a + 5, (mn + mx) * 0.5);
            if ((int)s->params.size() >= kMaxParams) return luaL_error(L, "maximum of %d parameters", kMaxParams);
            s->add(nm, mn, mx, df);
            s->params.back()->discrete = (kind == kButtonSpin);
            s->widgets.push_back(kind);
            return 0;
        }

        static int l_labels(lua_State* L) {
            LuaPedal* s = self(L);
            int a = firstArg(L);
            const char* nm = luaL_checkstring(L, a + 1);
            luaL_checktype(L, a + 2, LUA_TTABLE);
            int count = (int)lua_rawlen(L, a + 2);
            for (auto& p : s->params) {
                if (p->name != nm) continue;
                p->labels.clear();
                for (int i = 1; i <= count; ++i) {
                    lua_rawgeti(L, a + 2, i);
                    const char* t = lua_tostring(L, -1);
                    p->labels.push_back(t ? t : "");
                    lua_pop(L, 1);
                }
                return 0;
            }
            return luaL_error(L, "parameter '%s' does not exist", nm);
        }

        static int l_oversample(lua_State* L) {
            LuaPedal* s = self(L);
            s->oversample = lua_toboolean(L, firstArg(L) + 1) != 0;
            return 0;
        }

        static int l_size(lua_State* L) {
            LuaPedal* s = self(L);
            int a = firstArg(L);
            float w = (float)luaL_optnumber(L, a + 1, 0.0);
            float h = (float)luaL_optnumber(L, a + 2, 0.0);
            s->uiW = std::clamp(w, 0.0f, 4096.0f);
            s->uiH = std::clamp(h, 0.0f, 4096.0f);
            return 0;
        }

        static int l_color(lua_State* L) {
            LuaPedal* s = self(L);
            int a = firstArg(L);
            for (int i = 0; i < 3; ++i)
                s->color[i] = std::clamp((float)luaL_checknumber(L, a + 1 + i), 0.0f, 255.0f) / 255.0f;
            s->hasColor = true;
            return 0;
        }

        static int l_linkbar(lua_State* L) {
            LuaPedal* s = self(L);
            s->bar = luaL_checkstring(L, firstArg(L) + 1);
            return 0;
        }

        static TextureId textureArg(lua_State* L, LuaPedal* s, int index) {
            if (lua_type(L, index) == LUA_TSTRING)
                return s->loadImage(lua_tostring(L, index));
            if (lua_isnumber(L, index))
                return (TextureId)lua_tointeger(L, index);
            return 0;
        }

        static int l_image(lua_State* L) {
            LuaPedal* s = self(L);
            const char* name = luaL_checkstring(L, 1);
            lua_pushinteger(L, (lua_Integer)s->loadImage(name));
            return 1;
        }

        static int l_basetexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.base = textureArg(L, s, firstArg(L) + 1);
            return 0;
        }
        static int l_knobtexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.knob = textureArg(L, s, firstArg(L) + 1);
            return 0;
        }
        static int l_knobshadowtexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.knobShadow = textureArg(L, s, firstArg(L) + 1);
            return 0;
        }
        static int l_ledtexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.led = textureArg(L, s, firstArg(L) + 1);
            return 0;
        }
        static int l_footswitchtexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.footswitch = textureArg(L, s, firstArg(L) + 1);
            return 0;
        }
        static int l_labeltexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.label = textureArg(L, s, firstArg(L) + 1);
            return 0;
        }
        static int l_overlaytexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.overlay = textureArg(L, s, firstArg(L) + 1);
            return 0;
        }

        static int l_tanh(lua_State* L) { lua_pushnumber(L, std::tanh(luaL_checknumber(L, 1))); return 1; }
        static int l_clamp(lua_State* L) {
            lua_pushnumber(L, std::clamp(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3)));
            return 1;
        }
        static int l_db2lin(lua_State* L) { lua_pushnumber(L, std::pow(10.0, luaL_checknumber(L, 1) * 0.05)); return 1; }
    };

    // Carrega todos os plugins e amps de uma vez. O probe roda sem cache de
    // texturas para nao fazer upload de imagens so para descobrir o nome.
    inline int loadLuaPlugins(PluginManager& pm, AssetLibrary& lib, AmpLibrary& amps,
                              TextureCache* cache = nullptr) {
        lib.clearPlugins();
        amps.clearPlugins();
        int ok = 0;

        for (auto& info : pm.all()) {
            if (info.script.empty()) {
                if (info.status.empty()) info.status = "No script file.";
                continue;
            }

            std::unique_ptr<LuaPedal> probe;
            if (info.package) {
                probe = std::make_unique<LuaPedal>(info.package, info.rootPrefix, info.script, nullptr);
            } else if (!info.scriptPath.empty()) {
                probe = std::make_unique<LuaPedal>(info.scriptPath, nullptr);
            } else {
                info.status = "Script path is missing.";
                continue;
            }

            if (!probe->error.empty()) {
                info.status = probe->error;
                continue;
            }

            info.isAmp = probe->isAmp;
            info.status = "ok";
            const std::string displayName = probe->name;
            const std::string category = !probe->bar.empty()
                ? probe->bar
                : (info.fromPack && !info.packName.empty() ? info.packName : std::string("Plugins"));

            const auto pkg = info.package;
            const std::string root = info.rootPrefix;
            const std::string script = info.script;
            const std::filesystem::path path = info.scriptPath;
            const std::string source = info.sourceLabel();
            const std::string file = info.file;

            if (probe->isAmp) {
                amps.add(displayName, [pkg, root, script, path, cache]() -> std::unique_ptr<Pedal> {
                    if (pkg) return std::make_unique<LuaPedal>(pkg, root, script, cache);
                    return std::make_unique<LuaPedal>(path, cache);
                }, true, source, file);
            } else {
                lib.add(displayName, category, [pkg, root, script, path, cache]() -> std::unique_ptr<Pedal> {
                    if (pkg) return std::make_unique<LuaPedal>(pkg, root, script, cache);
                    return std::make_unique<LuaPedal>(path, cache);
                }, true, source, file);
            }
            ++ok;
        }
        return ok;
    }

}  // namespace gp
