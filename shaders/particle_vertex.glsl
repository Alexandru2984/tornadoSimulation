#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aLife;

uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;
uniform float uPointScale; // multiplier set per-pass

out float vLife;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    gl_Position = uProj * uView * world;
    vLife = aLife;
    // point size scaled per-pass and by remaining life (younger -> larger)
    gl_PointSize = uPointScale * 6.0 * (0.35 + vLife*0.65);
}
