/* clang-format off */
#[modes]

mode_default =

#[specializations]

DISABLE_LIGHTING = true
USE_RGBA_SHADOWS = false
USE_NINEPATCH = false
USE_PRIMITIVE = false
USE_ATTRIBUTES = false
USE_INSTANCING = false
USE_PAGE_RECT = false
USE_TEXTURE_ARRAY = false
REGION_MIP_NEAREST = false
REGION_MIP_TRILINEAR = false
USE_MASKS = false
USE_DEFORM = false

#[vertex]

#ifdef USE_ATTRIBUTES
layout(location = 0) in vec2 vertex_attrib;
layout(location = 3) in vec4 color_attrib;
layout(location = 4) in vec2 uv_attrib;

#ifdef USE_INSTANCING

layout(location = 1) in highp vec4 instance_xform0;
layout(location = 2) in highp vec4 instance_xform1;
layout(location = 5) in highp uvec4 instance_color_custom_data; // Color packed into xy, custom_data packed into zw for compatibility with 3D

#endif // USE_INSTANCING

#endif // USE_ATTRIBUTES

#include "stdlib_inc.glsl"

#if defined(CUSTOM0_USED)
layout(location = 6) in highp vec4 custom0_attrib;
#endif

#if defined(CUSTOM1_USED)
layout(location = 7) in highp vec4 custom1_attrib;
#endif

layout(location = 8) in highp vec4 attrib_A;
layout(location = 9) in highp vec4 attrib_B;
layout(location = 10) in highp vec4 attrib_C;
layout(location = 11) in highp vec4 attrib_D;
layout(location = 12) in highp vec4 attrib_E;
#ifdef USE_PRIMITIVE
layout(location = 13) in highp uvec4 attrib_F;
#else
layout(location = 13) in highp vec4 attrib_F;
#endif
layout(location = 14) in highp uvec4 attrib_G;
layout(location = 15) in highp uvec4 attrib_H;

#define read_draw_data_world_x attrib_A.xy
#define read_draw_data_world_y attrib_A.zw
#define read_draw_data_world_ofs attrib_B.xy
#define read_draw_data_color_texture_pixel_size attrib_B.zw

#ifdef USE_PRIMITIVE

#define read_draw_data_point_a attrib_C.xy
#define read_draw_data_point_b attrib_C.zw
#define read_draw_data_point_c attrib_D.xy
#define read_draw_data_uv_a attrib_D.zw
#define read_draw_data_uv_b attrib_E.xy
#define read_draw_data_uv_c attrib_E.zw

#define read_draw_data_color_a_rg attrib_F.x
#define read_draw_data_color_a_ba attrib_F.y
#define read_draw_data_color_b_rg attrib_F.z
#define read_draw_data_color_b_ba attrib_F.w
#define read_draw_data_color_c_rg attrib_G.x
#define read_draw_data_color_c_ba attrib_G.y

#else

#define read_draw_data_modulation attrib_C
#define read_draw_data_ninepatch_margins attrib_D
#define read_draw_data_dst_rect attrib_E
#define read_draw_data_src_rect attrib_F
#define read_draw_data_item_data attrib_G.x
#define read_draw_data_deform attrib_G.y
#define read_draw_data_array_layer (read_draw_data_item_data & 0x7FFu)
#define read_draw_data_max_region_lod ((read_draw_data_item_data >> 11u) & 0x1Fu)

#endif

#if defined(USE_DEFORM) && !defined(USE_PRIMITIVE) && !defined(USE_ATTRIBUTES) && !defined(USE_NINEPATCH)
#define DEFORM_RECT
#endif

#define read_draw_data_flags attrib_G.z
#define read_draw_data_instance_offset attrib_G.w
#define read_draw_data_lights attrib_H

// Varyings so the per-instance info can be used in the fragment shader
flat out vec4 varying_A;
flat out vec2 varying_B;
#ifndef USE_PRIMITIVE
flat out vec4 varying_C;
#ifndef USE_ATTRIBUTES
#ifdef USE_NINEPATCH

flat out vec2 varying_D;
#endif
flat out vec4 varying_E;
#endif
#endif
// Flags, instance offset, item data and the follow codes of the mask slots.
flat out uvec4 varying_F;
flat out uvec4 varying_G;
#ifdef USE_MASKS
out highp vec2 mask_uv_interp;
flat out highp vec4 mask_region_interp;
flat out highp vec4 mask_extra_interp;
#ifdef DEFORM_RECT
// Screen places of the point after none and one (deform_rest_interp) and two and three (deform_rest_far_interp) records
// of its chain, where masks that follow the bend are sampled.
out highp vec4 deform_rest_interp;
out highp vec4 deform_rest_far_interp;
#endif
#endif

// This needs to be outside clang-format so the ubo comment is in the right place
#ifdef MATERIAL_UNIFORMS_USED
layout(std140) uniform MaterialUniforms{ //ubo:4

#MATERIAL_UNIFORMS

};
#endif

uniform mediump uint batch_flags;

/* clang-format on */
#include "canvas_uniforms_inc.glsl"

out vec2 uv_interp;
out vec4 color_interp;
out vec2 vertex_interp;

#ifdef USE_NINEPATCH

out vec2 pixel_size_interp;

#endif

#GLOBALS

#ifdef DEFORM_RECT
vec4 deform_row(uint p_texel) {
	return uintBitsToFloat(read_mask_row(p_texel));
}

vec2 deform_warp_point(uint p_base, uint p_index) {
	vec4 row = deform_row(p_base + p_index / 2u);
	return (p_index & 1u) != 0u ? row.zw : row.xy;
}

// One record moves p_point (owner space). Weights come from the straight position p_rest; joints apply from the tip.
vec2 deform_apply_record(uint p_offset, vec2 p_point, vec2 p_rest) {
	vec4 counts = deform_row(p_offset + 1u);
	vec4 warp_info = deform_row(p_offset + 2u);
	uint joints = uint(counts.z);
	uint warp_columns = uint(counts.w);
	uint warp_rows = uint(warp_info.x);
	vec2 point = p_point;
	if (warp_columns >= 2u && warp_rows >= 2u) {
		vec4 rect = deform_row(p_offset + 3u);
		vec2 unit = (p_rest - rect.xy) / rect.zw;
		// Grid points on the rectangle's edges count as inside, so rounding cannot drop the edge points.
		float inside = (all(greaterThanEqual(unit, vec2(-1.0e-4))) && all(lessThanEqual(unit, vec2(1.0 + 1.0e-4)))) ? 1.0 : 0.0;
		vec2 cell = clamp(unit, vec2(0.0), vec2(1.0)) * vec2(float(warp_columns - 1u), float(warp_rows - 1u));
		uvec2 corner = min(uvec2(cell), uvec2(warp_columns - 2u, warp_rows - 2u));
		vec2 fraction = cell - vec2(corner);
		uint base = p_offset + 4u + joints * 2u;
		uint index = corner.y * warp_columns + corner.x;
		vec2 top = mix(deform_warp_point(base, index), deform_warp_point(base, index + 1u), fraction.x);
		vec2 bottom = mix(deform_warp_point(base, index + warp_columns), deform_warp_point(base, index + warp_columns + 1u), fraction.x);
		point += inside * warp_info.y * mix(top, bottom, fraction.y);
	}
	for (int joint = int(joints) - 1; joint >= 0; joint--) {
		vec4 a = deform_row(p_offset + 4u + uint(joint) * 2u);
		vec4 b = deform_row(p_offset + 5u + uint(joint) * 2u);
		vec2 pivot = a.xy;
		vec2 axis = a.zw;
		float along = dot(p_rest - pivot, axis);
		float blend = max(b.y, 0.001);
		float weight = smoothstep(-blend, blend, along);
		if (b.z != 1.0) {
			float segment = b.w > 0.0 ? b.w : 1.0e9;
			point += axis * (clamp(along, 0.0, segment) * (b.z - 1.0));
		}
		float angle = b.x * weight;
		float c = cos(angle);
		float s = sin(angle);
		vec2 d = point - pivot;
		point = pivot + vec2(c * d.x - s * d.y, s * d.x + c * d.y);
	}
	return point;
}

// True when the grid's long lines run along the picture's x: the turning joints that cross the rectangle [p_lo, p_hi]
// with an axis along x turn by more in total than those with an axis along y.
bool deform_bands_across(uint p_chain, uint p_record_base, vec2 p_lo, vec2 p_hi, mat2 p_item_basis, vec2 p_item_ofs) {
	uvec4 records = read_mask_row(deform_texel_base + p_chain) & uvec4(DEFORM_RECORD_MASK);
	mat2 to_item_basis = inverse(p_item_basis);
	float turn_x = 0.0;
	float turn_y = 0.0;
	for (uint level = 0u; level < 4u; level++) {
		if (records[level] == 0u) {
			break;
		}
		uint offset = p_record_base + records[level] - 1u;
		vec4 basis = deform_row(offset);
		vec4 counts = deform_row(offset + 1u);
		mat2 to_item = to_item_basis * mat2(basis.xy, basis.zw);
		vec2 to_item_ofs = to_item_basis * (counts.xy - p_item_ofs);
		uint joints = min(uint(counts.z), DEFORM_BAND_JOINTS);
		for (uint joint = 0u; joint < joints; joint++) {
			vec4 b = deform_row(offset + 5u + joint * 2u);
			if (b.x == 0.0) {
				continue;
			}
			vec4 a = deform_row(offset + 4u + joint * 2u);
			vec2 axis = to_item * a.zw;
			vec2 pivot = to_item * a.xy + to_item_ofs;
			float axis_length = length(axis);
			float half_width = max(b.y, 0.5) * axis_length;
			if (abs(axis.x) >= 0.85 * axis_length && pivot.x + half_width > p_lo.x && pivot.x - half_width < p_hi.x) {
				turn_x += abs(b.x);
			} else if (abs(axis.y) >= 0.85 * axis_length && pivot.y + half_width > p_lo.y && pivot.y - half_width < p_hi.y) {
				turn_y += abs(b.x);
			}
		}
	}
	return turn_x > turn_y;
}

