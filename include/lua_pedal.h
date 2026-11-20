// lua_pedal.h - Pedais escritos em Lua, com suporte a texturas.
#pragma once
#include <atomic>
#include <lua.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "assets.h"
#include "plugins.h"
#include "pedals.h"
#include "textures.h"

namespace gp {

    // Forward declaration da AmpLibrary (para não incluir amps.h e gerar ciclo)
    class AmpLibrary;

    enum LuaWidget { kButtonSpin = 1, kSlider = 2 };

    class LuaPedal : public Pedal {
    public:
        // Antes era 16: impedia amps grandes. Agora o limite é apenas de
        // segurança da pilha Lua; o layout da UI é dinâmico.
        static constexpr int kMaxParams = 128;

        std::string bar;
        std::string error;
        std::vector<int> widgets;
        std::atomic<bool> broken{ false };
        bool isAmp = false;
        bool isTest = false;
        // Dimensões pedidas pelo script (amp.size(w,h)). 0 = automático.
        float uiW = 0.0f, uiH = 0.0f;
        // Distorção valvulada gera harmônicos acima de Nyquist. Com
        // oversample(true) o callback Lua roda a 2x e o sinal é filtrado
        // na ida e na volta, sem "chiado metálico" por alias.
        bool oversample = false;
        char rtErr[256] = { 0 };

        TextureSet tex;
        std::filesystem::path assetsDir;
        TextureCache* texCache = nullptr;

        explicit LuaPedal(const std::filesystem::path& script, TextureCache* cache = nullptr)
            : Pedal("Lua Pedal"), texCache(cache) {
            assetsDir = script.parent_path() / (script.stem().string() + "_assets");
            if (setup()) init(luaL_loadfile(L, script.string().c_str()));
        }

        LuaPedal(const std::string& source, const char* chunkName, TextureCache* cache = nullptr)
            : Pedal("Lua Pedal"), texCache(cache) {
            assetsDir = std::filesystem::path(".");
            if (setup()) init(luaL_loadbuffer(L, source.data(), source.size(), chunkName));
        }

        ~LuaPedal() override {
            if (texCache) {
                for (TextureId* p : { &tex.base, &tex.knob, &tex.knobShadow,
                                      &tex.led, &tex.footswitch, &tex.label, &tex.overlay })
                    if (*p) { texCache->release(*p); *p = 0; }
            }
            if (L) lua_close(L);
        }

        void prepare(float sr) override {
            Pedal::prepare(sr);
            if (!L || broken.load(std::memory_order_relaxed)) return;

            const float runSR = oversample ? sampleRate * 2.0f : sampleRate;
            lua_pushnumber(L, runSR); lua_setglobal(L, "SAMPLE_RATE");
            if (prepRef != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, prepRef);
                lua_pushnumber(L, runSR);
                if (lua_pcall(L, 1, 0, 0) != LUA_OK) setRuntimeError();
            }

