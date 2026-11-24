#version 330

uniform sampler2D background;
in vec2 imageUv;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normalz;
layout(location = 2) out vec4 emissiveness;

void main() {
    color = texture(background, imageUv);
    normalz = vec4(0.5, 0.5, 1.0, gl_FragCoord.z);
    emissiveness = vec4(0.0, 0.0, 0.0, 1.0);
}
