#version 330

in vec2 glyphUv;
uniform sampler2D atlas;
uniform vec4 textColor;
uniform bool colored;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normalz;
layout(location = 2) out vec4 emissiveness;

void main() {
    float alpha = texture(atlas, glyphUv).a * textColor.a;
    if (alpha < 0.01) discard;
    color = vec4(textColor.rgb * (colored ? texture(atlas, glyphUv).rgb : vec3(1.0)), alpha);
    normalz = vec4(0.5, 0.5, 1.0, gl_FragCoord.z);
    emissiveness = vec4(0.0, 0.0, 0.0, alpha);
}
