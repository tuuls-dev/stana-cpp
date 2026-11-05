// Application-owned WGSL. Logical bindings are shared across native adapters.
struct Level { value: vec4<f32>, }
@group(0) @binding(0) var<uniform> level: Level;
@group(0) @binding(1) var field: texture_storage_2d<rgba16float, write>;
@group(0) @binding(2) var image: texture_2d<f32>;
@group(0) @binding(3) var imageSampler: sampler;
@group(0) @binding(4) var previous: texture_2d<f32>;
@group(0) @binding(5) var next: texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn generate(@builtin(global_invocation_id) id: vec3<u32>) {
    let size = textureDimensions(field);
    if (id.x >= size.x || id.y >= size.y) { return; }
    let old = textureLoad(previous, vec2<i32>(0), 0).x;
    let uv = (vec2<f32>(id.xy) + 0.5) / vec2<f32>(size);
    let strength = level.value.x;
    let color = vec3<f32>(uv.x * strength, uv.y, 0.25 + 0.05 * (old % 4.0));
    textureStore(field, vec2<i32>(id.xy), vec4<f32>(color, 1.0));
    if (id.x == 0u && id.y == 0u) {
        textureStore(next, vec2<i32>(0), vec4<f32>(old + 1.0, 0.0, 0.0, 1.0));
    }
}

struct Vertex { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32>, }
@vertex fn fullscreen(@builtin(vertex_index) id: u32) -> Vertex {
    let positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    let p = positions[id];
    return Vertex(vec4<f32>(p, 0.0, 1.0), vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5));
}
@fragment fn shade(input: Vertex) -> @location(0) vec4<f32> {
    return textureSample(image, imageSampler, input.uv);
}
