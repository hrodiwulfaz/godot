#define MAX_LIGHTS_PER_ITEM 16

#define M_PI 3.14159265359

#define SDF_MAX_LENGTH 16384.0

#define INSTANCE_FLAGS_LIGHT_COUNT_SHIFT 0 // 4 bits.

#define INSTANCE_FLAGS_CLIP_RECT_UV (1 << 4)
#define INSTANCE_FLAGS_TRANSPOSE_RECT (1 << 5)
#define INSTANCE_FLAGS_MASK_INDEX_SHIFT 6

#define INSTANCE_FLAGS_NINEPATCH_DRAW_CENTER_SHIFT 8
#define INSTANCE_FLAGS_NINEPATCH_H_MODE_SHIFT 9
#define INSTANCE_FLAGS_NINEPATCH_V_MODE_SHIFT 11

#define INSTANCE_FLAGS_SHADOW_MASKED_SHIFT 13 // 16 bits.
#define INSTANCE_FLAGS_SHADOW_MASKED (1 << INSTANCE_FLAGS_SHADOW_MASKED_SHIFT)
#define INSTANCE_FLAGS_USE_ARRAY_LAYER (1U << 29)
#define INSTANCE_FLAGS_USE_REGION_SAMPLING (1U << 30)
#define INSTANCE_FLAGS_MASK_REVERSE (1U << 31)

struct InstanceData {
	vec2 world_x;
	vec2 world_y;
	vec2 world_ofs;
	vec2 ninepatch_pixel_size;
#ifdef USE_PRIMITIVE
	vec2 points[3];
	vec2 uvs[3];
	uint colors[6];
#else
	vec4 modulation;
	vec4 ninepatch_margins;
	vec4 dst_rect; //for built-in rect and UV
	vec4 src_rect;
	uint item_data;
	uint reserved;

#endif
	uint flags;
	uint instance_uniforms_ofs;
	uvec4 lights;
};

//1 means enabled, 2+ means trails in use
#define BATCH_FLAGS_INSTANCING_MASK 0x7F
#define BATCH_FLAGS_INSTANCING_HAS_COLORS_SHIFT 7
#define BATCH_FLAGS_INSTANCING_HAS_COLORS (1 << BATCH_FLAGS_INSTANCING_HAS_COLORS_SHIFT)
#define BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA_SHIFT 8
#define BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA (1 << BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA_SHIFT)

#define BATCH_FLAGS_DEFAULT_NORMAL_MAP_USED (1 << 9)
#define BATCH_FLAGS_DEFAULT_SPECULAR_MAP_USED (1 << 10)
#define BATCH_FLAGS_DEFORM_GRID (1 << 11)
#define BATCH_FLAGS_DEFORM_DENSE (1 << 12)
#define BATCH_FLAGS_DEFORM_WARP (1 << 13)

// Deform word of a rectangle (instance word at byte 100): chain and one bit per mask slot that follows the bend.
#define DEFORM_CHAIN_MASK 0x3FFFFFu
// Chain words: record row offset plus one, and in the high byte the follow codes of two mask slots.
#define DEFORM_RECORD_MASK 0xFFFFFFu
#define DEFORM_FOLLOW_CODE_SHIFT 24u
// Joints per record, and bands per rectangle, that place grid lines.
#define DEFORM_BAND_JOINTS 4u
#define DEFORM_GRID_COLUMNS 4u
#define DEFORM_GRID_ROWS 16u
#define DEFORM_DENSE_GRID_ROWS 32u
#define DEFORM_WARP_GRID_COLUMNS 24u
#define DEFORM_WARP_GRID_ROWS 32u

layout(push_constant, std430) uniform Params {
	uint sc_packed_0;
	uint specular_shininess;
	uint batch_flags;
	float region_mipmap_bias;

	vec2 msdf;
	vec2 color_texture_pixel_size;
#ifdef USE_UNIFORM_MASK
	vec4 mask_regions[2];
	vec4 mask_extras[2];
	vec4 mask_basis[2];
#endif
#ifdef USE_ATTRIBUTES
	// Particles and meshes
	vec2 world_x;
	vec2 world_y;

	vec2 world_ofs;
	uint flags;
	uint instance_uniforms_ofs;

	vec4 modulation;
	uvec4 lights;
#endif
}
params;

// Specialization constants.

#ifdef UBERSHADER

// Pull the constants from the draw call's push constants.
uint sc_packed_0() {
	return params.sc_packed_0;
}

#else

// Pull the constants from the pipeline's specialization constants.
layout(constant_id = 0) const uint pso_sc_packed_0 = 0;

uint sc_packed_0() {
	return pso_sc_packed_0;
}

#endif

bool sc_mask_mipmaps() {
	return ((sc_packed_0() >> 17) & 1U) != 0;
}

bool sc_use_deform() {
	return ((sc_packed_0() >> 18) & 1U) != 0;
}

bool sc_mask_all_valid() {
	return ((sc_packed_0() >> 7) & 1U) != 0;
}

bool sc_mask_uniform_slots() {
	return ((sc_packed_0() >> 8) & 1U) != 0;
}

uint sc_mask_uniform_index(uint slot) {
	return (sc_packed_0() >> (9U + slot)) & 1U;
}

