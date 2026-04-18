#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aCol;
layout(location = 3) in vec2 aUV;

out vec3 vCol;
out vec3 vNormal;
out vec3 vWorldPos;
out float vHeight;
out vec2 vUV;

uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;
uniform float uTime;
uniform float uEnableSwirl;

void main() {
    float height = aPos.y;
    vec3 p = aPos;
    if (uEnableSwirl > 0.5) {
        // swirling offset depends on height and time, stronger near middle
        float swirlAmt = 0.25 * (1.0 - height / 6.0);
        float angOff = uTime * 2.0 + height * 3.0;
        p.x = aPos.x * cos(angOff) - aPos.z * sin(angOff);
        p.z = aPos.x * sin(angOff) + aPos.z * cos(angOff);
        // small radial pulsation
        float pulse = 1.0 + 0.05 * sin(uTime * 6.0 + height * 8.0);
        p.x *= pulse;
        p.z *= pulse;
    }

    vec4 worldPos = uModel * vec4(p, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
    vCol = aCol;
    vUV = aUV;
    vHeight = height;
    gl_Position = uProj * uView * worldPos;
}
