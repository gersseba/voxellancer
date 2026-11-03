#include <bandit/bandit.h>

#include <memory>

#include <glm/glm.hpp>

#include "../bandit_extension/vec3helper.h"
#include "scripting/script.h"


using namespace bandit;


go_bandit([](){
 
    describe("Scripting vec3 lib", []() {

        std::unique_ptr<Script> script;

        before_each([&]() {
            script.reset(new Script());
        });

        it("can set debug status", [&]() {
            AssertThat(script->debugStatus(), Equals(""));
            script->loadString(R"( 
                setDebugStatus("success") 
            )");
            AssertThat(script->debugStatus(), Equals("success"));
        });

        it("can create vec3", [&]() {
            AssertThat(script->debugStatus(), Equals(""));
            script->loadString(R"( 
                v = vec3(1,2,3)
                setDebugStatus(vstring(v)) 
            )");
            AssertThat(script->debugStatus(), Equals("vec3(1, 2, 3)"));
        });

        it("can add vec3", [&]() {
            AssertThat(script->debugStatus(), Equals(""));
            script->loadString(R"( 
                x = vadd(vec3(10,20,30), vec3(1,2,3))
                setDebugStatus(vstring(x))
            )");
            AssertThat(script->debugStatus(), Equals("vec3(11, 22, 33)"));
        });

        it("can multiply vec3", [&]() {
            AssertThat(script->debugStatus(), Equals(""));
            script->loadString(R"( 
                x = vmul(vec3(1,2,3), 3.5)
                if x.x == 3.5 and x.y == 7 and x.z == 10.5 then
                    setDebugStatus("success")
                else
                    setDebugStatus("failed")
                end
            )");
            AssertThat(script->debugStatus(), Equals("success"));
        });

        it("can normalize vec3 ", [&]() {
            AssertThat(script->debugStatus(), Equals(""));
            script->loadString(R"( 
                x = vec3(1,4,9)
                l = vlength(vnormalize(v))
                setDebugStatus(l)
            )");
            
            AssertThat(atof(script->debugStatus().c_str()), EqualsWithDelta(1.0f, 0.001f));
        });

        it("can calculate vec3 lenght", [&]() {
            AssertThat(script->debugStatus(), Equals(""));
            script->loadString(R"( 
                x = vec3(1,4,9)
                l = vlength(v)
                setDebugStatus(l)
            )");
            AssertThat(atof(script->debugStatus().c_str()), EqualsWithDelta(glm::length(glm::vec3(1,4,9)), 0.001f));
        });
    });
});
