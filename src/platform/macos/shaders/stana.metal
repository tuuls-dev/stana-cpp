#include <metal_stdlib>
using namespace metal;
struct Quad { float4 bounds; float4 geometry; float4 color; float4 viewport; float4 options; uint4 flags; };
struct Clip { float4 bounds; float4 options; };
struct Vertex { packed_float2 position; packed_float4 color; };
struct Varying { float4 position [[position]]; float2 point; float2 uv; float4 color; };
float3 decode(float3 value) {
    float3 a = abs(value);
    return sign(value) * select(pow((a + 0.055f) / 1.055f, float3(2.4f)), a / 12.92f, a <= 0.04045f);
}
float3 encode(float3 value) {
    float3 a = abs(value);
    return sign(value) * select(1.055f * pow(a, float3(1.0f / 2.4f)) - 0.055f, a * 12.92f, a <= 0.0031308f);
}
float4 convert(float4 color, bool sourceLinear, bool destinationLinear) {
    if (color.a <= 0) return float4(0);
    if (sourceLinear != destinationLinear) {
        float3 straight = color.rgb / color.a;
        color.rgb = (destinationLinear ? decode(straight) : encode(straight)) * color.a;
    }
    return color;
}
float rectangleDistance(float2 point, float4 bounds, float radius) {
    float2 halfSize = bounds.zw * 0.5f;
    float r = min(radius, min(halfSize.x, halfSize.y));
    float2 d = abs(point - bounds.xy - halfSize) - (halfSize - r);
    return length(max(d, float2(0))) + min(max(d.x, d.y), 0.0f) - r;
}
float coverage(float distance) { return clamp(0.5f - distance / max(fwidth(distance), 0.00001f), 0.0f, 1.0f); }
float clipping(float2 point, constant Quad &quad, device const Clip *clips) {
    float value = 1;
    for (uint i = 0; i < quad.flags.y; ++i) {
        if (clips[i].bounds.z <= 0 || clips[i].bounds.w <= 0) return 0;
        value *= coverage(rectangleDistance(point, clips[i].bounds, clips[i].options.x));
    }
    return value;
}
vertex Varying stana_quad(uint index [[vertex_id]], constant Quad &quad [[buffer(0)]]) {
    const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1), float2(0,1), float2(1,0), float2(1,1)};
    float2 uv = corners[index];
    float2 p = quad.bounds.xy + uv * quad.bounds.zw;
    float2 ndc = (p - quad.viewport.xy) / quad.viewport.zw;
    Varying out;
    out.position = float4(ndc.x * 2 - 1, 1 - ndc.y * 2, 0, 1);
    out.point = p; out.uv = uv; out.color = quad.color;
    return out;
}
fragment float4 stana_shape(Varying in [[stage_in]], constant Quad &quad [[buffer(0)]],
    device const Clip *clips [[buffer(1)]], device const packed_float2 *points [[buffer(2)]]) {
    float distance;
    if (quad.flags.x == 0) distance = rectangleDistance(in.point, quad.geometry, quad.options.x);
    else if (quad.flags.x == 1) {
        float2 radii = max(quad.geometry.zw * 0.5f, float2(0.00001f));
        float2 n = (in.point - quad.geometry.xy - radii) / radii;
        distance = (length(n) - 1) * min(radii.x, radii.y);
    } else {
        float2 p = in.point - quad.geometry.xy;
        uint count = uint(quad.options.w);
        distance = INFINITY;
        if (count == 1) distance = length(p - float2(points[0]));
        for (uint i = 1; i < count; ++i) {
            float2 a = float2(points[i-1]), b = float2(points[i]), v = b-a;
            float t = dot(v,v) > 0 ? clamp(dot(p-a,v) / dot(v,v), 0.0f, 1.0f) : 0;
            distance = min(distance, length(p-a-t*v));
        }
        distance -= quad.options.x * 0.5f;
    }
    return quad.color * (quad.options.y * coverage(distance) * clipping(in.point, quad, clips));
}
fragment float4 stana_texture(Varying in [[stage_in]], constant Quad &quad [[buffer(0)]],
    device const Clip *clips [[buffer(1)]], texture2d<float> image [[texture(0)]]) {
    constexpr sampler sample(coord::normalized, address::clamp_to_zero, filter::linear);
    float4 value = image.sample(sample, in.uv);
    if (quad.options.w == 0) value.rgb *= value.a;
    value = convert(value, quad.flags.w != 0, quad.flags.z != 0);
    return value * (quad.options.y * clipping(in.point, quad, clips));
}
vertex Varying stana_mesh(uint index [[vertex_id]], device const Vertex *vertices [[buffer(0)]], constant Quad &quad [[buffer(1)]]) {
    Vertex item = vertices[index];
    float2 point = float2(item.position) + quad.geometry.xy;
    float2 ndc = (point - quad.viewport.xy) / quad.viewport.zw;
    Varying out;
    out.position = float4(ndc.x * 2 - 1, 1 - ndc.y * 2, 0, 1);
    out.point = point; out.uv = float2(0); out.color = float4(item.color);
    return out;
}
fragment float4 stana_mesh_fragment(Varying in [[stage_in]], constant Quad &quad [[buffer(0)]], device const Clip *clips [[buffer(1)]]) {
    return convert(in.color, true, quad.flags.z != 0) * (quad.options.y * clipping(in.point, quad, clips));
}
fragment float4 stana_display(Varying in [[stage_in]], constant Quad &quad [[buffer(0)]], texture2d<float> image [[texture(0)]]) {
    constexpr sampler sample(coord::normalized, address::clamp_to_edge, filter::nearest);
    float4 value = image.sample(sample, in.uv);
    if (value.a <= 0) return float4(0);
    float3 c = value.rgb / value.a;
    float level = max(0.0f, max(c.x, max(c.y,c.z)));
    float high = max(1.0f, quad.options.z);
    float knee = min(1.0f, 0.75f * high);
    if (level > knee) c *= (knee + (high-knee)*(level-knee)/((level-knee)+(high-knee))) / level;
    if (quad.flags.z == 0) c = clamp(encode(c), 0.0f, 1.0f);
    return float4(c * value.a, value.a);
}