// The long grid lines gather in the turning joint bands of every record of the chain whose axis runs along them, at
// equal weight steps (so a fold turns by equal angles from line to line); the rest of the lines spread evenly. p_y is
// a place along the picture's y, or along its x when p_across is true.
#define DEFORM_ROW_DENSITY 24.0
float deform_band_row(float p_u, vec2 p_band, vec2 p_shape) {
	float h = max(p_shape.y, 1.0e-3);
	float w0 = smoothstep(0.0, 1.0, (p_band.x - p_shape.x + h) / (2.0 * h));
	float w1 = smoothstep(0.0, 1.0, (p_band.y - p_shape.x + h) / (2.0 * h));
	float w = mix(w0, w1, clamp((p_u - p_band.x) / max(p_band.y - p_band.x, 1.0e-6), 0.0, 1.0));
	float t = 0.5 - sin(asin(clamp(1.0 - 2.0 * w, -1.0, 1.0)) / 3.0);
	return clamp(p_shape.x - h + 2.0 * h * t, p_band.x, p_band.y);
}

float deform_adapt_line(uint p_chain, uint p_record_base, float p_y, float p_y0, float p_y1, bool p_across, mat2 p_item_basis, vec2 p_item_ofs) {
	uvec4 records = read_mask_row(deform_texel_base + p_chain) & uvec4(DEFORM_RECORD_MASK);
	vec2 bands[DEFORM_BAND_JOINTS];
	vec2 shapes[DEFORM_BAND_JOINTS];
	uint band_count = 0u;
	mat2 to_item_basis = inverse(p_item_basis);
	for (uint level = 0u; level < 4u; level++) {
		if (records[level] == 0u) {
			break;
		}
		uint offset = p_record_base + records[level] - 1u;
		vec4 basis = deform_row(offset);
		vec4 counts = deform_row(offset + 1u);
		mat2 to_item = to_item_basis * mat2(basis.xy, basis.zw);
		vec2 to_item_ofs = to_item_basis * (counts.xy - p_item_ofs);
		uint joints = min(uint(counts.z), DEFORM_BAND_JOINTS);
		for (uint joint = 0u; joint < joints && band_count < DEFORM_BAND_JOINTS; joint++) {
			vec4 a = deform_row(offset + 4u + joint * 2u);
			vec4 b = deform_row(offset + 5u + joint * 2u);
			if (b.x == 0.0) {
				continue;
			}
			vec2 axis = to_item * a.zw;
			vec2 pivot = to_item * a.xy + to_item_ofs;
			if (p_across) {
				axis = axis.yx;
				pivot = pivot.yx;
			}
			float axis_length = length(axis);
			if (axis_length <= 0.0 || abs(axis.y) < 0.85 * axis_length) {
				continue;
			}
			float centre = pivot.y;
			float half_width = max(b.y, 0.5) * axis_length;
			vec2 band = vec2(max(centre - half_width - 1.0, p_y0), min(centre + half_width + 1.0, p_y1));
			if (band.y <= band.x) {
				continue;
			}
			uint at = band_count;
			while (at > 0u && bands[at - 1u].x > band.x) {
				bands[at] = bands[at - 1u];
				shapes[at] = shapes[at - 1u];
				at--;
			}
			bands[at] = band;
			shapes[at] = vec2(centre, half_width);
			band_count++;
		}
	}
	if (band_count == 0u) {
		return p_y;
	}
	if (band_count == 1u) {
		vec2 band = bands[0];
		float total = p_y1 - p_y0 + (DEFORM_ROW_DENSITY - 1.0) * (band.y - band.x);
		float target = (p_y - p_y0) / max(p_y1 - p_y0, 1.0e-6) * total;
		float gap = band.x - p_y0;
		if (gap >= target) {
			return p_y0 + target;
		}
		float dense = (band.y - band.x) * DEFORM_ROW_DENSITY;
		if (gap + dense >= target) {
			return deform_band_row(band.x + (target - gap) / DEFORM_ROW_DENSITY, band, shapes[0]);
		}
		return min(band.y + (target - (gap + dense)), p_y1);
	}
	float total = p_y1 - p_y0;
	float cursor = p_y0;
	for (uint i = 0u; i < band_count; i++) {
		vec2 band = vec2(max(bands[i].x, cursor), bands[i].y);
		if (band.y > band.x) {
			total += (DEFORM_ROW_DENSITY - 1.0) * (band.y - band.x);
			cursor = band.y;
		}
	}
	float target = (p_y - p_y0) / max(p_y1 - p_y0, 1.0e-6) * total;
	float accumulated = 0.0;
	cursor = p_y0;
	for (uint i = 0u; i < band_count; i++) {
		vec2 band = vec2(max(bands[i].x, cursor), bands[i].y);
		if (band.y <= band.x) {
			continue;
		}
		float gap = band.x - cursor;
		if (accumulated + gap >= target) {
			return cursor + (target - accumulated);
		}
		accumulated += gap;
		float dense = (band.y - band.x) * DEFORM_ROW_DENSITY;
		if (accumulated + dense >= target) {
			return deform_band_row(band.x + (target - accumulated) / DEFORM_ROW_DENSITY, band, shapes[i]);
		}
		accumulated += dense;
		cursor = band.y;
	}
	return min(cursor + (target - accumulated), p_y1);
}

// Weight of the grid lines from p_lo up to p_y: joint bands count DEFORM_ROW_DENSITY times. Bands are sorted and apart.
float deform_line_mass(float p_y, float p_lo, vec2 p_bands[DEFORM_BAND_JOINTS], uint p_band_count) {
	float mass = p_y - p_lo;
	for (uint i = 0u; i < p_band_count; i++) {
		mass += (DEFORM_ROW_DENSITY - 1.0) * max(min(p_y, p_bands[i].y) - p_bands[i].x, 0.0);
	}
	return mass;
}

// The place whose deform_line_mass() is p_mass.
float deform_line_at(float p_mass, float p_lo, vec2 p_bands[DEFORM_BAND_JOINTS], uint p_band_count) {
	float cursor = p_lo;
	float mass = 0.0;
	for (uint i = 0u; i < p_band_count; i++) {
		float gap = p_bands[i].x - cursor;
		if (mass + gap >= p_mass) {
			return cursor + (p_mass - mass);
		}
		mass += gap;
		float dense = (p_bands[i].y - p_bands[i].x) * DEFORM_ROW_DENSITY;
		if (mass + dense >= p_mass) {
			return p_bands[i].x + (p_mass - mass) / DEFORM_ROW_DENSITY;
		}
		mass += dense;
		cursor = p_bands[i].y;
	}
	return cursor + (p_mass - mass);
}

