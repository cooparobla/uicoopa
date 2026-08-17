#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(push_constant) uniform Push {
    vec2 inv_canvas_size;
    float is_text;   // Nonzero when this batch samples an R8 glyph atlas as alpha coverage.
    float _pad;
} pc;

void main() {
    v_uv = in_uv;
    v_color = in_color;

    // Canvas space is +Y up, origin bottom-left; Vulkan NDC is +Y down.
    vec2 ndc = in_pos * pc.inv_canvas_size * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