bool sc_mask_shared_basis() {
	return ((sc_packed_0() >> 16) & 1U) != 0;
}

bool sc_mask_same_uv() {
	return ((sc_packed_0() >> 11) & 1U) != 0;
}

bool sc_mask_valid(uint index) {
	return ((sc_packed_0() >> (12U + index)) & 1U) != 0;
}

float sc_mask_outside(uint index) {
	return float((sc_packed_0() >> (14U + index)) & 1U);
}

bool sc_use_lighting() {
	return ((sc_packed_0() >> 0) & 1U) != 0;
}

bool sc_use_msdf() {
	return ((sc_packed_0() >> 1) & 1U) != 0;
}

bool sc_use_lcd() {
	return ((sc_packed_0() >> 2) & 1U) != 0;
}

bool sc_use_texture_array() {
	return ((sc_packed_0() >> 3) & 1U) != 0;
}

bool sc_use_region_sampling() {
	return ((sc_packed_0() >> 4) & 1U) != 0;
}

bool sc_region_mip_nearest() {
	return ((sc_packed_0() >> 5) & 1U) != 0;
}

bool sc_region_mip_trilinear() {
	return ((sc_packed_0() >> 6) & 1U) != 0;
}

// In vulkan, sets should always be ordered using the following logic:
// Lower Sets: Sets that change format and layout less often
// Higher sets: Sets that change format and layout very often
// This is because changing a set for another with a different layout or format,
// invalidates all the upper ones (as likely internal base offset changes)

/* SET0: Globals */

#define CANVAS_FLAGS_CONVERT_ATTRIBUTES_TO_LINEAR (1 << 0)

// The values passed per draw primitives are cached within it

layout(set = 0, binding = 0) uniform texture2DArray mask_page;
layout(set = 0, binding = 8, std430) restrict readonly buffer MaskData {
	uvec4 rows[];
} mask_data;

// Deform chains (record row offsets plus one, innermost first), then deform records from deform_record_base.
layout(set = 0, binding = 30, std430) restrict readonly buffer DeformData {
	vec4 rows[];
} deform_data;

layout(set = 0, binding = 1, std140) uniform CanvasData {
	mat4 canvas_transform;
	mat4 screen_transform;
	mat4 canvas_normal_transform;
	vec4 canvas_modulation;
	vec2 screen_pixel_size;
	float time;
	bool use_pixel_snap;

	vec4 sdf_to_tex;
	vec2 screen_to_sdf;
	vec2 sdf_to_screen;

	uint directional_light_count;
	float tex_to_sdf;
	float shadow_pixel_size;
	uint flags;
	uint mask_entries_base;
	uint deform_record_base;
	uint mask_padding_1;
	uint mask_padding_2;
}
canvas_data;

#define LIGHT_FLAGS_BLEND_MASK (3 << 16)
#define LIGHT_FLAGS_BLEND_MODE_ADD (0 << 16)
#define LIGHT_FLAGS_BLEND_MODE_SUB (1 << 16)
#define LIGHT_FLAGS_BLEND_MODE_MIX (2 << 16)
#define LIGHT_FLAGS_BLEND_MODE_MASK (3 << 16)
#define LIGHT_FLAGS_HAS_SHADOW (1 << 20)
#define LIGHT_FLAGS_FILTER_SHIFT 22
#define LIGHT_FLAGS_FILTER_MASK (3 << 22)
#define LIGHT_FLAGS_SHADOW_NEAREST (0 << 22)
#define LIGHT_FLAGS_SHADOW_PCF5 (1 << 22)
#define LIGHT_FLAGS_SHADOW_PCF13 (2 << 22)

struct Light {
	mat2x4 texture_matrix; //light to texture coordinate matrix (transposed)
	mat2x4 shadow_matrix; //light to shadow coordinate matrix (transposed)
	vec4 color;

	uint shadow_color; // packed
	uint flags; //index to light texture
	float shadow_pixel_size;
	float height;

	vec2 position;
	float shadow_zfar_inv;
	float shadow_y_ofs;

	vec4 atlas_rect;
};

layout(set = 0, binding = 2, std430) restrict readonly buffer LightData {
	Light data[];
}
light_array;

layout(set = 0, binding = 3) uniform texture2D atlas_texture;
layout(set = 0, binding = 4) uniform texture2D shadow_atlas_texture;

layout(set = 0, binding = 5) uniform sampler shadow_sampler;

layout(set = 0, binding = 6) uniform texture2D color_buffer;
layout(set = 0, binding = 7) uniform texture2D sdf_texture;

#include "samplers_inc.glsl"

layout(set = 0, binding = 9, std430) restrict readonly buffer GlobalShaderUniformData {
	vec4 data[];
}
global_shader_uniforms;

/* SET1: Is reserved for the material */

//

/* SET2: Instancing and Skeleton */

layout(set = 2, binding = 0, std430) restrict readonly buffer Transforms {
	vec4 data[];
}
transforms;

/* SET3: Texture */

layout(set = 3, binding = 0) uniform texture2D color_texture;
layout(set = 3, binding = 1) uniform texture2D normal_texture;
layout(set = 3, binding = 2) uniform texture2D specular_texture;
layout(set = 3, binding = 3) uniform sampler texture_sampler;
layout(set = 3, binding = 4) uniform texture2DArray color_texture_array;
