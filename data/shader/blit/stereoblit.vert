#version 330

uniform vec2 viewportPosition;
uniform vec2 viewportSize;

in vec2 v_vertex;

out vec2 f_viewportC;


void main() {
    vec2 screenC = (v_vertex + 1.0) / 2.0 * viewportSize + viewportPosition;

    vec2 coord = (screenC * 2.0) - 1.0;

    gl_Position = vec4(coord, 0, 1);

    f_viewportC = v_vertex;
}

