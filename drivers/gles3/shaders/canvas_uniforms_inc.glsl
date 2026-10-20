#define MAX_LIGHTS_PER_ITEM uint(16)

// Mask rows, then deform rows from texel deform_texel_base, share one texture.
#if defined(USE_MASKS) || defined(USE_DEFORM)
uniform highp usampler2D mask_rows; //texunit:-8

uvec4 read_mask_row(uint row) {
	return texelFetch(mask_rows, ivec2(row % 1024u, row / 1024u), 0);
}
#endif

#ifdef USE_MASKS
uniform sampler2DArray mask_page; //texunit:-9
uniform bool mask_mipmaps;
#endif

#define M_PI 3.14159265359

#define SDF_MAX_LENGTH 16384.0

#define INSTANCE_FLAGS_LIGHT_COUNT_SHIFT 0 // 4 bits.

#define INSTANCE_FLAGS_CLIP_RECT_UV uint(1 << 4)
#define INSTANCE_FLAGS_TRANSPOSE_RECT uint(1 << 5)
#define INSTANCE_FLAGS_USE_MSDF uint(1 << 6)
#define INSTANCE_FLAGS_USE_LCD uint(1 << 7)

#define INSTANCE_FLAGS_NINEPATCH_DRAW_CENTER uint(1 << 8)
#define INSTANCE_FLAGS_NINEPATCH_H_MODE_SHIFT 9
#define INSTANCE_FLAGS_NINEPATCH_V_MODE_SHIFT 11

#define INSTANCE_FLAGS_SHADOW_MASKED_SHIFT 13u // 16 bits.
#define INSTANCE_FLAGS_SHADOW_MASKED uint(1 << INSTANCE_FLAGS_SHADOW_MASKED_SHIFT)
#define INSTANCE_FLAGS_USE_ARRAY_LAYER uint(1U << 29)
#define INSTANCE_FLAGS_USE_REGION_SAMPLING uint(1U << 30)

// 1 means enabled, 2+ means trails in use
#define BATCH_FLAGS_INSTANCING_MASK uint(0x7F)
#define BATCH_FLAGS_INSTANCING_HAS_COLORS_SHIFT 7
#define BATCH_FLAGS_INSTANCING_HAS_COLORS uint(1 << BATCH_FLAGS_INSTANCING_HAS_COLORS_SHIFT)
#define BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA_SHIFT 8
#define BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA uint(1 << BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA_SHIFT)

#define BATCH_FLAGS_DEFAULT_NORMAL_MAP_USED uint(1 << 9)
#define BATCH_FLAGS_DEFAULT_SPECULAR_MAP_USED uint(1 << 10)
#define BATCH_FLAGS_DEFORM_GRID uint(1 << 11)
#define BATCH_FLAGS_DEFORM_DENSE uint(1 << 12)
#define BATCH_FLAGS_DEFORM_WARP uint(1 << 13)

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

layout(std140) uniform GlobalShaderUniformData { //ubo:1
	vec4 global_shader_uniforms[MAX_GLOBAL_SHADER_UNIFORMS];
};

layout(std140) uniform CanvasData { //ubo:0
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
	uint mask_entries_base;
	uint deform_texel_base;
};

#ifndef DISABLE_LIGHTING
#define LIGHT_FLAGS_BLEND_MASK uint(3 << 16)
#define LIGHT_FLAGS_BLEND_MODE_ADD uint(0 << 16)
#define LIGHT_FLAGS_BLEND_MODE_SUB uint(1 << 16)
#define LIGHT_FLAGS_BLEND_MODE_MIX uint(2 << 16)
#define LIGHT_FLAGS_BLEND_MODE_MASK uint(3 << 16)
#define LIGHT_FLAGS_HAS_SHADOW uint(1 << 20)
#define LIGHT_FLAGS_FILTER_SHIFT 22
#define LIGHT_FLAGS_FILTER_MASK uint(3 << 22)
#define LIGHT_FLAGS_SHADOW_NEAREST uint(0 << 22)
#define LIGHT_FLAGS_SHADOW_PCF5 uint(1 << 22)
#define LIGHT_FLAGS_SHADOW_PCF13 uint(2 << 22)

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

layout(std140) uniform LightData { //ubo:2
	Light light_array[MAX_LIGHTS];
};
#endif // DISABLE_LIGHTING
