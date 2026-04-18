#version 330 core
in vec3 vCol;
in vec3 vNormal;
in vec3 vWorldPos;
in float vHeight;
in vec2 vUV;
out vec4 FragColor;

uniform vec3 uTint;
uniform vec3 uCamPos;
uniform float uOpacity;
uniform int uObjType; // 0=default,1=house,2=tree,3=ground,4=tornado
uniform int uHasAlbedo;
uniform sampler2D uAlbedo;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    // directional light (stronger contrast)
    vec3 lightDir = normalize(vec3(-0.6, -1.0, -0.3));
    float diff = max(dot(N, -lightDir), 0.0);
    float spec = pow(max(dot(reflect(lightDir, N), V), 0.0), 24.0) * 0.28; // softer, less bright spec
    vec3 ambient = 0.12 * (vCol * (uTint * 0.95));
    vec3 diffuse = 1.0 * diff * (vCol * (uTint * 0.95));
    vec3 final = ambient + diffuse + spec;
    // if textured, modulate by texture albedo
    if (uHasAlbedo == 1) {
        vec3 al = texture(uAlbedo, vUV).rgb;
        final *= al;
    }

    // desaturate and darken toward base to simulate dust (tornado default)
    float heightFactor = smoothstep(0.0, 6.0, vHeight);
    float desat = mix(0.2, 0.9, heightFactor);
    vec3 gray = vec3(dot(final, vec3(0.299,0.587,0.114)));
    vec3 tornadoFinal = mix(gray, final, desat);

    // procedural variations per object type
    vec3 outCol = final;
    float outAlpha = 1.0;
    if (uObjType == 1) {
        // house: walls + roof + simple window/door shapes using local height and normal
        // roof: use vHeight > 1.02
        if (vHeight > 1.02) {
            outCol = vec3(0.45, 0.2, 0.16) * (vCol * uTint);
        } else {
            // walls: brick-like tint with simple window and door masks via vWorldPos xz
            vec2 m = vWorldPos.xz * 2.0; // scale pattern
            float bricks = step(0.0, sin(m.x*3.0)*sin(m.y*6.0));
            outCol = mix(vec3(0.9,0.78,0.6), vec3(0.78,0.6,0.45), bricks) * (vCol * uTint);
            // window region near front face (approx by normal facing + local height band)
            if (vNormal.z < -0.3 && vHeight > 0.5 && vHeight < 0.9) {
                outCol *= 0.35; // darker window glass
            }
            // door hint near center front
            if (vNormal.z < -0.3 && vHeight < 0.5 && abs(vWorldPos.x - 0.0) < 0.18) {
                outCol = mix(outCol, vec3(0.2,0.12,0.08), 0.9);
            }
        }
        outAlpha = clamp(1.0 * uOpacity, 0.9, 1.0);
    } else if (uObjType == 2) {
        // tree: trunk at low vHeight, foliage above
        if (vHeight < 0.35) {
            outCol = vec3(0.38,0.25,0.14) * (vCol * uTint);
        } else {
            // layered foliage: darker at bottom, lighter at top
            float f = smoothstep(0.35, 1.2, vHeight);
            outCol = mix(vec3(0.08,0.28,0.08), vec3(0.12,0.45,0.12), f) * (vCol * uTint);
        }
        outAlpha = clamp(1.0 * uOpacity, 0.85, 1.0);
    } else if (uObjType == 3) {
        // ground: keep as before but slightly more varied
        float gr = fract(vWorldPos.x * 0.1) * fract(vWorldPos.z * 0.1);
        outCol = mix(vec3(0.12,0.38,0.18), vec3(0.08,0.32,0.14), gr) * (vCol * uTint);
        outAlpha = clamp(1.0 * uOpacity, 0.95, 1.0);
    } else {
        // default: tornado path
        float fres = pow(1.0 - max(dot(N, V), 0.0), 1.6);
        float baseAlpha = 1.0 - smoothstep(0.0, 6.0, vHeight);
        baseAlpha *= (1.0 - 0.55 * heightFactor);
        outAlpha = clamp(baseAlpha * (0.6 + 0.3 * fres) * uOpacity, 0.02, 1.0);
        // stronger fog for distance blending into storm sky
        float fog = smoothstep(8.0, 28.0, length(uCamPos - vWorldPos));
        outCol = mix(tornadoFinal, vec3(0.06,0.06,0.08), fog);
    }

    FragColor = vec4(outCol, outAlpha);
}
