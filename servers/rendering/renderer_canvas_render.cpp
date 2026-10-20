/**************************************************************************/
/*  renderer_canvas_render.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "renderer_canvas_render.h"

#include "servers/rendering/rendering_server_globals.h"

RendererCanvasRender *RendererCanvasRender::singleton = nullptr;

// Mirrors deform_vertex() of the canvas shaders: weights come from the straight (rest) position, records apply from
// the innermost, joints from the last. Maps are kept as affine functions of the canvas-space rest position.
RendererCanvasRender::DeformRectKind RendererCanvasRender::deform_classify_rect(uint32_t p_chain, const Transform2D &p_world, const Rect2 &p_rect, Transform2D &r_move) const {
	if (p_chain == 0 || p_chain >= deform_data.record_base) {
		return DEFORM_RECT_GRID;
	}
	const float *rows = deform_data.rows.ptr();
	const uint32_t row_count = deform_data.rows.size() / 4;
	uint32_t records[DEFORM_MAX_LEVELS];
	memcpy(records, rows + p_chain * 4, sizeof(records));
	for (uint32_t &record : records) {
		record &= DEFORM_RECORD_MASK;
	}
	const Vector2 corners[4] = {
		p_world.xform(p_rect.position),
		p_world.xform(Vector2(p_rect.position.x + p_rect.size.x, p_rect.position.y)),
		p_world.xform(p_rect.position + p_rect.size),
		p_world.xform(Vector2(p_rect.position.x, p_rect.position.y + p_rect.size.y)),
	};
	Transform2D total;
	// Set once a joint band or stretch piece crosses the rectangle; warps of the outer records still decide the grid.
	bool grid = false;
	for (uint32_t level = 0; level < DEFORM_MAX_LEVELS && records[level] != 0; level++) {
		const uint32_t offset = deform_data.record_base + records[level] - 1;
		if (offset + 4 > row_count) {
			return DEFORM_RECT_GRID;
		}
		const float *record = rows + offset * 4;
		const Transform2D owner(Vector2(record[0], record[1]), Vector2(record[2], record[3]), Vector2(record[4], record[5]));
		const Transform2D owner_inverse = owner.affine_inverse();
		const uint32_t joints = uint32_t(record[6]);
		const uint32_t warp_columns = uint32_t(record[7]);
		if (offset + 4 + joints * 2 > row_count) {
			return DEFORM_RECT_GRID;
		}
		Vector2 rest[4];
		Rect2 bounds(owner_inverse.xform(corners[0]), Vector2());
		for (int i = 0; i < 4; i++) {
			rest[i] = owner_inverse.xform(corners[i]);
			bounds.expand_to(rest[i]);
		}
		if (warp_columns != 0 && record[9] != 0.0f) {
			const Rect2 warp_rect(record[12], record[13], record[14], record[15]);
			if (bounds.intersects(warp_rect, true)) {
				return DEFORM_RECT_WARP_GRID;
			}
		}
		if (grid) {
			continue;
		}
		Transform2D point = owner_inverse * total;
		for (int joint = int(joints) - 1; joint >= 0 && !grid; joint--) {
			const float *a = record + 16 + joint * 8;
			const float *b = a + 4;
			if (b[0] == 0.0f && b[2] == 1.0f) {
				// A joint that neither turns nor stretches moves nothing.
				continue;
			}
			const Vector2 pivot(a[0], a[1]);
			const Vector2 axis(a[2], a[3]);
			const float blend = MAX(b[1], 0.001f);
			float lowest = 1.0e30f;
			float highest = -1.0e30f;
			for (int i = 0; i < 4; i++) {
				const float along = axis.dot(rest[i] - pivot);
				lowest = MIN(lowest, along);
				highest = MAX(highest, along);
			}
			if (b[2] != 1.0f) {
				const float segment = b[3] > 0.0f ? b[3] : 1.0e9f;
				const float grow = b[2] - 1.0f;
				if (lowest >= segment) {
					point.columns[2] += axis * (segment * grow);
				} else if (lowest >= 0.0f && highest <= segment) {
					point.columns[0] += axis * (grow * axis.dot(owner_inverse.columns[0]));
					point.columns[1] += axis * (grow * axis.dot(owner_inverse.columns[1]));
					point.columns[2] += axis * (grow * axis.dot(owner_inverse.columns[2] - pivot));
				} else if (highest > 0.0f) {
					grid = true;
					break;
				}
			}
			if (b[0] == 0.0f) {
				continue;
			}
			if (lowest >= blend) {
				Transform2D turn(b[0], Vector2());
				turn.columns[2] = pivot - turn.basis_xform(pivot);
				point = turn * point;
			} else if (highest > -blend) {
				grid = true;
			}
		}
		total = owner * point;
	}
	if (grid) {
		return DEFORM_RECT_GRID;
	}
	if (total.is_equal_approx(Transform2D())) {
		return DEFORM_RECT_STILL;
	}
	r_move = total;
	return DEFORM_RECT_RIGID;
}

uint32_t RendererCanvasRender::deform_rect_order(uint32_t p_deform, const Transform2D &p_world, const Rect2 &p_rect, const Vector2 &p_source_size, bool p_warp) const {
	const uint32_t chain = p_deform & DEFORM_CHAIN_MASK;
	if (!(p_deform & DEFORM_TIP_UNDER) || chain == 0 || chain >= deform_data.record_base) {
		return 0;
	}
	const float *rows = deform_data.rows.ptr();
	uint32_t records[DEFORM_MAX_LEVELS];
	memcpy(records, rows + chain * 4, sizeof(records));
	const Transform2D to_item_basis = p_world.affine_inverse();
	const Vector2 lo = p_rect.position;
	const Vector2 hi = p_rect.get_end();
	const Vector2 centre = p_rect.get_center();
	Vector2 tip;
	Vector2 turns;
	for (uint32_t packed_record : records) {
		const uint32_t index = packed_record & DEFORM_RECORD_MASK;
		if (index == 0) {
			break;
		}
		const float *record = rows + (deform_data.record_base + index - 1) * 4;
		const Transform2D owner(Vector2(record[0], record[1]), Vector2(record[2], record[3]), Vector2(record[4], record[5]));
		const Transform2D to_item = to_item_basis * owner;
		const Transform2D to_owner = to_item.affine_inverse();
		const uint32_t joints = uint32_t(record[6]);
		for (uint32_t joint = 0; joint < joints; joint++) {
			const float *a = record + 16 + joint * 8;
			const float *b = a + 4;
			if (b[0] == 0.0f) {
				continue;
			}
			const Vector2 axis(a[2], a[3]);
			const Vector2 pivot = to_item.xform(Vector2(a[0], a[1]));
			const Vector2 normal(axis.dot(to_owner.columns[0]), axis.dot(to_owner.columns[1]));
			const real_t extent = normal.abs().dot(p_rect.size) * 0.5;
			if (Math::abs(normal.dot(centre - pivot)) < extent + MAX(b[1], 0.001f)) {
				tip += normal.normalized() * Math::abs(b[0]);
			}
			// Match the shader's adaptive band orientation; the tip itself includes diagonal joints too.
			if (joint < 4 && !p_warp) {
				const Vector2 direction = to_item.basis_xform(axis);
				const real_t length = direction.length();
				const real_t half_width = MAX(b[1], 0.5f) * length;
				if (Math::abs(direction.x) >= 0.85 * length && pivot.x + half_width > lo.x && pivot.x - half_width < hi.x) {
					turns.x += Math::abs(b[0]);
				} else if (Math::abs(direction.y) >= 0.85 * length && pivot.y + half_width > lo.y && pivot.y - half_width < hi.y) {
					turns.y += Math::abs(b[0]);
				}
			}
		}
		const uint32_t columns = uint32_t(record[7]);
		const uint32_t height = uint32_t(record[8]);
		if (p_warp && columns >= 2 && height >= 2 && record[9] != 0.0f) {
			const float *offsets = record + 16 + joints * 8;
			for (uint32_t y = 0; y < height; y++) {
				for (uint32_t x = 0; x < columns; x++) {
					const Vector2 position = to_item.xform(Vector2(record[12] + record[14] * x / (columns - 1), record[13] + record[15] * y / (height - 1)));
					if (position.x < lo.x || position.y < lo.y || position.x > hi.x || position.y > hi.y) {
						continue;
					}
					const uint32_t at = (y * columns + x) * 2;
					const real_t distance = to_item.basis_xform(Vector2(offsets[at], offsets[at + 1]) * record[9]).length();
					tip += (position - centre) * distance;
				}
			}
		}
	}
	// Warp lines are placed in rectangle space directly. Joint lines follow the signed source axes.
	if (!p_warp) {
		tip *= p_source_size.sign();
	}
	const bool across = !p_warp && turns.x > turns.y;
	const bool reverse_rows = (across ? tip.x : tip.y) > 0;
	const bool reverse_columns = (across ? tip.y : tip.x) > 0;
	return (reverse_rows ? DEFORM_GRID_REVERSE_ROWS : 0) | (reverse_columns ? DEFORM_GRID_REVERSE_COLUMNS : 0);
}

uint32_t RendererCanvasRender::deform_follow_code(uint32_t p_chain, uint32_t p_slot) const {
	if (p_chain == 0 || p_chain >= deform_data.record_base || p_slot >= 8) {
		return 0;
	}
	uint32_t word;
	memcpy(&word, deform_data.rows.ptr() + p_chain * 4 + p_slot / 2, sizeof(word));
	return (word >> (DEFORM_FOLLOW_CODE_SHIFT + (p_slot & 1) * DEFORM_FOLLOW_CODE_BITS)) & ((1u << DEFORM_FOLLOW_CODE_BITS) - 1);
}

bool RendererCanvasRender::Item::CommandTexturePageRect::is_geometry_valid(const Size2i &p_page_size, int p_mipmap_count) const {
	const uint16_t required_flags = CANVAS_RECT_REGION | CANVAS_RECT_REGION_SAMPLING;
	const uint16_t incompatible_flags = CANVAS_RECT_TILE | CANVAS_RECT_MSDF | CANVAS_RECT_LCD | CANVAS_RECT_IS_GROUP;
	if ((flags & required_flags) != required_flags || (flags & incompatible_flags) != 0) {
		return false;
	}
	if (expected_generation == 0 || p_page_size.x <= 0 || p_page_size.y <= 0 || p_mipmap_count <= 0) {
		return false;
	}
	if (!rect.is_finite() || !source.is_finite() || !sampler_domain.is_finite()) {
		return false;
	}
	if (rect.size.x < 0 || rect.size.y < 0 || source.size.x <= 0 || source.size.y <= 0 || sampler_domain.size.x <= 0 || sampler_domain.size.y <= 0) {
		return false;
	}
	if (!sampler_domain.encloses(source) || !Rect2(Vector2(), p_page_size).encloses(sampler_domain)) {
		return false;
	}
	if (sampler_domain.position.x != Math::floor(sampler_domain.position.x) ||
			sampler_domain.position.y != Math::floor(sampler_domain.position.y) ||
			sampler_domain.size.x != Math::floor(sampler_domain.size.x) ||
			sampler_domain.size.y != Math::floor(sampler_domain.size.y)) {
		return false;
	}
	if (max_region_lod > 30 || max_region_lod >= uint32_t(p_mipmap_count)) {
		return false;
	}
	const uint32_t alignment = 1U << max_region_lod;
	if (uint32_t(p_page_size.x) % alignment != 0 || uint32_t(p_page_size.y) % alignment != 0) {
		return false;
	}
	const int64_t domain_x = int64_t(sampler_domain.position.x);
	const int64_t domain_y = int64_t(sampler_domain.position.y);
	const int64_t domain_width = int64_t(sampler_domain.size.x);
	const int64_t domain_height = int64_t(sampler_domain.size.y);
	if (domain_x % alignment != 0 || domain_y % alignment != 0 || domain_width % alignment != 0 || domain_height % alignment != 0) {
		return false;
	}
	return domain_width >= alignment && domain_height >= alignment;
}

const Rect2 &RendererCanvasRender::Item::get_rect() const {
	if (custom_rect || (!rect_dirty && !update_when_visible && skeleton == RID())) {
		return rect;
	}

	//must update rect

	if (commands == nullptr) {
		rect = Rect2();
		rect_dirty = false;
		return rect;
	}

	Transform2D xf;
	bool found_xform = false;
	bool first = true;

	const Item::Command *c = commands;

	while (c) {
		Rect2 r;

		switch (c->type) {
			case Item::Command::TYPE_RECT: {
				const Item::CommandRect *crect = static_cast<const Item::CommandRect *>(c);
				r = crect->rect;

			} break;
			case Item::Command::TYPE_TEXTURE_PAGE_RECT: {
				const Item::CommandTexturePageRect *crect = static_cast<const Item::CommandTexturePageRect *>(c);
				r = crect->rect;
			} break;
			case Item::Command::TYPE_NINEPATCH: {
				const Item::CommandNinePatch *style = static_cast<const Item::CommandNinePatch *>(c);
				r = style->rect;
			} break;

			case Item::Command::TYPE_POLYGON: {
				const Item::CommandPolygon *polygon = static_cast<const Item::CommandPolygon *>(c);
				r = polygon->polygon.rect_cache;
			} break;
			case Item::Command::TYPE_PRIMITIVE: {
				const Item::CommandPrimitive *primitive = static_cast<const Item::CommandPrimitive *>(c);
				for (uint32_t j = 0; j < primitive->point_count; j++) {
					if (j == 0) {
						r.position = primitive->points[0];
					} else {
						r.expand_to(primitive->points[j]);
					}
				}
			} break;
			case Item::Command::TYPE_MESH: {
				const Item::CommandMesh *mesh = static_cast<const Item::CommandMesh *>(c);
				AABB aabb = RSG::mesh_storage->mesh_get_aabb(mesh->mesh, skeleton);

				r = Rect2(aabb.position.x, aabb.position.y, aabb.size.x, aabb.size.y);

			} break;
			case Item::Command::TYPE_MULTIMESH: {
				const Item::CommandMultiMesh *multimesh = static_cast<const Item::CommandMultiMesh *>(c);
				AABB aabb = RSG::mesh_storage->multimesh_get_aabb(multimesh->multimesh);

				r = Rect2(aabb.position.x, aabb.position.y, aabb.size.x, aabb.size.y);

			} break;
			case Item::Command::TYPE_PARTICLES: {
				const Item::CommandParticles *particles_cmd = static_cast<const Item::CommandParticles *>(c);
				if (particles_cmd->particles.is_valid()) {
					AABB aabb = RSG::particles_storage->particles_get_aabb(particles_cmd->particles);
					r = Rect2(aabb.position.x, aabb.position.y, aabb.size.x, aabb.size.y);
				}

			} break;
			case Item::Command::TYPE_TRANSFORM: {
				const Item::CommandTransform *transform = static_cast<const Item::CommandTransform *>(c);
				xf = transform->xform;
				found_xform = true;
				[[fallthrough]];
			}
			default: {
				c = c->next;
				continue;
			}
		}

		if (found_xform) {
			r = xf.xform(r);
		}

		if (first) {
			rect = r;
			first = false;
		} else {
			rect = rect.merge(r);
		}
		c = c->next;
	}

	rect_dirty = false;
	return rect;
}

RendererCanvasRender::Item::CommandMesh::~CommandMesh() {
	if (mesh_instance.is_valid()) {
		RSG::mesh_storage->mesh_instance_free(mesh_instance);
	}
}
