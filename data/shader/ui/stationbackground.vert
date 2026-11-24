#version 330

layout(location = 0) in vec2 position;
uniform vec4 panelBounds;
out vec2 imageUv;

void main() {
    gl_Position = vec4(panelBounds.xy + (position * 0.5 + 0.5) * panelBounds.zw, 0.0, 1.0);
    imageUv = vec2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
}
