#include "framebuffer.h"

#include <assert.h>
#include <stdexcept>

#include <glow/global.h>
#include <glow/FrameBufferObject.h>
#include <glow/Texture.h>
#include <glow/RenderBufferObject.h>
#include <glow/FrameBufferAttachment.h>
#include <glow/TextureAttachment.h>


FrameBuffer::FrameBuffer(int colorAttachments, bool depthAttachment):
    m_fbo(new glow::FrameBufferObject()),
    m_colorAttachmentCount(colorAttachments),
    m_useDepthAttachment(depthAttachment)
{
    assert(colorAttachments >= 0);
#ifdef __EMSCRIPTEN__
    if (glow::getInteger(GL_MAX_DRAW_BUFFERS) < 3) {
        throw std::runtime_error("WebGL 2 requires at least three draw buffers for this renderer.");
    }
#else
    assert(colorAttachments <= glow::getInteger(GL_MAX_COLOR_ATTACHMENTS));
#endif
}

void FrameBuffer::setResolution(const glm::ivec2& resolution) {
    if (resolution != m_resolution) {
        m_resolution = resolution;
        setupFBO();
    }
}

void FrameBuffer::bind() {
    m_fbo->bind();
}

void FrameBuffer::unbind() {
    m_fbo->unbind();
}

void FrameBuffer::setupFBO() {
#ifdef __EMSCRIPTEN__
    m_fbo->bind();
    for (std::size_t i = 0; i < m_activeAttachments; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
            GL_TEXTURE_2D, 0, 0);
    }
    m_textures.clear();
    m_activeAttachments = 0;
#endif
    for (int i = 0; i < m_colorAttachmentCount; i++) {
        glow::Texture* texture = new glow::Texture(GL_TEXTURE_2D);
        texture->bind();

#ifdef __EMSCRIPTEN__
        texture->image2D(0, GL_RGBA8, m_resolution.x, m_resolution.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
        texture->image2D(0, GL_RGB, m_resolution.x, m_resolution.y, 0, GL_RGB, GL_FLOAT, nullptr);
#endif

        texture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        texture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);

#ifdef __EMSCRIPTEN__
        m_textures.emplace_back(texture);
#else
        m_fbo->attachTexture2D(GL_COLOR_ATTACHMENT0 + i, texture);
#endif
    }

    if (m_useDepthAttachment) {
        glow::RenderBufferObject* rbo = new glow::RenderBufferObject();
#ifdef __EMSCRIPTEN__
        rbo->storage(GL_DEPTH_COMPONENT24, m_resolution.x, m_resolution.y);
#else
        rbo->storage(GL_DEPTH_COMPONENT, m_resolution.x, m_resolution.y);
#endif
        m_fbo->attachRenderBuffer(GL_DEPTH_ATTACHMENT, rbo);
    }
    m_fbo->unbind();
}

glow::FrameBufferObject& FrameBuffer::get() {
    return *m_fbo;
}

void FrameBuffer::clear() {
#ifdef __EMSCRIPTEN__
    if (m_colorAttachmentCount == 0) {
        setDrawBuffers({});
        m_fbo->clear(GL_DEPTH_BUFFER_BIT);
    }
    for (int i = 0; i < m_colorAttachmentCount; ++i) {
        setDrawBuffers({i});
        m_fbo->clear(GL_COLOR_BUFFER_BIT | (i == 0 ? GL_DEPTH_BUFFER_BIT : 0));
    }
#else
    m_fbo->clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif
}

void FrameBuffer::setDrawBuffers(const std::vector<int>& buffers) {
    std::vector<GLenum> attachments;
    for (int value : buffers) {
        assert(value >= 0 && value < m_colorAttachmentCount);
#ifdef __EMSCRIPTEN__
        attachments.push_back(GL_COLOR_ATTACHMENT0 + attachments.size());
#else
        attachments.push_back(GL_COLOR_ATTACHMENT0 + static_cast<int>(value));
#endif
    }
#ifdef __EMSCRIPTEN__
    m_fbo->bind();
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
            GL_TEXTURE_2D, m_textures[buffers[i]]->id(), 0);
    }
    for (std::size_t i = buffers.size(); i < m_activeAttachments; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
            GL_TEXTURE_2D, 0, 0);
    }
    m_activeAttachments = buffers.size();
#endif
    m_fbo->setDrawBuffers(attachments);
}

glow::Texture* FrameBuffer::texture(int i) {
    assert(i < m_colorAttachmentCount);
#ifdef __EMSCRIPTEN__
    return m_textures[i];
#else
    glow::FrameBufferAttachment* fba = m_fbo->attachment(GL_COLOR_ATTACHMENT0 + i);
    assert(fba->isTextureAttachment());
    glow::Texture* texture = dynamic_cast<glow::TextureAttachment*>(fba)->texture();
    return texture;
#endif
}

const glm::ivec2& FrameBuffer::resolution() {
    return m_resolution;
}

void FrameBuffer::beforeContextDestroy() {
    m_fbo = nullptr;
#ifdef __EMSCRIPTEN__
    m_textures.clear();
    m_activeAttachments = 0;
#endif
}

void FrameBuffer::afterContextRebuild() {
    m_fbo = new glow::FrameBufferObject();
    setupFBO();
}