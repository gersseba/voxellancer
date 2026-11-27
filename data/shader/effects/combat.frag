#version 330

in vec4 f_color;
in vec3 f_normal;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 normalz;
layout(location = 2) out vec4 emissiveness;

void main() {
    float shade = 0.7 + 0.3 * abs(dot(normalize(f_normal),normalize(vec3(1,2,3))));
    fragColor = vec4(f_color.rgb * shade,f_color.a);
    normalz = vec4(0);
    emissiveness = vec4(f_color.rgb * 0.3,f_color.a);
}
