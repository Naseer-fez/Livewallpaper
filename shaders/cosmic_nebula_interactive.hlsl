cbuffer ShaderUniforms : register(b0) {
    float i_time;
    float3 i_resolution; // (width, height, aspect_ratio)
    float4 i_mouse;      // (x_pixel, y_pixel, is_click_1_or_0, padding)
    float4 i_audio;      // (bass, mid, treble, peak)
    float i_depth;
    int i_frame;
};

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

// 2D Hash function for noise
float hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

// 2D Smooth Value Noise
float noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// Fractal Brownian Motion (FBM)
float fbm(float2 p) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 5; i++) {
        val += amp * noise(p * freq);
        freq *= 2.1;
        amp *= 0.45;
    }
    return val;
}

float4 main(VS_OUTPUT input) : SV_TARGET {
    // Normalized screen coordinates (-0.5 to 0.5 with corrected aspect ratio)
    float2 uv = input.UV - 0.5;
    uv.x *= i_resolution.z; // Multiply by aspect ratio

    // Normalized mouse position (-0.5 to 0.5 aspect corrected)
    float2 mouseUV = (i_mouse.xy / i_resolution.xy) - 0.5;
    mouseUV.x *= i_resolution.z;

    // 1. Gravitational Black Hole distortion toward mouse position
    float distToMouse = length(uv - mouseUV);
    float distortion = 0.04 / (distToMouse + 0.08);
    float2 warpedUV = uv + normalize(uv - mouseUV + 0.0001) * distortion;

    // 2. Cosmic Nebula FBM Layering
    float t = i_time * 0.15;
    float n1 = fbm(warpedUV * 2.5 + float2(t, t * 0.5));
    float n2 = fbm(warpedUV * 5.0 - float2(t * 0.8, -t * 0.3) + n1 * 1.5);
    float n3 = fbm(warpedUV * 10.0 + float2(n2, n1));

    // 3. Dynamic Color Palettes
    float3 colorDeep = float3(0.02, 0.04, 0.15);
    float3 colorCyan = float3(0.0, 0.6, 0.9);
    float3 colorPurple = float3(0.7, 0.1, 0.85);
    float3 colorGold = float3(1.0, 0.7, 0.2);

    float3 finalColor = colorDeep;
    finalColor = lerp(finalColor, colorCyan, n1);
    finalColor = lerp(finalColor, colorPurple, n2 * n1);
    finalColor += colorGold * pow(n3, 3.0) * 0.8;

    // 4. Interactive Supernova Shockwave on Mouse Click (i_mouse.z > 0.5)
    bool isClicked = i_mouse.z > 0.5;
    float clickPulse = isClicked ? 1.0 : 0.0;
    
    // Glowing aura around cursor
    float mouseGlow = smoothstep(0.35, 0.0, distToMouse);
    float3 auraColor = lerp(float3(0.1, 0.7, 1.0), float3(1.0, 0.3, 0.8), sin(i_time * 3.0) * 0.5 + 0.5);
    
    if (isClicked) {
        // Shockwave ring
        float ring = abs(distToMouse - frac(i_time * 2.0) * 0.6);
        float ringGlow = smoothstep(0.08, 0.0, ring);
        finalColor += float3(1.0, 0.8, 0.4) * ringGlow * 2.0;
    }

    finalColor += auraColor * mouseGlow * (0.8 + clickPulse * 1.2);

    // 5. Starfield Background
    float starGrid = hash21(floor(warpedUV * 80.0));
    if (starGrid > 0.97) {
        float starTwinkle = sin(i_time * 4.0 + starGrid * 100.0) * 0.5 + 0.5;
        finalColor += float3(starTwinkle, starTwinkle, starTwinkle) * 0.7;
    }

    // Vignette effect
    float vignette = 1.0 - length(input.UV - 0.5) * 0.8;
    finalColor *= saturate(vignette);

    return float4(finalColor, 1.0);
}
