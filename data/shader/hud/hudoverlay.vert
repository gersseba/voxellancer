#version 330

layout(location = 0) in vec2 position;
uniform vec4 panelBounds;

void main() {
    vec2 panelPosition = panelBounds.xy +
        (position * 0.5 + 0.5) * panelBounds.zw;
    gl_Position = vec4(panelPosition, 0.0, 1.0);
}
