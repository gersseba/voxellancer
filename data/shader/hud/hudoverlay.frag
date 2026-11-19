#version 330

uniform vec3 panelColor;
uniform float panelOpacity;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normalz;
layout(location = 2) out vec4 emissiveness;

void main() {
    color = vec4(panelColor, panelOpacity);
    normalz = vec4(0.5, 0.5, 1.0, gl_FragCoord.z);
    emissiveness = vec4(0.0, 0.0, 0.0, 1.0);
}
