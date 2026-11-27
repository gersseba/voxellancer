#version 330

in vec3 v_vertex;
in vec3 v_normal;
in vec3 position;
in vec3 extent;
in vec4 orientation;
in vec4 color;
uniform mat4 viewProjection;
out vec4 f_color;
out vec3 f_normal;

vec3 qtransform(vec4 quaternion, vec3 vector);

void main() {
    vec3 point = position + qtransform(orientation,v_vertex * extent);
    gl_Position = viewProjection * vec4(point,1);
    f_color = color;
    f_normal = qtransform(orientation,v_normal);
}
