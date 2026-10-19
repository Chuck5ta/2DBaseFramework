#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor; // RGBA8 unorm, sRGB-encoded

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

vec3 srgbToLinear(vec3 c)
{
    // Exact piecewise sRGB EOTF.
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main()
{
    gl_Position = pc.viewProjection * vec4(inPosition, 0.0, 1.0);
    outUV = inUV;
    // Tint colours are authored in sRGB (like in an image editor); the
    // swapchain expects linear values, so convert once per vertex.
    outColor = vec4(srgbToLinear(inColor.rgb), inColor.a);
}
