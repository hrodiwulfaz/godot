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
