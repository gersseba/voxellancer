#version 330

uniform mat4 viewProjection;
uniform mat4 oldViewProjection;
uniform float sizeFactor;

layout(location=0) in vec3 v_vertex;
layout(location=1) in float v_brightness;
layout(location=2) in float v_size;

out vec2 f_uv;
out float f_brightness;

void main() {
    vec4 pos = viewProjection * vec4(v_vertex, 1.0);
    vec4 oldPos = oldViewProjection * vec4(v_vertex, 1.0);
    float size = v_size * sizeFactor;
    vec4 dir = pos - oldPos + vec4(pos.xy, 0, 0) / 100.0;
    dir = normalize(dir) * size;
    vec4 orthogonal = vec4(cross(normalize(vec3(dir.xy, 0)), vec3(0, 0, 1)), 0) * size;

    int stripVertex[6] = int[6](0, 1, 2, 2, 1, 3);
    int section = gl_VertexID / 6;
    int vertex = stripVertex[gl_VertexID % 6];
    bool right = vertex >= 2;
    bool outer = (vertex % 2) == 1;
    float u = right ? 1.0 : 0.0;
    vec4 side = right ? orthogonal : -orthogonal;

    if (section == 0) {
        gl_Position = pos + side + (outer ? dir : vec4(0));
        f_uv = vec2(u, outer ? 1.0 : 0.5);
    } else if (section == 1) {
        gl_Position = (outer ? oldPos : pos) + side;
        f_uv = vec2(u, 0.5);
    } else {
        gl_Position = oldPos + side - (outer ? dir : vec4(0));
        f_uv = vec2(u, outer ? 0.0 : 0.5);
    }
    f_brightness = v_brightness;
}