// Warp grids: place of grid line p_index of p_count along one item axis (0 x, 1 y) of the rectangle [p_lo, p_hi].
// Every grid line of the innermost warp of the chain that crosses the rectangle on this axis is a line of the grid, so
// every warp point moves the picture; the other lines spread over the gaps, denser in the joint bands across this axis.
// A warp turned against the item has no lines along its axes; its grid lines then only spread.
float deform_warp_line(uint p_chain, uint p_record_base, uint p_axis, uint p_index, uint p_count, float p_lo, float p_hi, mat2 p_item_basis, vec2 p_item_ofs) {
	uvec4 records = read_mask_row(deform_texel_base + p_chain) & uvec4(DEFORM_RECORD_MASK);
	mat2 to_item_basis = inverse(p_item_basis);
	vec2 bands[DEFORM_BAND_JOINTS];
	uint band_count = 0u;
	float line_first = 0.0;
	float line_step = 0.0;
	uint line_count = 0u;
	for (uint level = 0u; level < 4u; level++) {
		if (records[level] == 0u) {
			break;
		}
		uint offset = p_record_base + records[level] - 1u;
		vec4 basis = deform_row(offset);
		vec4 counts = deform_row(offset + 1u);
		mat2 to_item = to_item_basis * mat2(basis.xy, basis.zw);
		vec2 to_item_ofs = to_item_basis * (counts.xy - p_item_ofs);
		uint joints = min(uint(counts.z), DEFORM_BAND_JOINTS);
		for (uint joint = 0u; joint < joints && band_count < DEFORM_BAND_JOINTS; joint++) {
			vec4 a = deform_row(offset + 4u + joint * 2u);
			vec4 b = deform_row(offset + 5u + joint * 2u);
			if (b.x == 0.0) {
				continue;
			}
			vec2 axis = to_item * a.zw;
			float axis_length = length(axis);
			if (axis_length <= 0.0 || abs(axis[p_axis]) < 0.85 * axis_length) {
				continue;
			}
			vec2 pivot = to_item * a.xy + to_item_ofs;
			float centre = pivot[p_axis];
			float half_width = max(b.y, 0.5) * axis_length;
			vec2 band = vec2(max(centre - half_width - 1.0, p_lo), min(centre + half_width + 1.0, p_hi));
			if (band.y <= band.x) {
				continue;
			}
			uint at = band_count;
			while (at > 0u && bands[at - 1u].x > band.x) {
				bands[at] = bands[at - 1u];
				at--;
			}
			bands[at] = band;
			band_count++;
		}
		uint warp_columns = uint(counts.w);
		uint warp_rows = uint(deform_row(offset + 2u).x);
		if (line_count != 0u || warp_columns < 2u || warp_rows < 2u) {
			continue;
		}
		// The owner axis that runs along this item axis: its warp lines are lines across this axis.
		uint other = 1u - p_axis;
		vec4 rect = deform_row(offset + 3u);
		vec2 rect_start = rect.xy;
		vec2 rect_size = rect.zw;
		uint owner_axis = 2u;
		if (abs(to_item[0][other]) <= 1.0e-3 * abs(to_item[0][p_axis]) && abs(to_item[1][p_axis]) <= 1.0e-3 * abs(to_item[1][other])) {
			owner_axis = 0u;
		} else if (abs(to_item[1][other]) <= 1.0e-3 * abs(to_item[1][p_axis]) && abs(to_item[0][p_axis]) <= 1.0e-3 * abs(to_item[0][other])) {
			owner_axis = 1u;
		}
		if (owner_axis == 2u) {
			continue;
		}
		uint points = owner_axis == 0u ? warp_columns : warp_rows;
		float scale = to_item[owner_axis][p_axis];
		float first = scale * rect_start[owner_axis] + to_item_ofs[p_axis];
		float step = scale * rect_size[owner_axis] / float(points - 1u);
		if (step < 0.0) {
			first += step * float(points - 1u);
			step = -step;
		}
		if (step <= 0.0 || first > p_hi || first + step * float(points - 1u) < p_lo) {
			continue;
		}
		line_first = first;
		line_step = step;
		line_count = points;
	}
	// Overlapping bands become one.
	uint merged = 0u;
	for (uint i = 0u; i < band_count; i++) {
		if (merged > 0u && bands[i].x <= bands[merged - 1u].y) {
			bands[merged - 1u].y = max(bands[merged - 1u].y, bands[i].y);
		} else {
			bands[merged] = bands[i];
			merged++;
		}
	}
	// Warp lines strictly inside the rectangle split it into intervals; each interval gets one grid line, and the
	// spare lines go to the intervals by their weight.
	uint first_line = 0u;
	uint inner = 0u;
	if (line_count != 0u) {
		float margin = 1.0e-3 * line_step;
		float lowest = max(floor((p_lo + margin - line_first) / line_step) + 1.0, 0.0);
		float highest = min(ceil((p_hi - margin - line_first) / line_step) - 1.0, float(line_count - 1u));
		if (highest >= lowest) {
			first_line = uint(lowest);
			inner = uint(highest - lowest) + 1u;
		}
	}
	if (inner + 1u > p_count) {
		inner = 0u;
	}
	uint intervals = inner + 1u;
	float total = max(deform_line_mass(p_hi, p_lo, bands, merged), 1.0e-6);
	float spare = float(p_count - intervals);
	uint start = 0u;
	float lo = p_lo;
	float lo_mass = 0.0;
	for (uint interval = 0u; interval < intervals; interval++) {
		float hi = interval + 1u < intervals ? line_first + float(first_line + interval) * line_step : p_hi;
		float hi_mass = deform_line_mass(hi, p_lo, bands, merged);
		uint lines = 1u + uint(floor(spare * (hi_mass - lo_mass) / total));
		if (p_index < start + lines) {
			uint sub = p_index - start;
			if (sub == 0u) {
				return lo;
			}
			return deform_line_at(mix(lo_mass, hi_mass, float(sub) / float(lines)), p_lo, bands, merged);
		}
		start += lines;
		lo = hi;
		lo_mass = hi_mass;
	}
	return p_hi;
}

// Moves a rectangle vertex (item space) by the records of its chain, innermost first, in canvas space. r_moved[n] is
// the canvas-space place after the first n records, or after all of them when the chain is shorter.
vec2 deform_vertex(uint p_chain, uint p_record_base, vec2 p_vertex, vec2 p_world_x, vec2 p_world_y, vec2 p_world_ofs, out vec2 r_moved[4]) {
	uvec4 records = read_mask_row(deform_texel_base + p_chain) & uvec4(DEFORM_RECORD_MASK);
	mat2 item_basis = mat2(p_world_x, p_world_y);
	vec2 world = item_basis * p_vertex + p_world_ofs;
	vec2 rest_world = world;
	for (uint level = 0u; level < 4u; level++) {
		r_moved[level] = world;
		uint record = records[level];
		if (record == 0u) {
			continue;
		}
		uint offset = p_record_base + record - 1u;
		vec4 basis = deform_row(offset);
		vec2 origin = deform_row(offset + 1u).xy;
		mat2 owner_basis = mat2(basis.xy, basis.zw);
		mat2 to_owner = inverse(owner_basis);
		world = owner_basis * deform_apply_record(offset, to_owner * (world - origin), to_owner * (rest_world - origin)) + origin;
	}
	return inverse(item_basis) * (world - p_world_ofs);
}
#endif

