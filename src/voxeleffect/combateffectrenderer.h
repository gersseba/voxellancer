#pragma once

#include <memory>
#include <vector>
#include <glow/ref_ptr.h>
#include "etc/contextdependant.h"
#include "combateffects.h"

namespace glow { class Buffer; class Program; class VertexArrayObject; }
class VoxelMesh;

class CombatEffectRenderer final : public ContextDependant {
public:
    CombatEffectRenderer();
    ~CombatEffectRenderer();
    void draw(const std::vector<CombatEffectBox>& boxes, const glm::mat4& viewProjection);

private:
    std::unique_ptr<VoxelMesh> m_mesh;
    glow::ref_ptr<glow::Program> m_program;
    glow::ref_ptr<glow::Buffer> m_buffer;
    glow::ref_ptr<glow::VertexArrayObject> m_array;
    void initialize();
    void beforeContextDestroy() override;
    void afterContextRebuild() override {}
};
