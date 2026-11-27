#version 330

uniform sampler2D sourceTexture;

in vec2 f_texCoord;

out vec4 fragColor;


void main() {
    fragColor = texture(sourceTexture, f_texCoord);
}