void main() {
	varying_A = vec4(read_draw_data_world_x, read_draw_data_world_y);
	varying_B = read_draw_data_color_texture_pixel_size;
#ifndef USE_PRIMITIVE
	varying_C = read_draw_data_ninepatch_margins;

#ifndef USE_ATTRIBUTES
#ifdef USE_NINEPATCH
	varying_D = vec2(read_draw_data_dst_rect.z, read_draw_data_dst_rect.w);
#endif // USE_NINEPATCH
	varying_E = read_draw_data_src_rect;
#endif // !USE_ATTRIBUTES
#endif // USE_PRIMITIVE

	varying_F = uvec4(read_draw_data_flags, read_draw_data_instance_offset, 0u, 0u);
	varying_G = read_draw_data_lights;
#if defined(USE_MASKS) || defined(USE_PAGE_RECT)
	varying_F.z = read_draw_data_item_data;
#endif

	vec4 instance_custom = vec4(0.0);

#if defined(CUSTOM0_USED)
	vec4 custom0 = vec4(0.0);
#endif
#if defined(CUSTOM1_USED)
	vec4 custom1 = vec4(0.0);
#endif

#ifdef USE_PRIMITIVE
	vec2 vertex;
	vec2 uv;
	vec4 color;

	if (gl_VertexID % 3 == 0) {
		vertex = read_draw_data_point_a;
		uv = read_draw_data_uv_a;
		color.xy = unpackHalf2x16(read_draw_data_color_a_rg);
		color.zw = unpackHalf2x16(read_draw_data_color_a_ba);
	} else if (gl_VertexID % 3 == 1) {
		vertex = read_draw_data_point_b;
		uv = read_draw_data_uv_b;
		color.xy = unpackHalf2x16(read_draw_data_color_b_rg);
		color.zw = unpackHalf2x16(read_draw_data_color_b_ba);
	} else {
		vertex = read_draw_data_point_c;
		uv = read_draw_data_uv_c;
		color.xy = unpackHalf2x16(read_draw_data_color_c_rg);
		color.zw = unpackHalf2x16(read_draw_data_color_c_ba);
	}

#elif defined(USE_ATTRIBUTES)
	vec2 vertex = vertex_attrib;
	vec4 color = color_attrib * read_draw_data_modulation;
	vec2 uv = uv_attrib;

#ifdef USE_INSTANCING
	if (bool(batch_flags & BATCH_FLAGS_INSTANCING_HAS_COLORS)) {
		vec4 instance_color;
		instance_color.xy = unpackHalf2x16(uint(instance_color_custom_data.x));
		instance_color.zw = unpackHalf2x16(uint(instance_color_custom_data.y));
		color *= instance_color;
	}
	if (bool(batch_flags & BATCH_FLAGS_INSTANCING_HAS_CUSTOM_DATA)) {
		instance_custom.xy = unpackHalf2x16(instance_color_custom_data.z);
		instance_custom.zw = unpackHalf2x16(instance_color_custom_data.w);
	}
#endif // !USE_INSTANCING

#else // !USE_ATTRIBUTES

	// crash on Adreno 320/330
	//vec2 vertex_base_arr[6] = vec2[](vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0), vec2(1.0, 1.0));
	//vec2 vertex_base = vertex_base_arr[gl_VertexID % 6];
	//-----------------------------------------
	// ID |  0  |  1  |  2  |  3  |  4  |  5  |
	//-----------------------------------------
	// X  | 0.0 | 0.0 | 1.0 | 1.0 | 0.0 | 1.0 |
	// Y  | 0.0 | 1.0 | 1.0 | 0.0 | 0.0 | 1.0 |
	//-----------------------------------------
	// no crash or freeze on all Adreno 3xx	with 'if / else if' and slightly faster!
	int vertex_id = gl_VertexID % 6;
	vec2 vertex_base;
	if (vertex_id == 0) {
		vertex_base = vec2(0.0, 0.0);
	} else if (vertex_id == 1) {
		vertex_base = vec2(0.0, 1.0);
	} else if (vertex_id == 2) {
		vertex_base = vec2(1.0, 1.0);
	} else if (vertex_id == 3) {
		vertex_base = vec2(1.0, 0.0);
	} else if (vertex_id == 4) {
		vertex_base = vec2(0.0, 0.0);
	} else if (vertex_id == 5) {
		vertex_base = vec2(1.0, 1.0);
	}
#ifdef DEFORM_RECT
	uint deform_word = read_draw_data_deform;
	bool warp_grid = bool(batch_flags & BATCH_FLAGS_DEFORM_WARP);
	uvec4 grid = uvec4(0u);
	if (bool(batch_flags & BATCH_FLAGS_DEFORM_GRID)) {
		// grid: column, row, columns, rows.
		grid.z = warp_grid ? DEFORM_WARP_GRID_COLUMNS : DEFORM_GRID_COLUMNS;
		grid.w = warp_grid ? DEFORM_WARP_GRID_ROWS : (bool(batch_flags & BATCH_FLAGS_DEFORM_DENSE) ? DEFORM_DENSE_GRID_ROWS : DEFORM_GRID_ROWS);
		grid.x = uint(gl_VertexID) % (grid.z + 1u);
		grid.y = uint(gl_VertexID) / (grid.z + 1u);

		vertex_base = vec2(grid.xy) / vec2(grid.zw);
	}
#endif

	vec2 uv = read_draw_data_src_rect.xy + abs(read_draw_data_src_rect.zw) * ((read_draw_data_flags & INSTANCE_FLAGS_TRANSPOSE_RECT) != uint(0) ? vertex_base.yx : vertex_base.xy);
	vec4 color = read_draw_data_modulation;
	vec2 vertex = read_draw_data_dst_rect.xy + abs(read_draw_data_dst_rect.zw) * mix(vertex_base, vec2(1.0, 1.0) - vertex_base, lessThan(read_draw_data_src_rect.zw, vec2(0.0, 0.0)));
#ifdef DEFORM_RECT
	uint deform_chain = deform_word & DEFORM_CHAIN_MASK;
	vec2 deform_moved[4];
	if (deform_chain != 0u) {
		uint record_base = deform_texel_base + read_mask_row(deform_texel_base).x;
		vec2 lo = read_draw_data_dst_rect.xy;
		vec2 hi = lo + abs(read_draw_data_dst_rect.zw);
		mat2 item_basis = mat2(read_draw_data_world_x, read_draw_data_world_y);
		bool joint_grid = !warp_grid && bool(batch_flags & BATCH_FLAGS_DEFORM_GRID);
		if (warp_grid) {
			vertex.x = deform_warp_line(deform_chain, record_base, 0u, grid.x, grid.z, lo.x, hi.x, item_basis, read_draw_data_world_ofs);
			vertex.y = deform_warp_line(deform_chain, record_base, 1u, grid.y, grid.w, lo.y, hi.y, item_basis, read_draw_data_world_ofs);
		} else if (joint_grid) {
			// The long lines of the grid run along the picture's x when the turning joints run that way, else along y.
			if (deform_bands_across(deform_chain, record_base, lo, hi, item_basis, read_draw_data_world_ofs)) {
				vertex = lo + (hi - lo) * mix(vertex_base.yx, vec2(1.0) - vertex_base.yx, lessThan(read_draw_data_src_rect.zw, vec2(0.0)));
				vertex.x = deform_adapt_line(deform_chain, record_base, vertex.x, lo.x, hi.x, true, item_basis, read_draw_data_world_ofs);
			} else {
				vertex.y = deform_adapt_line(deform_chain, record_base, vertex.y, lo.y, hi.y, false, item_basis, read_draw_data_world_ofs);
			}
		}
		if (warp_grid || joint_grid) {
			vec2 unit = (vertex - lo) / max(hi - lo, vec2(1.0e-6));
			vertex_base = mix(unit, vec2(1.0) - unit, lessThan(read_draw_data_src_rect.zw, vec2(0.0)));
			uv = read_draw_data_src_rect.xy + abs(read_draw_data_src_rect.zw) * ((read_draw_data_flags & INSTANCE_FLAGS_TRANSPOSE_RECT) != uint(0) ? vertex_base.yx : vertex_base.xy);
		}
		vertex = deform_vertex(deform_chain, record_base, vertex, read_draw_data_world_x, read_draw_data_world_y, read_draw_data_world_ofs, deform_moved);
	}
#endif

#endif // USE_ATTRIBUTES

#if defined(CUSTOM0_USED)
	custom0 = custom0_attrib;
#endif

#if defined(CUSTOM1_USED)
	custom1 = custom1_attrib;
#endif

	mat4 model_matrix = mat4(vec4(read_draw_data_world_x, 0.0, 0.0), vec4(read_draw_data_world_y, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(read_draw_data_world_ofs, 0.0, 1.0));

#ifdef USE_INSTANCING
	model_matrix = model_matrix * transpose(mat4(instance_xform0, instance_xform1, vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0)));
#endif // USE_INSTANCING

	vec2 color_texture_pixel_size = read_draw_data_color_texture_pixel_size;

#ifdef USE_POINT_SIZE
	float point_size = 1.0;
#endif

#ifdef USE_WORLD_VERTEX_COORDS
	vertex = (model_matrix * vec4(vertex, 0.0, 1.0)).xy;
#endif
	{
#CODE : VERTEX
	}

#ifdef USE_NINEPATCH
	pixel_size_interp = abs(read_draw_data_dst_rect.zw) * vertex_base;
#endif

#if !defined(SKIP_TRANSFORM_USED) && !defined(USE_WORLD_VERTEX_COORDS)
	vertex = (model_matrix * vec4(vertex, 0.0, 1.0)).xy;
#endif

	color_interp = color;

	vertex = (canvas_transform * vec4(vertex, 0.0, 1.0)).xy;

	if (use_pixel_snap) {
		vertex = floor(vertex + 0.5);
		// precision issue on some hardware creates artifacts within texture
		// offset uv by a small amount to avoid
		uv += 1e-5;
	}

	vertex_interp = vertex;
	uv_interp = uv;

#ifdef USE_MASKS
	vec2 mask_vertex = vertex;
#ifdef DEFORM_RECT
	// Follow code of each mask slot, three bits each: 0 stays, 1 + n samples where the first n records moved the point.
	uint follow_codes = 0u;
	vec2 follow_places[4] = vec2[](vertex, vertex, vertex, vertex);
	if (deform_chain != 0u) {
		uvec4 code_bytes = read_mask_row(deform_texel_base + deform_chain) >> DEFORM_FOLLOW_CODE_SHIFT;
		follow_codes = code_bytes.x | (code_bytes.y << 6u) | (code_bytes.z << 12u) | (code_bytes.w << 18u);
	}
	if (follow_codes != 0u) {
		for (uint level = 0u; level < 4u; level++) {
			vec2 place = (canvas_transform * vec4(deform_moved[level], 0.0, 1.0)).xy;
			follow_places[level] = use_pixel_snap ? floor(place + 0.5) : place;
		}
		uint code_0 = follow_codes & 7u;
		mask_vertex = code_0 != 0u ? follow_places[code_0 - 1u] : vertex;
	}
	deform_rest_interp = vec4(follow_places[0], follow_places[1]);
	deform_rest_far_interp = vec4(follow_places[2], follow_places[3]);
	varying_F.w = follow_codes;
#endif
	mask_uv_interp = vec2(0.0);
	mask_region_interp = vec4(0.0);
	mask_extra_interp = vec4(0.0, 0.0, 0.0, -1.0);
	uint mask_set = read_draw_data_item_data >> 16u;
	if (mask_set > 1u) {
		uvec4 indices = read_mask_row(mask_set);
		if ((indices.x >> 16u) == 0u) {
			uint base = mask_entries_base + ((indices.x & 0xFFFFu) - 1u) * 4u;
			vec4 row_x = uintBitsToFloat(read_mask_row(base));
			vec4 row_y = uintBitsToFloat(read_mask_row(base + 1u));
			vec4 extra = uintBitsToFloat(read_mask_row(base + 3u));
			mask_uv_interp = vec2(dot(row_x.xyz, vec3(mask_vertex, 1.0)), dot(row_y.xyz, vec3(mask_vertex, 1.0)));
			mask_region_interp = uintBitsToFloat(read_mask_row(base + 2u));
			mask_extra_interp = vec4(min(extra.yz * exp2(ceil(mask_mipmaps ? row_y.w : 0.0)), vec2(0.5)), row_y.w, extra.x * 4.0 + row_x.w * 2.0 + extra.w);
		}
	}
#endif

	gl_Position = screen_transform * vec4(vertex, 0.0, 1.0);

#ifdef USE_POINT_SIZE
	gl_PointSize = point_size;
#endif
}

#[fragment]

