#include "combateffectrenderer.h"

#include <GL/glew.h>
#include <glow/Buffer.h>
#include <glow/Program.h>
#include <glow/VertexArrayObject.h>
#include <glow/VertexAttributeBinding.h>

#include "display/rendering/shaderloader.h"
#include "voxelmesh.h"

CombatEffectRenderer::CombatEffectRenderer(): m_mesh(new VoxelMesh()) {}
CombatEffectRenderer::~CombatEffectRenderer() = default;

void CombatEffectRenderer::initialize() {
    m_program = new glow::Program();
    m_program->attach(
        createShaderFromFiles(GL_VERTEX_SHADER,{"data/shader/effects/combat.vert",
            "data/shader/lib/quaternion.glsl"}),
        createShaderFromFile(GL_FRAGMENT_SHADER,"data/shader/effects/combat.frag"));
    m_array = new glow::VertexArrayObject();
    m_buffer = new glow::Buffer(GL_ARRAY_BUFFER);
    m_mesh->bindTo(m_program,m_array,0);
    const auto attribute = [&](const char* name, int count, std::size_t offset, int bindingIndex) {
        const auto location = m_program->getAttributeLocation(name);
        auto* binding = m_array->binding(bindingIndex);
        binding->setAttribute(location);
        binding->setBuffer(m_buffer,0,sizeof(CombatEffectBox));
        binding->setFormat(count,GL_FLOAT,GL_FALSE,static_cast<GLint>(offset));
        m_array->enable(location);
    };
    attribute("position",3,offsetof(CombatEffectBox,position),2);
    attribute("extent",3,offsetof(CombatEffectBox,extent),3);
    attribute("orientation",4,offsetof(CombatEffectBox,orientation),4);
    attribute("color",4,offsetof(CombatEffectBox,color),5);
}

void CombatEffectRenderer::draw(const std::vector<CombatEffectBox>& boxes,
    const glm::mat4& viewProjection) {
    if (boxes.empty()) return;
    GLint program, array, buffer, sourceRgb, destinationRgb, sourceAlpha, destinationAlpha;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&array);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
    glGetIntegerv(GL_BLEND_SRC_RGB,&sourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB,&destinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&sourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA,&destinationAlpha);
    GLboolean depthMask;
    glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    const bool depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND),
        cull = glIsEnabled(GL_CULL_FACE);
    if (!m_program) initialize();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    m_program->use();
    m_program->setUniform("viewProjection",viewProjection);
    m_buffer->setData(boxes.size()*sizeof(CombatEffectBox),boxes.data(),GL_STREAM_DRAW);
    m_array->bind();
    glVertexAttribDivisor(m_program->getAttributeLocation("v_vertex"),0);
    glVertexAttribDivisor(m_program->getAttributeLocation("v_normal"),0);
    for (const char* name : {"position","extent","orientation","color"})
        glVertexAttribDivisor(m_program->getAttributeLocation(name),1);
    m_array->drawArraysInstanced(GL_TRIANGLE_STRIP,0,14,static_cast<GLsizei>(boxes.size()));
    glDepthMask(depthMask);
    if (!depth) glDisable(GL_DEPTH_TEST);
    if (!blend) glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
    glBlendFuncSeparate(sourceRgb,destinationRgb,sourceAlpha,destinationAlpha);
    glUseProgram(program);
    glBindVertexArray(array);
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
}
void CombatEffectRenderer::beforeContextDestroy() {
    m_array = nullptr; m_buffer = nullptr; m_program = nullptr;
}