            if (oversample) {
                const float fc = sampleRate * 0.45f;
                // Butterworth 4a (two biquads) na reconstrução e na decimação.
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

    private:
        lua_State* L = nullptr;
        int pedalRef = LUA_NOREF, procRef = LUA_NOREF, resetRef = LUA_NOREF, prepRef = LUA_NOREF;
        int imgTableRef = LUA_NOREF;
        // Tabela de amostras reaproveitada: criar tabela por bloco alocava
        // memória no thread de áudio e podia causar crackles.
        int bufRef = LUA_NOREF;
        bool blockMode = true;
        Biquad osUp1, osUp2, osDn1, osDn2;
        std::vector<float> osBuf;

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

        // Processa o bloco a 2x para que a saturação não aliases de volta
        // na faixa audível. O script vê SAMPLE_RATE = 2x e n = 2*n.
        void processOversampled(float* x, int n, int np) {
            if ((size_t)n * 2 > osBuf.size()) return;
            float* up = osBuf.data();
            for (int i = 0; i < n; ++i) {
                // O fator 2 compensa o zero-stuffing; não multiplicar de novo.
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
            // Buffer de amostras pré-alocado (tamanho máximo de um bloco 2x).
            lua_createtable(L, kMaxBlock * 2, 0);
            bufRef = luaL_ref(L, LUA_REGISTRYINDEX);
            procRef = fieldRef("process");
            resetRef = fieldRef("reset");
            prepRef = fieldRef("prepare");
            if (procRef == LUA_NOREF) error = "define pedal.process = function(...) ... end";
            // Coleta o lixo da carga uma vez, fora do callback de áudio.
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
            lua_pushlightuserdata(L, this); lua_pushcclosure(L, f, 1); lua_setglobal(L, name);
        }
        void method(const char* key, lua_CFunction f) {
            lua_pushlightuserdata(L, this); lua_pushcclosure(L, f, 1); lua_setfield(L, -2, key);
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
            const char* modname = luaL_checkstring(L, 1);
            std::filesystem::path dir = s->assetsDir.parent_path() / modname;
            std::string dirStr = dir.string();

            lua_newtable(L);
            // A pasta vai como upvalue string (memória do Lua): sem userdata
            // com std::string e sem vazamento por falta de __gc.
            lua_pushlightuserdata(L, s);
            lua_pushlstring(L, dirStr.data(), dirStr.size());
            lua_pushcclosure(L, l_imagepast, 2);
            lua_setfield(L, -2, "imagepast");
            return 1;
        }

        static int l_newamp(lua_State* L) {
            LuaPedal* s = self(L);
            if (s->pedalRef != LUA_NOREF) return luaL_error(L, "use np() ou newamp() apenas uma vez por script");
            s->isAmp = true;
            s->name = luaL_optstring(L, 1, "New Amp");
            lua_newtable(L);
            s->method("instance", l_instance);
            s->method("color", l_color);
            s->method("linkbar", l_linkbar);
            s->method("basetexture", l_basetexture);
            s->method("knobtexture", l_knobtexture);
            s->method("ledtexture", l_ledtexture);
            s->method("footswitchtexture", l_footswitchtexture);
            s->method("oversample", l_oversample);
            s->method("size", l_size);
            s->method("labels", l_labels);
            lua_pushvalue(L, -1);
            s->pedalRef = luaL_ref(L, LUA_REGISTRYINDEX);
            return 1;
        }

        static int l_imagepast(lua_State* L) {
            LuaPedal* s = self(L);
            lua_newtable(L);
            lua_pushlightuserdata(L, s);
            lua_pushcclosure(L, l_loadimage, 1);
            lua_setfield(L, -2, "loadimage");
            return 1;
        }

        static int l_loadimage(lua_State* L) {
            LuaPedal* s = self(L);
            if (!s->texCache) { lua_pushinteger(L, 0); return 1; }
            const char* name = luaL_checkstring(L, 1);
            std::filesystem::path p = s->assetsDir / name;
            TextureId id = s->texCache->load(p);
            lua_pushinteger(L, (lua_Integer)id);
            return 1;
        }

        static int l_newassetsbar(lua_State* L) {
            luaL_checkstring(L, 1);
            lua_settop(L, 1);
            return 1;
        }

        static int l_np(lua_State* L) {
            LuaPedal* s = self(L);
            if (s->pedalRef != LUA_NOREF) return luaL_error(L, "use np() apenas uma vez por script");
            s->name = luaL_optstring(L, 1, "Lua Pedal");
            lua_newtable(L);
            s->method("instance", l_instance);
            s->method("color", l_color);
            s->method("linkbar", l_linkbar);
            s->method("basetexture", l_basetexture);
            s->method("knobtexture", l_knobtexture);
            s->method("ledtexture", l_ledtexture);
            s->method("footswitchtexture", l_footswitchtexture);
            s->method("texture", l_basetexture);
            s->method("oversample", l_oversample);
            s->method("size", l_size);
            s->method("labels", l_labels);
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

        // labels("modo", {"CLEAN","CRUNCH","OD1","OD2"}): nomes das posições
        // de um BUTTONSPIN. O botão passa a mostrar o nome e ciclar.
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

        // oversample(true): processa a 2x internamente (anti-aliasing).
        static int l_oversample(lua_State* L) {
            LuaPedal* s = self(L);
            s->oversample = lua_toboolean(L, firstArg(L) + 1) != 0;
            return 0;
        }

        // size(largura, altura): dimensões do card do pedal/amp na UI.
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

        static int l_basetexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.base = (TextureId)luaL_checkinteger(L, firstArg(L) + 1);
            return 0;
        }
        static int l_knobtexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.knob = (TextureId)luaL_checkinteger(L, firstArg(L) + 1);
            return 0;
        }
        static int l_ledtexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.led = (TextureId)luaL_checkinteger(L, firstArg(L) + 1);
            return 0;
        }
        static int l_footswitchtexture(lua_State* L) {
            LuaPedal* s = self(L);
            s->tex.footswitch = (TextureId)luaL_checkinteger(L, firstArg(L) + 1);
            return 0;
        }

        static int l_tanh(lua_State* L) { lua_pushnumber(L, std::tanh(luaL_checknumber(L, 1))); return 1; }
        static int l_clamp(lua_State* L) {
            lua_pushnumber(L, std::clamp(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3)));
            return 1;
        }
        static int l_db2lin(lua_State* L) { lua_pushnumber(L, std::pow(10.0, luaL_checknumber(L, 1) * 0.05)); return 1; }
    };

    inline int loadLuaPlugins(const PluginManager& pm, AssetLibrary& lib, TextureCache* cache = nullptr) {
        lib.clearPlugins();
        int ok = 0;
        for (const auto& info : pm.all()) {
            if (info.script.empty()) { info.status.clear(); continue; }
            auto path = pm.dir() / info.script;
            LuaPedal probe(path, cache);
            if (!probe.error.empty()) { info.status = probe.error; continue; }
            // Se for um amp, não adiciona na biblioteca de pedais
            if (probe.isAmp) continue;
            info.status = "ok";
            lib.add(probe.name, probe.bar.empty() ? "Plugins" : probe.bar,
                [path, cache] { return std::make_unique<LuaPedal>(path, cache); }, true);
            ++ok;
        }
        return ok;
    }

}  // namespace gp