#include "canvas_uniforms_inc.glsl"
#include "stdlib_inc.glsl"

in vec2 uv_interp;
in vec2 vertex_interp;
in vec4 color_interp;

#ifdef USE_MASKS
in highp vec2 mask_uv_interp;
flat in highp vec4 mask_region_interp;
flat in highp vec4 mask_extra_interp;
#if defined(USE_DEFORM) && !defined(USE_PRIMITIVE) && !defined(USE_ATTRIBUTES) && !defined(USE_NINEPATCH)
in highp vec4 deform_rest_interp;
in highp vec4 deform_rest_far_interp;
#endif
#endif

#if defined(USE_DEFORM) && !defined(USE_PRIMITIVE) && !defined(USE_ATTRIBUTES) && !defined(USE_NINEPATCH)
#define DEFORM_RECT
#endif

#ifdef USE_NINEPATCH

in vec2 pixel_size_interp;

#endif

// Can all be flat as they are the same for the whole batched instance
flat in vec4 varying_A;
flat in vec2 varying_B;
#define read_draw_data_world_x varying_A.xy
#define read_draw_data_world_y varying_A.zw
#define read_draw_data_color_texture_pixel_size varying_B

#ifndef USE_PRIMITIVE
flat in vec4 varying_C;
#define read_draw_data_ninepatch_margins varying_C

#ifndef USE_ATTRIBUTES
#ifdef USE_NINEPATCH

flat in vec2 varying_D;
#define read_draw_data_dst_rect_z varying_D.x
#define read_draw_data_dst_rect_w varying_D.y
#endif

flat in vec4 varying_E;
#define read_draw_data_src_rect varying_E
#endif // USE_ATTRIBUTES
#endif // USE_PRIMITIVE

flat in uvec4 varying_F;
flat in uvec4 varying_G;
#if defined(USE_MASKS) || defined(USE_PAGE_RECT)
#define read_draw_data_item_data varying_F.z
#define read_draw_data_array_layer (read_draw_data_item_data & 0x7FFu)
#define read_draw_data_max_region_lod ((read_draw_data_item_data >> 11u) & 0x1Fu)
#endif
#define read_draw_data_flags varying_F.x
#define read_draw_data_instance_offset varying_F.y
#define read_draw_data_lights varying_G

#ifndef DISABLE_LIGHTING
uniform sampler2D atlas_texture; //texunit:-2
uniform sampler2D shadow_atlas_texture; //texunit:-3
#endif // DISABLE_LIGHTING
uniform sampler2D color_buffer; //texunit:-4
uniform sampler2D sdf_texture; //texunit:-5
uniform sampler2D normal_texture; //texunit:-6
uniform sampler2D specular_texture; //texunit:-7

uniform sampler2D color_texture; //texunit:0
uniform sampler2DArray color_texture_array; //texunit:-1

uniform mediump uint batch_flags;
uniform highp uint specular_shininess_in;
uniform highp float region_mipmap_bias;

layout(location = 0) out vec4 frag_color;

/* clang-format off */
// This needs to be outside clang-format so the ubo comment is in the right place
#ifdef MATERIAL_UNIFORMS_USED
layout(std140) uniform MaterialUniforms{ //ubo:4

#MATERIAL_UNIFORMS

};
#endif
/* clang-format on */

#GLOBALS


float vec4_to_float(vec4 p_vec) {
	return dot(p_vec, vec4(1.0 / (255.0 * 255.0 * 255.0), 1.0 / (255.0 * 255.0), 1.0 / 255.0, 1.0)) * 2.0 - 1.0;
}

vec2 screen_uv_to_sdf(vec2 p_uv) {
	return screen_to_sdf * p_uv;
}

float texture_sdf(vec2 p_sdf) {
	vec2 uv = p_sdf * sdf_to_tex.xy + sdf_to_tex.zw;
	float d = vec4_to_float(texture(sdf_texture, uv));
	d *= SDF_MAX_LENGTH;
	return d * tex_to_sdf;
}

vec2 texture_sdf_normal(vec2 p_sdf) {
	vec2 uv = p_sdf * sdf_to_tex.xy + sdf_to_tex.zw;

	const float EPSILON = 0.001;
	return normalize(vec2(
			vec4_to_float(texture(sdf_texture, uv + vec2(EPSILON, 0.0))) - vec4_to_float(texture(sdf_texture, uv - vec2(EPSILON, 0.0))),
			vec4_to_float(texture(sdf_texture, uv + vec2(0.0, EPSILON))) - vec4_to_float(texture(sdf_texture, uv - vec2(0.0, EPSILON)))));
}

vec2 sdf_to_screen_uv(vec2 p_sdf) {
	return p_sdf * sdf_to_screen;
}

#ifndef DISABLE_LIGHTING
#ifdef LIGHT_CODE_USED

// Declared before the light code, which may read the picture's page too.
vec4 texture_page(highp vec2 p_uv);
vec4 texture_page_lod(highp vec2 p_uv, highp float p_lod);

vec4 light_compute(
		vec3 light_vertex,
		vec3 light_position,
		vec3 normal,
		vec4 light_color,
		float light_energy,
		vec4 specular_shininess,
		inout vec4 shadow_modulate,
		vec2 screen_uv,
		vec2 uv,
		vec4 color, bool is_directional) {
	vec4 light = vec4(0.0);
	vec3 light_direction = vec3(0.0);

	if (is_directional) {
		light_direction = normalize(mix(vec3(light_position.xy, 0.0), vec3(0, 0, 1), light_position.z));
		light_position = vec3(0.0);
	} else {
		light_direction = normalize(light_position - light_vertex);
	}

#CODE : LIGHT

	return light;
}

#endif

vec3 light_normal_compute(vec3 light_vec, vec3 normal, vec3 base_color, vec3 light_color, vec4 specular_shininess, bool specular_shininess_used) {
	float cNdotL = max(0.0, dot(normal, light_vec));

	if (specular_shininess_used) {
		//blinn
		vec3 view = vec3(0.0, 0.0, 1.0); // not great but good enough
		vec3 half_vec = normalize(view + light_vec);

		float cNdotV = max(dot(normal, view), 0.0);
		float cNdotH = max(dot(normal, half_vec), 0.0);
		float cVdotH = max(dot(view, half_vec), 0.0);
		float cLdotH = max(dot(light_vec, half_vec), 0.0);
		float shininess = exp2(15.0 * specular_shininess.a + 1.0) * 0.25;
		float blinn = pow(cNdotH, shininess);
		blinn *= (shininess + 8.0) * (1.0 / (8.0 * M_PI));
		float s = (blinn) / max(4.0 * cNdotV * cNdotL, 0.75);

		return specular_shininess.rgb * light_color * s + light_color * base_color * cNdotL;
	} else {
		return light_color * base_color * cNdotL;
	}
}

#ifdef USE_RGBA_SHADOWS

#define SHADOW_DEPTH(m_uv) (dot(textureLod(shadow_atlas_texture, (m_uv), 0.0), vec4(1.0 / (255.0 * 255.0 * 255.0), 1.0 / (255.0 * 255.0), 1.0 / 255.0, 1.0)) * 2.0 - 1.0)

#else

#define SHADOW_DEPTH(m_uv) (textureLod(shadow_atlas_texture, (m_uv), 0.0).r)

#endif

/* clang-format off */
#define SHADOW_TEST(m_uv) { highp float sd = SHADOW_DEPTH(m_uv); shadow += step(sd, shadow_uv.z / shadow_uv.w); }
/* clang-format on */

//float distance = length(shadow_pos);
vec4 light_shadow_compute(uint light_base, vec4 light_color, vec4 shadow_uv
#ifdef LIGHT_CODE_USED
		,
		vec3 shadow_modulate
#endif
) {
	float shadow = 0.0;
	uint shadow_mode = light_array[light_base].flags & LIGHT_FLAGS_FILTER_MASK;

	if (shadow_mode == LIGHT_FLAGS_SHADOW_NEAREST) {
		SHADOW_TEST(shadow_uv.xy);
	} else if (shadow_mode == LIGHT_FLAGS_SHADOW_PCF5) {
		vec2 shadow_pixel_size = vec2(light_array[light_base].shadow_pixel_size, 0.0);
		SHADOW_TEST(shadow_uv.xy - shadow_pixel_size * 2.0);
		SHADOW_TEST(shadow_uv.xy - shadow_pixel_size);
		SHADOW_TEST(shadow_uv.xy);
		SHADOW_TEST(shadow_uv.xy + shadow_pixel_size);
		SHADOW_TEST(shadow_uv.xy + shadow_pixel_size * 2.0);
		shadow /= 5.0;
	} else { //PCF13
		vec2 shadow_pixel_size = vec2(light_array[light_base].shadow_pixel_size, 0.0);
		SHADOW_TEST(shadow_uv.xy - shadow_pixel_size * 6.0);
		SHADOW_TEST(shadow_uv.xy - shadow_pixel_size * 5.0);
		SHADOW_TEST(shadow_uv.xy - shadow_pixel_size * 4.0);
		SHADOW_TEST(shadow_uv.xy - shadow_pixel_size * 3.0);
		SHADOW_TEST(shadow_uv.xy - shadow_pixel_size * 2.0);
		SHADOW_TEST(shadow_uv.xy - shadow_pixel_size);
		SHADOW_TEST(shadow_uv.xy);
		SHADOW_TEST(shadow_uv.xy + shadow_pixel_size);
		SHADOW_TEST(shadow_uv.xy + shadow_pixel_size * 2.0);
		SHADOW_TEST(shadow_uv.xy + shadow_pixel_size * 3.0);
		SHADOW_TEST(shadow_uv.xy + shadow_pixel_size * 4.0);
		SHADOW_TEST(shadow_uv.xy + shadow_pixel_size * 5.0);
		SHADOW_TEST(shadow_uv.xy + shadow_pixel_size * 6.0);
		shadow /= 13.0;
	}

	vec4 shadow_color = godot_unpackUnorm4x8(light_array[light_base].shadow_color);
#ifdef LIGHT_CODE_USED
	shadow_color.rgb *= shadow_modulate;
#endif

	shadow_color.a *= light_color.a; //respect light alpha

	return mix(light_color, shadow_color, shadow);
}

