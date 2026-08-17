#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform Push {
    vec2 inv_canvas_size;
    float is_text;   // Nonzero when this batch samples an R8 glyph atlas as alpha coverage.
    float _pad;
} pc;

void main() {
    if (pc.is_text != 0.0) {
        float coverage = texture(tex, v_uv).r;
        out_color = vec4(v_color.rgb, v_color.a * coverage);
    } else {
        out_color = texture(tex, v_uv) * v_color;
    }
}
