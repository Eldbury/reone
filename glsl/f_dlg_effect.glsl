#include "u_screeneffect.glsl"

uniform sampler2D sMainTex;
noperspective in vec2 fragUV1;
out vec4 fragColor;

void main() {
    vec2 uv = fragUV1;
    float time = uVideoParams.w;
    // Clairvoyance is an intentionally bounded approximation of vanilla's
    // framebuffer distortion. It is local presentation, never a dialogue clock.
    if (uVideoParams.y > 0.0) {
        uv.x += 0.0025 * sin(uv.y * 24.0 + time * 1.8);
        uv.y += 0.0015 * sin(uv.x * 19.0 - time * 1.3);
    }
    vec3 color = texture(sMainTex, clamp(uv, 0.0, 1.0)).rgb;
    float gray = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(gray), color, uVideoColor.a) * uVideoColor.rgb;
    if (uVideoParams.x > 0.0) {
        float noise = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233)) + floor(time * 30.0)) * 43758.5453);
        float scan = mod(floor(gl_FragCoord.y), 3.0) == 0.0 ? 0.82 : 1.0;
        color = color * scan + (noise - 0.5) * 0.055;
    }
    if (uVideoParams.y > 0.0) {
        vec2 delta = (uv - vec2(0.5)) * 0.012;
        vec3 bloom = (texture(sMainTex, clamp(uv + delta, 0.0, 1.0)).rgb +
                      texture(sMainTex, clamp(uv - delta, 0.0, 1.0)).rgb) * 0.5;
        color = mix(color, bloom, 0.3);
        if (uVideoParams.z == 0.0 && (uv.y < 0.09 || uv.y > 0.91)) color = vec3(0.0);
    }
    // These post-process cues do not claim vanilla's alignment-aware actor
    // silhouettes (Force Sight) or exact platform-specific fury compositing.
    if (uVideoOther.x > 0.0) color = mix(color, vec3(gray) * vec3(0.65, 0.85, 1.2), 0.65);
    if (uVideoOther.y > 0.0) {
        float edge = smoothstep(0.18, 0.7, length(uv - vec2(0.5)));
        color = mix(color, color * vec3(1.35, 0.5, 0.45), edge * uVideoOther.y / 3.0);
    }
    color = mix(color, uDialogueFade.rgb, uDialogueFade.a);
    fragColor = vec4(color, 1.0);
}
