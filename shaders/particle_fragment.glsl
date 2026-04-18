#version 330 core
in float vLife;
out vec4 FragColor;

uniform vec3 uColor;

void main() {
    // soft circular particle
    vec2 uv = gl_PointCoord.xy - 0.5;
    float d = length(uv);
    float mask = smoothstep(0.5, 0.45, d);
    float a = clamp(vLife, 0.0, 1.0) * mask;
    // bias alpha so outer layer appears denser and darker
    float alpha = a * 0.9;
    // slightly reduce overall brightness to avoid white hotspots
    vec3 c = uColor * 0.85;
    FragColor = vec4(c, alpha);
}
