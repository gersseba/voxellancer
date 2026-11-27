#include "effectlab.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

#include "settings/settingsstore.h"
#include "utils/filesystem.h"
#include "voxeleffect/combateffectrenderer.h"
#include "voxeleffect/effectsequence.h"

namespace {
    std::unique_ptr<EffectSequence> sequence;
    std::unique_ptr<CombatEffectRenderer> renderer;
    int currentFrame = -1;
    bool paused = false;

    EM_JS(char*, queryOption, (const char* key), {
        return stringToNewUTF8(new URLSearchParams(location.search).get(UTF8ToString(key)) || "");
    });

    std::string option(const char* key, const std::string& fallback) {
        char* text = queryOption(key);
        const std::string value(text);
        std::free(text);
        return value.empty() ? fallback : value;
    }

    void failure(const std::exception& error) {
        const std::string message = std::string("Effect lab failed: ")+error.what();
        EM_ASM({
            Module.effectLabError = UTF8ToString($0);
            console.error(Module.effectLabError);
            Module.showNotice(Module.effectLabError);
        },message.c_str());
    }

    int draw(int index) {
        int width, height;
        glfwGetFramebufferSize(glfwGetCurrentContext(),&width,&height);
        if (width <= 0 || height <= 0) throw std::runtime_error("Effect lab viewport is empty.");
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(0,0,width,height);
        glClearColor(0.025f,0.045f,0.075f,1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const auto projection = glm::perspective(glm::radians(35.0f),
            static_cast<float>(width)/height,0.1f,100.0f);
        const auto view = glm::lookAt(EffectSequence::viewer(),glm::vec3(0),glm::vec3(0,1,0));
        const auto& boxes = sequence->frame(index);
        renderer->draw(boxes,projection*view);
        glfwSwapBuffers(glfwGetCurrentContext());
        return static_cast<int>(boxes.size());
    }
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int effectLabDraw(int index) {
    try { return draw(index); }
    catch (const std::exception& error) { failure(error); return -1; }
}

EMSCRIPTEN_KEEPALIVE int effectLabSet(int group, int field, float value) {
    try {
        sequence->settings().set(group,field,value);
        return 1;
    } catch (const std::exception& error) { failure(error); return 0; }
}

EMSCRIPTEN_KEEPALIVE float effectLabGet(int group, int field) {
    return sequence->settings().value(group,field);
}

EMSCRIPTEN_KEEPALIVE int effectLabLoadSettings() {
    try {
        sequence->settings().restore(SettingsStore("/effect-lab.ini"));
        return 1;
    } catch (const std::exception& error) { failure(error); return 0; }
}
}

bool startEffectLab() {
    if (option("scenario","") != "effect-lab") return false;
    const auto group = option("group","pulselaser");
    const auto kind = option("effect","muzzle");
    const auto rate = option("fps","60");
    std::size_t consumed = 0;
    const int fps = std::stoi(rate,&consumed);
    if (consumed != rate.size()) throw std::invalid_argument("Invalid effect lab FPS.");
    sequence = std::make_unique<EffectSequence>(group,kind,fps);
    if (option("saved","0") == "1")
        sequence->settings().restore(SettingsStore(FileSystem::userConfigDir()+"/settings.ini"));
    renderer = std::make_unique<CombatEffectRenderer>();
    EM_ASM({
        Module.canvas.style.width = "100vw";
        Module.canvas.style.height = "100vh";
        Module.canvas.parentElement.style.minHeight = "0";
    });
    double width, height;
    emscripten_get_element_css_size("#canvas",&width,&height);
    glfwSetWindowSize(glfwGetCurrentContext(),static_cast<int>(width),static_cast<int>(height));
    std::ostringstream json;
    json << "{\"fps\":" << fps << ",\"group\":" << std::quoted(group) <<
        ",\"effect\":" << std::quoted(kind) <<
        ",\"camera\":{\"eye\":[10,8,18],\"target\":[0,0,0],\"up\":[0,1,0],"
        "\"verticalFovDegrees\":35,\"near\":0.1,\"far\":100},\"groups\":[";
    auto& settings = sequence->settings();
    for (std::size_t g = 0; g < CombatEffectSettings::GroupCount; ++g) {
        if (g) json << ',';
        json << "{\"key\":" << std::quoted(CombatEffectSettings::key(g)) << ",\"fields\":[";
        for (std::size_t f = 0; f < settings.fields(g).size(); ++f) {
            if (f) json << ',';
            const auto& field = settings.fields(g)[f];
            json << "{\"key\":" << std::quoted(field.property) << ",\"label\":" <<
                std::quoted(field.label) << ",\"minimum\":" << SettingsStore::number(field.minimum) <<
                ",\"maximum\":" << SettingsStore::number(field.maximum) << ",\"integer\":" <<
                (field.integer ? "true" : "false") << ",\"value\":" <<
                SettingsStore::number(settings.value(g,f)) << '}';
        }
        json << "]}";
    }
    json << "]}";
    EM_ASM(({
        const info = JSON.parse(UTF8ToString($0));
        const check = result => {
            if (Module.effectLabError) throw Error(Module.effectLabError);
            return result;
        };
        Module.effectLab = {
            describe: () => {
                info.groups.forEach((group,g) => group.fields.forEach((field,f) => {
                    field.value = _effectLabGet(g,f);
                }));
                return structuredClone(info);
            },
            frame: index => new Promise((resolve,reject) => requestAnimationFrame(() => {
                try {
                    if (!Number.isInteger(index)) throw Error("Effect lab frame must be an integer.");
                    const boxes = check(_effectLabDraw(index));
                    const gl = Module.ctx, width = gl.drawingBufferWidth, height = gl.drawingBufferHeight;
                    const pixels = new Uint8Array(width*height*4);
                    gl.readPixels(0,0,width,height,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
                    let visiblePixels = 0, peak = 0, energy = 0;
                    const bounds = [width,height,-1,-1];
                    for (let p = 0; p < pixels.length; p += 4) {
                        const difference = Math.abs(pixels[p]-pixels[0]) +
                            Math.abs(pixels[p+1]-pixels[1]) + Math.abs(pixels[p+2]-pixels[2]);
                        if (!difference) continue;
                        const x = (p/4)%width, y = height-1-Math.floor(p/4/width);
                        ++visiblePixels; peak = Math.max(peak,difference); energy += difference;
                        bounds[0] = Math.min(bounds[0],x); bounds[1] = Math.min(bounds[1],y);
                        bounds[2] = Math.max(bounds[2],x); bounds[3] = Math.max(bounds[3],y);
                    }
                    resolve({boxes,visiblePixels,peak,energy,bounds:visiblePixels ? bounds : null});
                } catch (error) { reject(error); }
            })),
            set: (group,key,value) => {
                const g = info.groups.findIndex(entry => entry.key === group);
                const f = g < 0 ? -1 : info.groups[g].fields.findIndex(entry => entry.key === key);
                if (f < 0 || !Number.isFinite(value)) throw Error("Unknown or invalid effect setting: "+group+"."+key);
                check(_effectLabSet(g,f,value));
                info.groups[g].fields[f].value = value;
            },
            loadSettings: text => {
                FS.writeFile("/effect-lab.ini",text);
                check(_effectLabLoadSettings());
            }
        };
        Module.setStatus("");
        Module.canvas.focus();
        if (new URLSearchParams(location.search).get("capture") !== "1")
            Module.showNotice("EFFECT LAB / "+info.group+" / "+info.effect+
                " / Space pause / Right step / R restart");
    }),json.str().c_str());
    const bool capture = option("capture","0") == "1";
    if (capture) {
        draw(0);
    } else {
        glfwSetKeyCallback(glfwGetCurrentContext(),[](GLFWwindow*,int key,int,int action,int) {
            if (action != GLFW_PRESS) return;
            if (key == GLFW_KEY_SPACE) paused = !paused;
            if (key == GLFW_KEY_R) { currentFrame = 0; effectLabDraw(currentFrame); }
            if (key == GLFW_KEY_RIGHT) { paused = true; effectLabDraw(++currentFrame); }
        });
        emscripten_set_main_loop([]() {
            if (!paused) {
                const int seconds = sequence->kind() == EffectSequence::Kind::ShieldIdle ? 8 : 2;
                if (++currentFrame >= sequence->fps()*seconds) currentFrame = 0;
                if (effectLabDraw(currentFrame) < 0) emscripten_cancel_main_loop();
            }
            glfwPollEvents();
        },fps,false);
    }
    return true;
}