void light_blend_compute(uint light_base, vec4 light_color, inout vec3 color) {
	uint blend_mode = light_array[light_base].flags & LIGHT_FLAGS_BLEND_MASK;

	if (blend_mode == LIGHT_FLAGS_BLEND_MODE_ADD) {
		color.rgb += light_color.rgb * light_color.a;
	} else if (blend_mode == LIGHT_FLAGS_BLEND_MODE_SUB) {
		color.rgb -= light_color.rgb * light_color.a;
	} else if (blend_mode == LIGHT_FLAGS_BLEND_MODE_MIX) {
		color.rgb = mix(color.rgb, light_color.rgb, light_color.a);
	}
}

#endif

#ifdef USE_NINEPATCH

float map_ninepatch_axis(float pixel, float draw_size, float tex_pixel_size, float margin_begin, float margin_end, int np_repeat, inout int draw_center) {
	float tex_size = 1.0 / tex_pixel_size;

	if (pixel < margin_begin) {
		return pixel * tex_pixel_size;
	} else if (pixel >= draw_size - margin_end) {
		return (tex_size - (draw_size - pixel)) * tex_pixel_size;
	} else {
		if (!bool(read_draw_data_flags & INSTANCE_FLAGS_NINEPATCH_DRAW_CENTER)) {
			draw_center--;
		}

		// np_repeat is passed as uniform using NinePatchRect::AxisStretchMode enum.
		if (np_repeat == 0) { // Stretch.
			// Convert to ratio.
			float ratio = (pixel - margin_begin) / (draw_size - margin_begin - margin_end);
			// Scale to source texture.
			return (margin_begin + ratio * (tex_size - margin_begin - margin_end)) * tex_pixel_size;
		} else if (np_repeat == 1) { // Tile.
			// Convert to offset.
			float ofs = mod((pixel - margin_begin), tex_size - margin_begin - margin_end);
			// Scale to source texture.
			return (margin_begin + ofs) * tex_pixel_size;
		} else if (np_repeat == 2) { // Tile Fit.
			// Calculate scale.
			float src_area = draw_size - margin_begin - margin_end;
			float dst_area = tex_size - margin_begin - margin_end;
			float scale = max(1.0, floor(src_area / max(dst_area, 0.0000001) + 0.5));
			// Convert to ratio.
			float ratio = (pixel - margin_begin) / src_area;
			ratio = mod(ratio * scale, 1.0);
			// Scale to source texture.
			return (margin_begin + ratio * dst_area) * tex_pixel_size;
		} else { // Shouldn't happen, but silences compiler warning.
			return 0.0;
		}
	}
}

#endif

float msdf_median(float r, float g, float b) {
	return max(min(r, g), min(max(r, g), b));
}

highp vec2 page_texture_size() {
#ifdef USE_TEXTURE_ARRAY
	return vec2(textureSize(color_texture_array, 0).xy);
#else
	return vec2(textureSize(color_texture, 0));
#endif
}

vec4 sample_page_texture_lod(highp vec2 p_uv, highp uint p_array_layer, highp float p_lod) {
#ifdef USE_TEXTURE_ARRAY
	return textureLod(color_texture_array, vec3(p_uv, float(p_array_layer)), p_lod);
#else
	return textureLod(color_texture, p_uv, p_lod);
#endif
}

vec4 sample_isolated_page_2d(highp vec2 p_uv_unclamped, highp vec4 p_sampler_domain, highp uint p_array_layer, highp uint p_max_region_lod) {
	highp vec2 page_size = page_texture_size();
	highp vec2 gradient_x = dFdx(p_uv_unclamped) * page_size;
	highp vec2 gradient_y = dFdy(p_uv_unclamped) * page_size;
	highp float lambda = 0.5 * log2(max(max(dot(gradient_x, gradient_x), dot(gradient_y, gradient_y)), 1e-8)) + region_mipmap_bias;
	highp float lod = clamp(lambda, 0.0, float(p_max_region_lod));

#ifdef REGION_MIP_NEAREST
	highp float level = floor(lod + 0.5);
	highp vec2 half_texel = vec2(0.5 * exp2(level)) / page_size;
	highp vec2 sample_uv = clamp(p_uv_unclamped, p_sampler_domain.xy + half_texel, p_sampler_domain.xy + p_sampler_domain.zw - half_texel);
	return sample_page_texture_lod(sample_uv, p_array_layer, level);
#elif defined(REGION_MIP_TRILINEAR)
	highp float level_0 = floor(lod);
	highp float level_1 = min(level_0 + 1.0, float(p_max_region_lod));
	highp vec2 half_texel_0 = vec2(0.5 * exp2(level_0)) / page_size;
	highp vec2 half_texel_1 = vec2(0.5 * exp2(level_1)) / page_size;
	highp vec2 sample_uv_0 = clamp(p_uv_unclamped, p_sampler_domain.xy + half_texel_0, p_sampler_domain.xy + p_sampler_domain.zw - half_texel_0);
	highp vec2 sample_uv_1 = clamp(p_uv_unclamped, p_sampler_domain.xy + half_texel_1, p_sampler_domain.xy + p_sampler_domain.zw - half_texel_1);
	highp vec4 sample_0 = sample_page_texture_lod(sample_uv_0, p_array_layer, level_0);
	highp vec4 sample_1 = sample_page_texture_lod(sample_uv_1, p_array_layer, level_1);
	return mix(sample_0, sample_1, fract(lod));
#else
	highp vec2 half_texel = vec2(0.5) / page_size;
	highp vec2 sample_uv = clamp(p_uv_unclamped, p_sampler_domain.xy + half_texel, p_sampler_domain.xy + p_sampler_domain.zw - half_texel);
	return sample_page_texture_lod(sample_uv, p_array_layer, 0.0);
#endif
}

// texture_page() and texture_page_lod() read the drawn picture's own texture: its cell of the page array for page
// rectangles, sampled like the rectangle itself, TEXTURE otherwise.
#ifdef USE_PAGE_RECT
vec4 texture_page(highp vec2 p_uv) {
	if (bool(read_draw_data_flags & INSTANCE_FLAGS_USE_REGION_SAMPLING)) {
		return sample_isolated_page_2d(p_uv, read_draw_data_ninepatch_margins, read_draw_data_array_layer, read_draw_data_max_region_lod);
	}
	return sample_page_texture_lod(p_uv, read_draw_data_array_layer, 0.0);
}

vec4 texture_page_lod(highp vec2 p_uv, highp float p_lod) {
	if (bool(read_draw_data_flags & INSTANCE_FLAGS_USE_REGION_SAMPLING)) {
		highp float level = clamp(p_lod, 0.0, float(read_draw_data_max_region_lod));
		highp vec2 half_texel = vec2(0.5 * exp2(ceil(level))) / page_texture_size();
		highp vec4 domain = read_draw_data_ninepatch_margins;
		return sample_page_texture_lod(clamp(p_uv, domain.xy + half_texel, domain.xy + domain.zw - half_texel), read_draw_data_array_layer, level);
	}
	return sample_page_texture_lod(p_uv, read_draw_data_array_layer, p_lod);
}
#else
vec4 texture_page(highp vec2 p_uv) {
	return texture(color_texture, p_uv);
}

vec4 texture_page_lod(highp vec2 p_uv, highp float p_lod) {
	return textureLod(color_texture, p_uv, p_lod);
}
#endif

#ifdef USE_MASKS
float sample_mask(vec2 p_uv, vec4 p_region, vec2 p_margin, float p_layer, float p_lod, float p_outside) {
	if (all(greaterThanEqual(p_uv, vec2(0.0))) && all(lessThan(p_uv, vec2(1.0)))) {
		vec2 sample_uv = p_region.xy + clamp(p_uv, p_margin, vec2(1.0) - p_margin) * p_region.zw;
		return textureLod(mask_page, vec3(sample_uv, p_layer), p_lod).a;
	}
	return p_outside;
}

