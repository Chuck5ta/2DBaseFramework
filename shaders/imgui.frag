#version 450

// Dear ImGui's fragment shader with one change: ImGui's colours are sRGB values
// (as seen on screen), but our swapchain is an sRGB format that expects linear
// colours and converts them on write. Converting here keeps ImGui's colours
// exactly as designed instead of washed out. Used by debugui/DebugUI.cpp.

layout(set = 0, binding = 0) uniform texture2D _Texture;
layout(set = 1, binding = 0) uniform sampler _Sampler;

layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout(location = 0) out vec4 fColor;

vec3 srgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main()
{
    fColor = vec4(srgbToLinear(In.Color.rgb), In.Color.a) * texture(sampler2D(_Texture, _Sampler), In.UV.st);
}
