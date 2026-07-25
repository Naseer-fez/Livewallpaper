cbuffer ShaderUniforms : register(b0) {
    float i_time;
    float3 i_resolution; // (width, height, aspect)
    float4 i_mouse;      // (x_pixel, y_pixel, is_click, padding)
    float4 i_audio;
    float i_depth;
    int i_frame;
};

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    float2 uv = (input.UV - 0.5);
    uv.x *= i_resolution.z; // Aspect ratio correction

    // Mouse interactive offset (-0.5 to 0.5)
    float2 mouseOffset = (i_mouse.xy / i_resolution.xy) - 0.5;
    bool isClick = i_mouse.z > 0.5;

    // Camera perspective projection for 3D ground grid
    float3 rayDir = normalize(float3(uv.x - mouseOffset.x * 0.3, uv.y - mouseOffset.y * 0.2, 0.8));
    
    float3 color = float3(0.02, 0.01, 0.05); // Dark synthwave sky background

    // 1. Synthwave Sun
    float2 sunUV = uv - float2(mouseOffset.x * 0.1, 0.15);
    float sunDist = length(sunUV);
    if (sunDist < 0.25) {
        // Sun gradient (yellow top to magenta bottom)
        float3 sunColor = lerp(float3(1.0, 0.9, 0.2), float3(1.0, 0.1, 0.6), sunUV.y * 3.0 + 0.5);
        
        // Horizontal sun cuts / lines
        float lines = sin((sunUV.y + i_time * 0.05) * 80.0);
        if (sunUV.y < 0.0 && lines < -0.2) {
            sunColor *= 0.1; // Cut out line gap
        }
        color = lerp(color, sunColor, smoothstep(0.25, 0.24, sunDist));
    }
    
    // Sun outer halo glow
    float sunGlow = smoothstep(0.45, 0.0, sunDist);
    color += float3(1.0, 0.2, 0.6) * sunGlow * 0.5;

    // 2. 3D Floor Grid Plane (Y < 0.0)
    if (rayDir.y < 0.0) {
        // Raycast to plane Y = -0.4
        float t = -0.4 / rayDir.y;
        float3 hitPos = rayDir * t;

        // Animate grid forward motion
        hitPos.z += i_time * 2.0;

        // Grid lines math
        float2 gridUV = frac(hitPos.xz * 1.5) - 0.5;
        float gridLine = smoothstep(0.45, 0.5, max(abs(gridUV.x), abs(gridUV.y)));

        // Neon Pink / Cyan grid colors
        float3 neonPink = float3(1.0, 0.05, 0.6);
        float3 neonCyan = float3(0.0, 0.9, 1.0);
        float3 gridColor = lerp(neonPink, neonCyan, sin(hitPos.z * 0.2) * 0.5 + 0.5);

        // Distance fog fadeout towards horizon
        float fog = exp(-t * 0.15);

        // Mouse click laser energy beam effect
        float laserIntensity = 1.0;
        if (isClick) {
            float laserTrack = abs(hitPos.x - mouseOffset.x * 5.0);
            if (laserTrack < 0.15) {
                gridColor += float3(1.0, 1.0, 1.0) * (0.15 / (laserTrack + 0.01));
            }
        }

        color = lerp(color, gridColor * (gridLine * 2.5 + 0.2), fog);
    }

    // Mouse cursor reactive light spot
    float2 mouseUV = (i_mouse.xy / i_resolution.xy) - 0.5;
    mouseUV.x *= i_resolution.z;
    float cursorGlow = smoothstep(0.2, 0.0, length(uv - mouseUV));
    color += float3(0.0, 0.8, 1.0) * cursorGlow * (isClick ? 1.5 : 0.6);

    return float4(color, 1.0);
}
