#version 330

uniform sampler2D sourceTexture;
uniform vec4 distortionKs;
uniform float distortionScale;
uniform vec2 lensCenter;

in vec2 f_viewportC;

layout(location = 0) out vec4 fragColor;


vec2 HmdWarp(vec2 linear) {
    float radiusSquared = linear.x * linear.x + linear.y * linear.y;
    vec2 rvector = linear / distortionScale * (
        distortionKs.x +
        distortionKs.y * radiusSquared +
        distortionKs.z * radiusSquared * radiusSquared +
        distortionKs.w * radiusSquared * radiusSquared * radiusSquared
    );
    return (rvector + 1.0) / 2.0;
}

void main() {
    vec2 lensC = f_viewportC - lensCenter;
    vec2 texC = HmdWarp(f_viewportC);

    if(texC.x < 0.0 || texC.x > 1.0) {
        discard;
    }
    if(texC.y < 0.0 || texC.y > 1.0) {
        discard;
    }

    fragColor = texture(sourceTexture, texC);
}

