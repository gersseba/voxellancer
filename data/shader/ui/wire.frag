#version 330
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normalz;
layout(location = 2) out vec4 emissiveness;
uniform vec4 lineColor;
void main() {
    color = lineColor;
    normalz = vec4(0.5, 0.5, 1.0, gl_FragCoord.z);
    emissiveness = vec4(0.0, 0.0, 0.0, 1.0);
}
