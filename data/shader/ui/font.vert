#version 330

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
uniform mat4 viewProjection;
out vec2 glyphUv;

void main() {
    gl_Position = viewProjection * vec4(position, 1.0);
    glyphUv = uv;
}