float sample_mask_entry(uint p_entry, uint p_slot) {
	if (p_entry == 0u) {
		return 1.0;
	}
	uint base = mask_entries_base + (p_entry - 1u) * 4u;
	vec4 extra = uintBitsToFloat(read_mask_row(base + 3u));
	if (extra.w == 0.0) {
		return 0.0;
	}
	vec4 row_x = uintBitsToFloat(read_mask_row(base));
	vec4 row_y = uintBitsToFloat(read_mask_row(base + 1u));
	vec4 region = uintBitsToFloat(read_mask_row(base + 2u));
	vec3 position = vec3(vertex_interp, 1.0);
#ifdef DEFORM_RECT
	uint code = (varying_F.w >> (p_slot * 3u)) & 7u;
	if (code != 0u) {
		vec4 places = code < 3u ? deform_rest_interp : deform_rest_far_interp;
		position.xy = (code & 1u) != 0u ? places.xy : places.zw;
	}
#endif
	vec2 mask_uv = vec2(dot(row_x.xyz, position), dot(row_y.xyz, position));
	vec2 margin = min(extra.yz * exp2(ceil(mask_mipmaps ? row_y.w : 0.0)), vec2(0.5));
	return sample_mask(mask_uv, region, margin, extra.x, row_y.w, row_x.w);
}
#endif

#ifdef DEFORM_RECT
// Canvas-space directions of the picture's local axes at this pixel of a bent grid: screen-space derivatives of the
// texture coordinate and of the position give how the picture is turned here.
mat2 deform_local_axes(vec4 p_src_rect, mat2 p_item_axes) {
	mat2 uv_derivatives = mat2(dFdx(uv_interp), dFdy(uv_interp));
	mat2 position_derivatives = mat2(dFdx(vertex_interp), dFdy(vertex_interp));
	mat2 canvas_basis = mat2(canvas_transform[0].xy, canvas_transform[1].xy);
	if (abs(determinant(uv_derivatives)) < 1.0e-20 || abs(determinant(canvas_basis)) < 1.0e-20) {
		return p_item_axes;
	}
	mat2 canvas_from_uv = inverse(canvas_basis) * position_derivatives * inverse(uv_derivatives);
	bool transposed = bool(read_draw_data_flags & INSTANCE_FLAGS_TRANSPOSE_RECT);
	vec2 axis_x = (transposed ? canvas_from_uv[1] : canvas_from_uv[0]) * sign(p_src_rect.z);
	vec2 axis_y = (transposed ? canvas_from_uv[0] : canvas_from_uv[1]) * sign(p_src_rect.w);
	if (dot(axis_x, axis_x) < 1.0e-20 || dot(axis_y, axis_y) < 1.0e-20) {
		return p_item_axes;
	}
	return mat2(normalize(axis_x), normalize(axis_y));
}
#endif

