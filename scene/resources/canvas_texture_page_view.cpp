/**************************************************************************/
/*  canvas_texture_page_view.cpp                                          */
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

#include "canvas_texture_page_view.h"

#include "core/object/class_db.h"
#include "scene/resources/drawable_texture_2d.h"
#include "servers/rendering/rendering_server.h"

Error CanvasTexturePageView::configure(const Ref<Texture> &p_page_texture, const Rect2i &p_physical_region, uint32_t p_array_layer, uint32_t p_max_region_lod, uint64_t p_expected_generation, const Ref<Texture2D> &p_fallback_texture) {
	ERR_FAIL_COND_V(configured, ERR_ALREADY_IN_USE);
	ERR_FAIL_COND_V(p_page_texture.is_null(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_physical_region.position.x < 0 || p_physical_region.position.y < 0 || p_physical_region.size.x <= 0 || p_physical_region.size.y <= 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_array_layer != 0 || p_max_region_lod > 30 || p_expected_generation == 0, ERR_INVALID_PARAMETER);

	Ref<DrawableTexture2D> drawable = p_page_texture;
	ERR_FAIL_COND_V(drawable.is_null(), ERR_UNAVAILABLE);
	const int page_width = drawable->get_width();
	const int page_height = drawable->get_height();
	const RID page_rid = drawable->get_rid();
	const int page_mipmap_count = drawable->get_use_mipmaps() ? Image::get_image_required_mipmaps(page_width, page_height, drawable->get_format()) + 1 : 1;
	ERR_FAIL_COND_V(p_physical_region.get_end().x > page_width || p_physical_region.get_end().y > page_height, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_max_region_lod >= uint32_t(page_mipmap_count), ERR_INVALID_PARAMETER);
	if (p_fallback_texture.is_valid()) {
		ERR_FAIL_COND_V(p_fallback_texture->get_width() <= 0 || p_fallback_texture->get_height() <= 0, ERR_INVALID_PARAMETER);
		ERR_FAIL_COND_V(p_fallback_texture->get_rid().is_null() || p_fallback_texture->get_rid() == page_rid, ERR_INVALID_PARAMETER);
	}
	ERR_FAIL_COND_V(RenderingServer::get_singleton()->texture_drawable_get_generation(page_rid) != p_expected_generation, ERR_INVALID_DATA);

	const uint32_t alignment = 1U << p_max_region_lod;
	ERR_FAIL_COND_V(uint32_t(page_width) % alignment != 0 || uint32_t(page_height) % alignment != 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(uint32_t(p_physical_region.position.x) % alignment != 0 || uint32_t(p_physical_region.position.y) % alignment != 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(uint32_t(p_physical_region.size.x) % alignment != 0 || uint32_t(p_physical_region.size.y) % alignment != 0, ERR_INVALID_PARAMETER);

	page_texture = p_page_texture;
	physical_region = p_physical_region;
	logical_size = p_physical_region.size;
	array_layer = p_array_layer;
	max_region_lod = p_max_region_lod;
	expected_generation = p_expected_generation;
	fallback_texture = p_fallback_texture;
	configured = true;
	emit_changed();
	return OK;
}

Image::Format CanvasTexturePageView::get_format() const {
	return fallback_texture.is_valid() ? fallback_texture->get_format() : Image::FORMAT_MAX;
}

int CanvasTexturePageView::get_mipmap_count() const {
	return configured ? int(max_region_lod) : 0;
}

int CanvasTexturePageView::get_width() const {
	return configured ? logical_size.x : 0;
}

int CanvasTexturePageView::get_height() const {
	return configured ? logical_size.y : 0;
}

bool CanvasTexturePageView::is_pixel_opaque(int p_x, int p_y) const {
	return fallback_texture.is_valid() ? fallback_texture->is_pixel_opaque(p_x, p_y) : false;
}

bool CanvasTexturePageView::has_alpha() const {
	return fallback_texture.is_valid() ? fallback_texture->has_alpha() : true;
}

bool CanvasTexturePageView::has_mipmaps() const {
	return configured && max_region_lod > 0;
}

RID CanvasTexturePageView::get_rid() const {
	return fallback_texture.is_valid() ? fallback_texture->get_rid() : RID();
}

Ref<Image> CanvasTexturePageView::get_image() const {
	return fallback_texture.is_valid() ? fallback_texture->get_image() : Ref<Image>();
}

void CanvasTexturePageView::draw(RID p_canvas_item, const Point2 &p_pos, const Color &p_modulate, bool p_transpose) const {
	draw_rect_region(p_canvas_item, Rect2(p_pos, get_size()), Rect2(Vector2(), get_size()), p_modulate, p_transpose, false);
}

void CanvasTexturePageView::draw_rect(RID p_canvas_item, const Rect2 &p_rect, bool p_tile, const Color &p_modulate, bool p_transpose) const {
	if (!configured || p_tile) {
		if (fallback_texture.is_valid()) {
			fallback_texture->draw_rect(p_canvas_item, p_rect, p_tile, p_modulate, p_transpose);
		}
		return;
	}
	draw_rect_region(p_canvas_item, p_rect, Rect2(Vector2(), get_size()), p_modulate, p_transpose, false);
}

void CanvasTexturePageView::draw_rect_region(RID p_canvas_item, const Rect2 &p_rect, const Rect2 &p_src_rect, const Color &p_modulate, bool p_transpose, bool p_clip_uv) const {
	if (!configured) {
		return;
	}

	Rect2 clipped_rect;
	Rect2 clipped_source;
	if (!get_rect_region(p_rect, p_src_rect, clipped_rect, clipped_source)) {
		return;
	}
	const Rect2 fallback_source = _get_fallback_source(clipped_source);
	if (p_clip_uv) {
		if (fallback_texture.is_valid()) {
			fallback_texture->draw_rect_region(p_canvas_item, clipped_rect, fallback_source, p_modulate, p_transpose, false);
		}
		return;
	}

	const Rect2 mapped_source(Vector2(physical_region.position) + clipped_source.position, clipped_source.size);
	RenderingServer::get_singleton()->canvas_item_add_texture_page_rect_region(
			p_canvas_item,
			clipped_rect,
			page_texture->get_rid(),
			mapped_source,
			Rect2(physical_region),
			array_layer,
			max_region_lod,
			expected_generation,
			fallback_texture.is_valid() ? fallback_texture->get_rid() : RID(),
			fallback_source,
			p_modulate,
			p_transpose,
			false);
}

Rect2 CanvasTexturePageView::_get_fallback_source(const Rect2 &p_logical_source) const {
	if (fallback_texture.is_null() || logical_size.x <= 0 || logical_size.y <= 0) {
		return Rect2();
	}
	const Vector2 scale = fallback_texture->get_size() / Vector2(logical_size);
	return Rect2(p_logical_source.position * scale, p_logical_source.size * scale);
}

bool CanvasTexturePageView::get_rect_region(const Rect2 &p_rect, const Rect2 &p_src_rect, Rect2 &r_rect, Rect2 &r_src_rect) const {
	if (!configured) {
		return false;
	}

	Rect2 source = p_src_rect;
	if (source.size == Size2()) {
		source.size = get_size();
	}
	if (source.size.x == 0 || source.size.y == 0) {
		return false;
	}

	const Vector2 scale = p_rect.size / source.size;
	const Rect2 clipped_source = Rect2(Vector2(), get_size()).intersection(source);
	if (clipped_source.size == Size2()) {
		return false;
	}

	Vector2 offset = clipped_source.position - source.position;
	if (scale.x < 0) {
		offset.x += clipped_source.size.x - source.size.x;
	}
	if (scale.y < 0) {
		offset.y += clipped_source.size.y - source.size.y;
	}

	r_rect = Rect2(p_rect.position + offset * scale, clipped_source.size * scale);
	r_src_rect = clipped_source;
	return true;
}

void CanvasTexturePageView::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "page_texture", "physical_region", "array_layer", "max_region_lod", "expected_generation", "fallback_texture"), &CanvasTexturePageView::configure, DEFVAL(Ref<Texture2D>()));
}
