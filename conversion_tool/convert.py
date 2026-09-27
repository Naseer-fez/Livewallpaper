
"""
ShaderToy GLSL to LiveWallpaper HLSL Conversion Tool
===================================================
A self-contained, lightweight Python script to convert standard ShaderToy GLSL 
shaders into DirectX 11 HLSL shaders compatible with the LiveWallpaper rendering engine.

Requirements:
- Python 3.x (Standard Library ONLY, no pip installations required).

Usage:
  python convert.py <input_shader.glsl> [output_shader.hlsl]
"""

import os
import sys
import re

HLSL_HEADER = """// =========================================================================
// Converted via LiveWallpaper Shader Conversion Tool
// Compatible with DX11 HLSL (ps_4_0 / ps_5_0 profiles)
// =========================================================================

cbuffer ShaderUniforms : register(b0) {
    float iTime;
    float3 iResolution;
    float4 iMouse;
    float4 iAudio;
    float iDepth;
    int iFrame;
};

struct VS_OUTPUT {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};
"""

def convert_atan_calls(text):
    # Convert 2-argument atan(y, x) to HLSL atan2(y, x)
    pos = 0
    while True:
        m = re.search(r'\batan\s*\(', text[pos:])
        if not m:
            break
        atan_start = pos + m.start()
        paren_start = pos + m.end() - 1  # index of '('
        depth = 1
        comma_idx = -1
        i = paren_start + 1
        n = len(text)
        while i < n and depth > 0:
            ch = text[i]
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ',' and depth == 1:
                comma_idx = i
            i += 1
        if depth == 0 and comma_idx != -1:
            # 2-argument atan -> replace with atan2
            text = text[:atan_start] + "atan2" + text[atan_start + 4:]
            pos = atan_start + 5
        else:
            pos = paren_start + 1
    return text

def find_matching_brace(code, open_pos):
    depth = 1
    i = open_pos + 1
    n = len(code)
    in_line_comment = False
    in_block_comment = False
    while i < n:
        if in_line_comment:
            if code[i] == '\n':
                in_line_comment = False
        elif in_block_comment:
            if code[i:i+2] == '*/':
                in_block_comment = False
                i += 1
        else:
            if code[i:i+2] == '//':
                in_line_comment = True
                i += 1
            elif code[i:i+2] == '/*':
                in_block_comment = True
                i += 1
            elif code[i] == '{':
                depth += 1
            elif code[i] == '}':
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    return -1

def convert_glsl_to_hlsl(glsl_code):
    # Keep track of mouse usage
    has_mouse = "iMouse" in glsl_code
    
    # 1. Strip GLSL precision qualifiers
    code = re.sub(r'\b(lowp|mediump|highp)\b\s*', '', glsl_code)

    # 2. Convert standard GLSL types to HLSL
    # Vector types: vec2 -> float2, vec3 -> float3, vec4 -> float4
    code = re.sub(r'\bvec([234])\b', r'float\1', code)
    # Boolean vector types: bvec2 -> bool2, etc.
    code = re.sub(r'\bbvec([234])\b', r'bool\1', code)
    # Integer vector types: ivec2 -> int2, etc.
    code = re.sub(r'\bivec([234])\b', r'int\1', code)
    # Unsigned vector types: uvec2 -> uint2, etc.
    code = re.sub(r'\buvec([234])\b', r'uint\1', code)
    
    # Matrix types: mat2 -> float2x2, mat3 -> float3x3, mat4 -> float4x4
    code = re.sub(r'\bmat([234])\b', r'float\1x\1', code)

    # 3. Convert standard GLSL intrinsic functions to HLSL
    code = re.sub(r'\bfract\b', 'frac', code)
    code = re.sub(r'\bmix\b', 'lerp', code)
    code = re.sub(r'\bmod\b', 'fmod', code)
    code = re.sub(r'\binversesqrt\b', 'rsqrt', code)
    code = re.sub(r'\bdFdx\b', 'ddx', code)
    code = re.sub(r'\bdFdy\b', 'ddy', code)

    # Convert 2-argument atan(y, x) to HLSL atan2(y, x)
    code = convert_atan_calls(code)

    # 4. Handle ShaderToy mainImage entrypoint conversion
    main_pattern = r'void\s+mainImage\s*\(\s*out\s+(\w+)\s+(\w+)\s*,\s*in\s+(\w+)\s+(\w+)\s*\)'
    match = re.search(main_pattern, code)
    
    if match:
        color_type, color_name = match.group(1), match.group(2)
        coord_type, coord_name = match.group(3), match.group(4)
        
        # Replace mainImage signature with HLSL main signature
        hlsl_sig = "float4 main(VS_OUTPUT input) : SV_Target"
        code = re.sub(main_pattern, hlsl_sig, code)
        
        sig_index = code.find(hlsl_sig)
        if sig_index != -1:
            brace_index = code.find('{', sig_index)
            if brace_index != -1:
                close_brace = find_matching_brace(code, brace_index)
                if close_brace != -1:
                    code = code[:close_brace] + f"\n    return {color_name};\n" + code[close_brace:]

                # Coordinate mapping logic
                injection = f"\n    float2 {coord_name} = input.TexCoord * iResolution.xy;"
                
                # If the shader does not already have mouse interaction, inject camera sway parallax
                if not has_mouse:
                    injection += f"\n    // Universal mouse interactive sway injected by Conversion Tool\n"
                    injection += f"    if (iMouse.x > 0.0f) {{\n"
                    injection += f"        {coord_name} += (iMouse.xy - iResolution.xy * 0.5f) * 0.15f;\n"
                    injection += f"    }}\n"
                
                injection += f"    float4 {color_name} = float4(0.0f, 0.0f, 0.0f, 1.0f);\n"
                
                # Insert at opening brace
                code = code[:brace_index + 1] + injection + code[brace_index + 1:]
    
    # 5. Prepend HLSL constant buffers & header
    full_hlsl = HLSL_HEADER + "\n" + code
    return full_hlsl

def main():
    if len(sys.argv) < 2:
        print("Error: No input shader file provided.", file=sys.stderr)
        print("Usage: python convert.py <input_shader.glsl> [output_shader.hlsl]", file=sys.stderr)
        sys.exit(1)

    input_path = sys.argv[1]
    if not os.path.exists(input_path):
        print(f"Error: Input file '{input_path}' does not exist.", file=sys.stderr)
        sys.exit(1)

    # Determine output path
    if len(sys.argv) >= 3:
        output_path = sys.argv[2]
    else:
        base, _ = os.path.splitext(input_path)
        output_path = base + ".hlsl"

    print(f"Reading GLSL shader from: {input_path}")
    with open(input_path, 'r', encoding='utf-8') as f:
        glsl_content = f.read()

    print("Converting GLSL to HLSL...")
    hlsl_content = convert_glsl_to_hlsl(glsl_content)

    print(f"Writing converted HLSL shader to: {output_path}")
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(hlsl_content)

    print("\n[Success] Conversion completed successfully!")
    print("--------------------------------------------------")
    print("Notes:")
    print("1. Entry point 'mainImage' has been converted to DX11 'main'.")
    print("2. Required DirectX constant buffers & structs have been prepended.")
    print("3. Vector & Matrix types (vec/mat) mapped to float/floatMxN.")
    print("4. Standard math intrinsics (fract/mix/mod/atan) mapped to HLSL counterparts.")
    print("5. Universal mouse interactive parallax sway has been injected (if iMouse was unused).")
    print("--------------------------------------------------")

if __name__ == "__main__":
    main()