void main() {
	vec4 color = color_interp;
	vec2 uv = uv_interp;
	vec2 vertex = vertex_interp;

#ifdef USE_PAGE_RECT
	vec4 region_rect = read_draw_data_ninepatch_margins;
#elif !defined(USE_ATTRIBUTES) && !defined(USE_PRIMITIVE)
	vec4 region_rect = read_draw_data_src_rect;
#else
	vec4 region_rect = vec4(0.0, 0.0, 1.0 / read_draw_data_color_texture_pixel_size);
#endif
#if !defined(USE_ATTRIBUTES) && !defined(USE_PRIMITIVE)
	vec4 page_region = vec4(min(read_draw_data_src_rect.xy, read_draw_data_src_rect.xy + read_draw_data_src_rect.zw), abs(read_draw_data_src_rect.zw));
#else
	vec4 page_region = vec4(0.0, 0.0, 1.0, 1.0);
#endif

#if !defined(USE_ATTRIBUTES) && !defined(USE_PRIMITIVE)

#ifdef USE_NINEPATCH

	int draw_center = 2;
	uv = vec2(
			map_ninepatch_axis(pixel_size_interp.x, abs(read_draw_data_dst_rect_z), read_draw_data_color_texture_pixel_size.x, read_draw_data_ninepatch_margins.x, read_draw_data_ninepatch_margins.z, int(read_draw_data_flags >> INSTANCE_FLAGS_NINEPATCH_H_MODE_SHIFT) & 0x3, draw_center),
			map_ninepatch_axis(pixel_size_interp.y, abs(read_draw_data_dst_rect_w), read_draw_data_color_texture_pixel_size.y, read_draw_data_ninepatch_margins.y, read_draw_data_ninepatch_margins.w, int(read_draw_data_flags >> INSTANCE_FLAGS_NINEPATCH_V_MODE_SHIFT) & 0x3, draw_center));

	if (draw_center == 0) {
		color.a = 0.0;
	}

	uv = uv * read_draw_data_src_rect.zw + read_draw_data_src_rect.xy; //apply region if needed

#endif
	if (bool(read_draw_data_flags & INSTANCE_FLAGS_CLIP_RECT_UV)) {
		vec2 half_texpixel = read_draw_data_color_texture_pixel_size * 0.5;
		uv = clamp(uv, read_draw_data_src_rect.xy + half_texpixel, read_draw_data_src_rect.xy + abs(read_draw_data_src_rect.zw) - half_texpixel);
	}

#endif

#ifndef USE_PRIMITIVE
	if (bool(read_draw_data_flags & INSTANCE_FLAGS_USE_MSDF)) {
		float px_range = read_draw_data_ninepatch_margins.x;
		float outline_thickness = read_draw_data_ninepatch_margins.y;

		vec4 msdf_sample = texture(color_texture, uv);
		vec2 msdf_size = vec2(textureSize(color_texture, 0));
		vec2 dest_size = vec2(1.0) / fwidth(uv);
		float px_size = max(0.5 * dot((vec2(px_range) / msdf_size), dest_size), 1.0);
		float d = msdf_median(msdf_sample.r, msdf_sample.g, msdf_sample.b);

		if (outline_thickness > 0.0) {
			float cr = clamp(outline_thickness, 0.0, (px_range / 2.0) - 1.0) / px_range;
			d = min(d, msdf_sample.a);
			float a = clamp((d - 0.5 + cr) * px_size, 0.0, 1.0);
			color.a = a * color.a;
		} else {
			float a = clamp((d - 0.5) * px_size + 0.5, 0.0, 1.0);
			color.a = a * color.a;
		}
	} else if (bool(read_draw_data_flags & INSTANCE_FLAGS_USE_LCD)) {
		vec4 lcd_sample = texture(color_texture, uv);
		if (lcd_sample.a == 1.0) {
			color.rgb = lcd_sample.rgb * color.a;
		} else {
			color = vec4(0.0, 0.0, 0.0, 0.0);
		}
	}
#ifdef USE_PAGE_RECT
	else if (bool(read_draw_data_flags & INSTANCE_FLAGS_USE_REGION_SAMPLING)) {
		color *= sample_isolated_page_2d(uv, read_draw_data_ninepatch_margins, read_draw_data_array_layer, read_draw_data_max_region_lod);
	}
#endif
	else {
#else
	{
#endif
#ifdef USE_PAGE_RECT
		color *= sample_page_texture_lod(uv, read_draw_data_array_layer, 0.0);
#else
		color *= texture(color_texture, uv);
#endif
	}

	uint light_count = read_draw_data_flags & uint(0xF); // Max 16 lights.
	bool using_light = light_count > 0u || directional_light_count > 0u;

	vec3 normal;

#if defined(NORMAL_USED)
	bool normal_used = true;
#else
	bool normal_used = false;
#endif

	if (normal_used || (using_light && bool(batch_flags & BATCH_FLAGS_DEFAULT_NORMAL_MAP_USED))) {
		normal.xy = texture(normal_texture, uv).xy * vec2(2.0, -2.0) - vec2(1.0, -1.0);

#if !defined(USE_ATTRIBUTES) && !defined(USE_PRIMITIVE)
		if (bool(read_draw_data_flags & INSTANCE_FLAGS_TRANSPOSE_RECT)) {
			normal.xy = normal.yx;
		}
		normal.xy *= sign(read_draw_data_src_rect.zw);
#endif

		normal.z = sqrt(max(0.0, 1.0 - dot(normal.xy, normal.xy)));
		normal_used = true;
	} else {
		normal = vec3(0.0, 0.0, 1.0);
	}

	vec4 specular_shininess;

#if defined(SPECULAR_SHININESS_USED)

	bool specular_shininess_used = true;
#else
	bool specular_shininess_used = false;
#endif

	if (specular_shininess_used || (using_light && normal_used && bool(batch_flags & BATCH_FLAGS_DEFAULT_SPECULAR_MAP_USED))) {
		specular_shininess = texture(specular_texture, uv);
		specular_shininess *= godot_unpackUnorm4x8(specular_shininess_in);
		specular_shininess_used = true;
	} else {
		specular_shininess = vec4(1.0);
	}

#if defined(SCREEN_UV_USED)
	vec2 screen_uv = gl_FragCoord.xy * screen_pixel_size;
#else
	vec2 screen_uv = vec2(0.0);
#endif

	vec2 color_texture_pixel_size = read_draw_data_color_texture_pixel_size.xy;

	vec3 light_vertex = vec3(vertex, 0.0);
	vec2 shadow_vertex = vertex;

	{
		float normal_map_depth = 1.0;

#if defined(NORMAL_MAP_USED)
		vec3 normal_map = vec3(0.0, 0.0, 1.0);
		normal_used = true;
#endif

#CODE : FRAGMENT

#if defined(NORMAL_MAP_USED)
		normal = mix(vec3(0.0, 0.0, 1.0), normal_map * vec3(2.0, -2.0, 1.0) - vec3(1.0, -1.0, 0.0), normal_map_depth);
#endif
	}

	if (normal_used) {
		//convert by item transform
		mat2 item_axes = mat2(normalize(read_draw_data_world_x), normalize(read_draw_data_world_y));
#ifdef DEFORM_RECT
		if (bool(batch_flags & BATCH_FLAGS_DEFORM_GRID)) {
			item_axes = deform_local_axes(read_draw_data_src_rect, item_axes);
		}
#endif
		normal.xy = item_axes * normal.xy;
		//convert by canvas transform
		normal = normalize((canvas_normal_transform * vec4(normal, 0.0)).xyz);
	}

#ifdef USE_MASKS
	float coverage = 1.0;
	uint mask_set = read_draw_data_item_data >> 16u;
	if (mask_set != 0u) {
		if (mask_set == 1u) {
			discard;
		}
		if (mask_extra_interp.w >= 0.0) {
			uint mask_flags = uint(mask_extra_interp.w);
			coverage = (mask_flags & 1u) == 0u ? 0.0 : sample_mask(mask_uv_interp, mask_region_interp, mask_extra_interp.xy, float(mask_flags >> 2u), mask_extra_interp.z, float((mask_flags >> 1u) & 1u));
		} else {
			uvec4 indices = read_mask_row(mask_set);
			coverage *= sample_mask_entry(indices.x & 0xFFFFu, 0u);
			coverage *= sample_mask_entry(indices.x >> 16u, 1u);
			coverage *= sample_mask_entry(indices.y & 0xFFFFu, 2u);
			coverage *= sample_mask_entry(indices.y >> 16u, 3u);
			coverage *= sample_mask_entry(indices.z & 0xFFFFu, 4u);
			coverage *= sample_mask_entry(indices.z >> 16u, 5u);
			coverage *= sample_mask_entry(indices.w & 0xFFFFu, 6u);
			coverage *= sample_mask_entry(indices.w >> 16u, 7u);
		}
#if defined(MASK_BLEND_DISABLED) || defined(MASK_BLEND_MULTIPLY)
		if (coverage == 0.0) {
			discard;
		}
#endif
		color.a *= coverage;
	}
#endif

	vec4 base_color = color;

#ifdef MODE_LIGHT_ONLY
	float light_only_alpha = 0.0;
#elif !defined(MODE_UNSHADED)
	color *= canvas_modulation;
#endif

#if !defined(DISABLE_LIGHTING) && !defined(MODE_UNSHADED)

	// Directional Lights

	for (uint i = 0u; i < directional_light_count; i++) {
		uint light_base = i;

		vec2 direction = light_array[light_base].position;
		vec4 light_color = light_array[light_base].color;

#ifdef LIGHT_CODE_USED

		vec4 shadow_modulate = vec4(1.0);
		light_color = light_compute(light_vertex, vec3(direction, light_array[light_base].height), normal, light_color, light_color.a, specular_shininess, shadow_modulate, screen_uv, uv, base_color, true);
#else

		if (normal_used) {
			vec3 light_vec = normalize(mix(vec3(direction, 0.0), vec3(0, 0, 1), light_array[light_base].height));
			light_color.rgb = light_normal_compute(light_vec, normal, base_color.rgb, light_color.rgb, specular_shininess, specular_shininess_used);
		} else {
			light_color.rgb *= base_color.rgb;
		}
#endif

		if (bool(light_array[light_base].flags & LIGHT_FLAGS_HAS_SHADOW)) {
			vec2 shadow_pos = (vec4(shadow_vertex, 0.0, 1.0) * mat4(light_array[light_base].shadow_matrix[0], light_array[light_base].shadow_matrix[1], vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0))).xy; //multiply inverse given its transposed. Optimizer removes useless operations.

			vec4 shadow_uv = vec4(shadow_pos.x, light_array[light_base].shadow_y_ofs, shadow_pos.y * light_array[light_base].shadow_zfar_inv, 1.0);

			light_color = light_shadow_compute(light_base, light_color, shadow_uv
#ifdef LIGHT_CODE_USED
					,
					shadow_modulate.rgb
#endif
			);
		}

		light_blend_compute(light_base, light_color, color.rgb);
#ifdef MODE_LIGHT_ONLY
		light_only_alpha += light_color.a;
#endif
	}

	// Positional Lights

	for (uint i = 0u; i < MAX_LIGHTS_PER_ITEM; i++) {
		if (i >= light_count) {
			break;
		}
		uint light_base;
		if (i < 8u) {
			if (i < 4u) {
				light_base = read_draw_data_lights[0];
			} else {
				light_base = read_draw_data_lights[1];
			}
		} else {
			if (i < 12u) {
				light_base = read_draw_data_lights[2];
			} else {
				light_base = read_draw_data_lights[3];
			}
		}
		light_base >>= (i & 3u) * 8u;
		light_base &= uint(0xFF);

		vec2 tex_uv = (vec4(vertex, 0.0, 1.0) * mat4(light_array[light_base].texture_matrix[0], light_array[light_base].texture_matrix[1], vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0))).xy; //multiply inverse given its transposed. Optimizer removes useless operations.
		vec2 tex_uv_atlas = tex_uv * light_array[light_base].atlas_rect.zw + light_array[light_base].atlas_rect.xy;

		if (any(lessThan(tex_uv, vec2(0.0, 0.0))) || any(greaterThanEqual(tex_uv, vec2(1.0, 1.0)))) {
			//if outside the light texture, light color is zero
			continue;
		}

		vec4 light_color = textureLod(atlas_texture, tex_uv_atlas, 0.0);
		vec4 light_base_color = light_array[light_base].color;

#ifdef LIGHT_CODE_USED

		vec4 shadow_modulate = vec4(1.0);
		vec3 light_position = vec3(light_array[light_base].position, light_array[light_base].height);

		light_color.rgb *= light_base_color.rgb;
		light_color = light_compute(light_vertex, light_position, normal, light_color, light_base_color.a, specular_shininess, shadow_modulate, screen_uv, uv, base_color, false);
#else

		light_color.rgb *= light_base_color.rgb * light_base_color.a;

		if (normal_used) {
			vec3 light_pos = vec3(light_array[light_base].position, light_array[light_base].height);
			vec3 pos = light_vertex;
			vec3 light_vec = normalize(light_pos - pos);

			light_color.rgb = light_normal_compute(light_vec, normal, base_color.rgb, light_color.rgb, specular_shininess, specular_shininess_used);
		} else {
			light_color.rgb *= base_color.rgb;
		}
#endif

		if (bool(light_array[light_base].flags & LIGHT_FLAGS_HAS_SHADOW) && bool(read_draw_data_flags & uint(INSTANCE_FLAGS_SHADOW_MASKED << i))) {
			vec2 shadow_pos = (vec4(shadow_vertex, 0.0, 1.0) * mat4(light_array[light_base].shadow_matrix[0], light_array[light_base].shadow_matrix[1], vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0))).xy; //multiply inverse given its transposed. Optimizer removes useless operations.

			vec2 pos_norm = normalize(shadow_pos);
			vec2 pos_abs = abs(pos_norm);
			vec2 pos_box = pos_norm / max(pos_abs.x, pos_abs.y);
			vec2 pos_rot = pos_norm * mat2(vec2(0.7071067811865476, -0.7071067811865476), vec2(0.7071067811865476, 0.7071067811865476)); //is there a faster way to 45 degrees rot?
			float tex_ofs;
			float dist;
			if (pos_rot.y > 0.0) {
				if (pos_rot.x > 0.0) {
					tex_ofs = pos_box.y * 0.125 + 0.125;
					dist = shadow_pos.x;
				} else {
					tex_ofs = pos_box.x * -0.125 + (0.25 + 0.125);
					dist = shadow_pos.y;
				}
			} else {
				if (pos_rot.x < 0.0) {
					tex_ofs = pos_box.y * -0.125 + (0.5 + 0.125);
					dist = -shadow_pos.x;
				} else {
					tex_ofs = pos_box.x * 0.125 + (0.75 + 0.125);
					dist = -shadow_pos.y;
				}
			}

			dist *= light_array[light_base].shadow_zfar_inv;

			//float distance = length(shadow_pos);
			vec4 shadow_uv = vec4(tex_ofs, light_array[light_base].shadow_y_ofs, dist, 1.0);

			light_color = light_shadow_compute(light_base, light_color, shadow_uv
#ifdef LIGHT_CODE_USED
					,
					shadow_modulate.rgb
#endif
			);
		}

		light_blend_compute(light_base, light_color, color.rgb);
#ifdef MODE_LIGHT_ONLY
		light_only_alpha += light_color.a;
#endif
	}
#endif

#ifdef MODE_LIGHT_ONLY
	color.a *= light_only_alpha;
#endif

#ifdef USE_MASKS
#ifdef MASK_PREMULTIPLIED_ALPHA
	color.rgb *= coverage;
#elif defined(MASK_BLEND_MULTIPLY)
	color.rgb = mix(vec3(1.0), color.rgb, coverage);
#endif
#endif

	frag_color = color;
}
