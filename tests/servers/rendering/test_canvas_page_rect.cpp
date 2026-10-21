/**************************************************************************/
/*  test_canvas_page_rect.cpp                                             */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_canvas_page_rect)

#include "servers/rendering/renderer_canvas_cull.h"
#include "servers/rendering/renderer_canvas_render.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"

#include <cfloat>

namespace TestCanvasPageRect {

static RendererCanvasRender::TexturePageView make_view() {
	RendererCanvasRender::TexturePageView view;
	view.physical_region = Rect2i(64, 32, 32, 32);
	view.logical_size = Size2i(32, 32);
	view.max_region_lod = 2;
	view.expected_generation = 7;
	view.serial = 1;
	return view;
}

static RendererCanvasRender::Item::CommandTexturePageRect make_page_rect() {
	RendererCanvasRender::Item::CommandTexturePageRect rect;
	rect.flags = RendererCanvasRender::CANVAS_RECT_REGION | RendererCanvasRender::CANVAS_RECT_REGION_SAMPLING;
	rect.rect = Rect2(0, 0, 32, 32);
	rect.source = Rect2(0, 0, 32, 32);
	return rect;
}

TEST_CASE("[CanvasPageRect] The kept geometry check matches a fresh check for the same view and page") {
	const RendererCanvasRender::TexturePageView view = make_view();
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	CHECK(rect.is_geometry_valid(view, Size2i(128, 64), 3));
	CHECK(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	CHECK(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] Overflowing destination and source ends are invalid") {
	const RendererCanvasRender::TexturePageView view = make_view();
	for (const Rect2 &invalid : { Rect2(FLT_MAX, 0, FLT_MAX, 32), Rect2(0, FLT_MAX, 32, FLT_MAX) }) {
		RendererCanvasRender::Item::CommandTexturePageRect destination = make_page_rect();
		destination.rect = invalid;
		CHECK_FALSE(destination.is_geometry_valid(view, Size2i(128, 64), 3));
		RendererCanvasRender::Item::CommandTexturePageRect source = make_page_rect();
		source.source = invalid;
		CHECK_FALSE(source.is_geometry_valid(view, Size2i(128, 64), 3));
	}
}

TEST_CASE("[CanvasPageRect] A size override without a new generation checks again") {
	const RendererCanvasRender::TexturePageView view = make_view();
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	REQUIRE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	CHECK_FALSE(rect.is_geometry_valid_cached(view, 7, Size2i(80, 64), 3));
	CHECK(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] A mip count change checks again") {
	const RendererCanvasRender::TexturePageView view = make_view();
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	REQUIRE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	CHECK_FALSE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 2));
	CHECK(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] A new page generation checks again") {
	RendererCanvasRender::TexturePageView view = make_view();
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	REQUIRE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	view.max_region_lod = 3;
	CHECK(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	CHECK_FALSE(rect.is_geometry_valid_cached(view, 8, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] A view set again checks again") {
	RendererCanvasRender::TexturePageView view = make_view();
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	REQUIRE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	view.physical_region = Rect2i(112, 32, 32, 32);
	view.serial = 2;
	CHECK_FALSE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	view.physical_region = Rect2i(0, 0, 32, 32);
	view.serial = 3;
	CHECK(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] The kept place on the page follows the view and matches a fresh placement") {
	RendererCanvasRender::TexturePageView view = make_view();
	RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	rect.source = Rect2(8, 0, 16, 32);
	Rect2 fresh_rect;
	Rect2 fresh_source;
	REQUIRE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	REQUIRE(RendererCanvasRender::texture_page_place_on_page(view, rect.rect, rect.source, rect.flags, fresh_rect, fresh_source));
	CHECK(rect.placed);
	CHECK(rect.placed_rect == fresh_rect);
	CHECK(rect.placed_source == Rect2(72, 32, 16, 32));

	view.physical_region = Rect2i(0, 0, 16, 32);
	view.logical_offset = Vector2i(16, 0);
	view.serial = 2;
	REQUIRE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	CHECK(rect.placed);
	CHECK(rect.placed_rect == Rect2(16, 0, 16, 32));
	CHECK(rect.placed_source == Rect2(0, 0, 8, 32));

	view.physical_region = Rect2i(0, 0, 8, 32);
	view.logical_offset = Vector2i(24, 0);
	view.serial = 3;
	REQUIRE(rect.is_geometry_valid_cached(view, 7, Size2i(128, 64), 3));
	CHECK_FALSE(rect.placed);
}

TEST_CASE("[CanvasPageRect] A page without a generation is never kept") {
	const RendererCanvasRender::TexturePageView view = make_view();
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	CHECK(rect.is_geometry_valid_cached(view, 0, Size2i(128, 64), 3));
	CHECK_FALSE(rect.is_geometry_valid_cached(view, 0, Size2i(80, 64), 3));
	CHECK(rect.is_geometry_valid_cached(view, 0, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] The logical source lies inside the picture of the view") {
	const RendererCanvasRender::TexturePageView view = make_view();
	RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	rect.source = Rect2(8, 8, 24, 24);
	CHECK(rect.is_geometry_valid(view, Size2i(128, 64), 3));
	rect.source = Rect2(16, 16, 24, 24);
	CHECK_FALSE(rect.is_geometry_valid(view, Size2i(128, 64), 3));
	rect.source = Rect2(-1, 0, 8, 8);
	CHECK_FALSE(rect.is_geometry_valid(view, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] A source may lie outside the region but not outside the whole picture") {
	RendererCanvasRender::TexturePageView view = make_view();
	view.logical_size = Size2i(64, 48);
	view.logical_offset = Vector2i(8, 4);
	RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	rect.source = Rect2(0, 0, 64, 48);
	CHECK(rect.is_geometry_valid(view, Size2i(128, 64), 3));
	rect.source = Rect2(0, 0, 65, 48);
	CHECK_FALSE(rect.is_geometry_valid(view, Size2i(128, 64), 3));
	rect.source = Rect2(0, 0, 64, 48);
	view.logical_offset = Vector2i(40, 4);
	CHECK_FALSE(rect.is_geometry_valid(view, Size2i(128, 64), 3));
	view.logical_offset = Vector2i(-1, 4);
	CHECK_FALSE(rect.is_geometry_valid(view, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] Page and fallback sources follow the place of the view and its whole picture") {
	RendererCanvasRender::TexturePageView view = make_view();
	Rect2 drawn;
	Rect2 source;
	CHECK(RendererCanvasRender::texture_page_place_on_page(view, Rect2(1, 2, 32, 16), Rect2(4, 8, 16, 8), 0, drawn, source));
	CHECK(drawn == Rect2(1, 2, 32, 16));
	CHECK(source == Rect2(68, 40, 16, 8));
	CHECK(RendererCanvasRender::texture_page_fallback_source(view, Rect2(4, 8, 16, 8), Size2i(16, 16)) == Rect2(2, 4, 8, 4));
	view.logical_size = Size2i(64, 64);
	CHECK(RendererCanvasRender::texture_page_fallback_source(view, Rect2(4, 8, 16, 8), Size2i(16, 16)) == Rect2(1, 2, 4, 2));
	view.logical_size = Size2i();
	CHECK(RendererCanvasRender::texture_page_fallback_source(view, Rect2(4, 8, 16, 8), Size2i(16, 16)) == Rect2());
}

TEST_CASE("[CanvasPageRect] A source partly outside the region keeps the part the region holds, under flips and transposition") {
	// A 64x48 picture whose part from (8, 4) to (40, 36) lies on the page at (64, 32); drawn at twice its size.
	RendererCanvasRender::TexturePageView view = make_view();
	view.logical_size = Size2i(64, 48);
	view.logical_offset = Vector2i(8, 4);
	const Rect2 whole(0, 0, 64, 48);
	Rect2 drawn;
	Rect2 source;

	CHECK(RendererCanvasRender::texture_page_place_on_page(view, Rect2(0, 0, 128, 96), whole, 0, drawn, source));
	CHECK(source == Rect2(64, 32, 32, 32));
	CHECK(drawn.is_equal_approx(Rect2(16, 8, 64, 64)));

	CHECK(RendererCanvasRender::texture_page_place_on_page(view, Rect2(0, 0, 128, 96), whole, RendererCanvasRender::CANVAS_RECT_FLIP_H, drawn, source));
	CHECK(source == Rect2(64, 32, 32, 32));
	CHECK(drawn.is_equal_approx(Rect2(48, 8, 64, 64)));

	CHECK(RendererCanvasRender::texture_page_place_on_page(view, Rect2(0, 0, 128, 96), whole, RendererCanvasRender::CANVAS_RECT_FLIP_V, drawn, source));
	CHECK(drawn.is_equal_approx(Rect2(16, 24, 64, 64)));

	// A transposed rectangle is kept with its size swapped and runs the source x along its y.
	CHECK(RendererCanvasRender::texture_page_place_on_page(view, Rect2(0, 0, 96, 128), whole, RendererCanvasRender::CANVAS_RECT_TRANSPOSE, drawn, source));
	CHECK(source == Rect2(64, 32, 32, 32));
	CHECK(drawn.is_equal_approx(Rect2(8, 16, 64, 64)));
	CHECK(RendererCanvasRender::texture_page_place_on_page(view, Rect2(0, 0, 96, 128), whole, RendererCanvasRender::CANVAS_RECT_TRANSPOSE | RendererCanvasRender::CANVAS_RECT_FLIP_H, drawn, source));
	CHECK(drawn.is_equal_approx(Rect2(24, 16, 64, 64)));

	CHECK(RendererCanvasRender::texture_page_place_on_page(view, Rect2(5, 5, 16, 16), Rect2(10, 6, 8, 8), 0, drawn, source));
	CHECK(drawn == Rect2(5, 5, 16, 16));
	CHECK(source == Rect2(66, 34, 8, 8));

	CHECK(RendererCanvasRender::texture_page_place_on_page(view, Rect2(0, 0, 16, 16), Rect2(36, 30, 8, 8), 0, drawn, source));
	CHECK(source == Rect2(92, 58, 4, 6));
	CHECK(drawn.is_equal_approx(Rect2(0, 0, 8, 12)));

	CHECK_FALSE(RendererCanvasRender::texture_page_place_on_page(view, Rect2(0, 0, 16, 16), Rect2(44, 40, 8, 8), 0, drawn, source));
	CHECK_FALSE(RendererCanvasRender::texture_page_place_on_page(view, Rect2(0, 0, 16, 16), Rect2(0, 0, 8, 4), 0, drawn, source));
}

TEST_CASE("[SceneTree][CanvasPageRect] Page views are created, set and freed through the server") {
	RenderingServer *rs = RenderingServer::get_singleton();
	const RID view = rs->canvas_texture_page_view_create();
	REQUIRE(view.is_valid());
	const RendererCanvasRender::TexturePageView *data = RSG::canvas_render->texture_page_view_owner.get_or_null(view);
	REQUIRE(data != nullptr);
	CHECK(data->serial == 0);
	CHECK(data->page_texture.is_null());

	const RID page = rs->texture_2d_placeholder_create();
	const RID fallback = rs->texture_2d_placeholder_create();
	rs->canvas_texture_page_view_set(view, page, Rect2i(8, 16, 32, 64), 3, 2, 5, fallback, Size2i(48, 80), Vector2i(4, 8));
	CHECK(data->page_texture == page);
	CHECK(data->physical_region == Rect2i(8, 16, 32, 64));
	CHECK(data->logical_size == Size2i(48, 80));
	CHECK(data->logical_offset == Vector2i(4, 8));
	CHECK(data->array_layer == 3);
	CHECK(data->max_region_lod == 2);
	CHECK(data->expected_generation == 5);
	CHECK(data->fallback_texture == fallback);
	const uint64_t first_serial = data->serial;
	CHECK(first_serial != 0);

	const RID other = rs->canvas_texture_page_view_create();
	rs->canvas_texture_page_view_set(other, page, Rect2i(0, 0, 32, 32), 0, 0, 5, RID(), Size2i(32, 32), Vector2i());
	rs->canvas_texture_page_view_set(view, page, Rect2i(40, 16, 32, 64), 3, 2, 5, fallback, Size2i(48, 80), Vector2i(4, 8));
	CHECK(data->physical_region == Rect2i(40, 16, 32, 64));
	CHECK(data->serial > RSG::canvas_render->texture_page_view_owner.get_or_null(other)->serial);
	CHECK(RSG::canvas_render->texture_page_view_owner.get_or_null(other)->serial > first_serial);

	// Showing only the fallback lets the page go and keeps the picture.
	const uint64_t placed_serial = data->serial;
	ERR_PRINT_OFF;
	rs->canvas_texture_page_view_set(view, page, Rect2i(INT32_MAX - 7, 0, 32, 32), 0, 0, 5, fallback, Size2i(32, 32), Vector2i());
	rs->canvas_texture_page_view_set(view, page, Rect2i(0, INT32_MAX - 7, 32, 32), 0, 0, 5, fallback, Size2i(32, 32), Vector2i());
	rs->canvas_texture_page_view_set(view, page, Rect2i(0, 0, 32, 32), 0, 0, 5, fallback, Size2i(32, 32), Vector2i(INT32_MAX, 0));
	rs->canvas_texture_page_view_set(view, page, Rect2i(0, 0, 32, 32), 0, 0, 5, fallback, Size2i(32, 32), Vector2i(0, INT32_MAX));
	rs->canvas_texture_page_view_set(view, page, Rect2i(0, 0, 32, 32), 0, 0, 5, fallback, Size2i(0, 32), Vector2i());
	rs->canvas_texture_page_view_set_fallback(view, fallback, Size2i(-1, 32));
	ERR_PRINT_ON;
	CHECK(data->serial == placed_serial);
	CHECK(data->physical_region == Rect2i(40, 16, 32, 64));
	CHECK(data->logical_offset == Vector2i(4, 8));
	rs->canvas_texture_page_view_set_fallback(view, fallback, Size2i(48, 80));
	CHECK(data->page_texture.is_null());
	CHECK(data->physical_region == Rect2i());
	CHECK(data->expected_generation == 0);
	CHECK(data->logical_size == Size2i(48, 80));
	CHECK(data->logical_offset == Vector2i());
	CHECK(data->fallback_texture == fallback);
	CHECK(data->serial > placed_serial);

	rs->free_rid(view);
	rs->free_rid(other);
	CHECK_FALSE(RSG::canvas_render->texture_page_view_owner.owns(view));
	CHECK_FALSE(RSG::canvas_render->texture_page_view_owner.owns(other));
	ERR_PRINT_OFF;
	rs->canvas_texture_page_view_set(view, page, Rect2i(0, 0, 32, 32), 0, 0, 5, RID(), Size2i(32, 32), Vector2i());
	rs->canvas_texture_page_view_set_fallback(view, fallback, Size2i(32, 32));
	ERR_PRINT_ON;
	rs->free_rid(page);
	rs->free_rid(fallback);
}

TEST_CASE("[SceneTree][CanvasPageRect] Page rectangles keep the view and their logical source") {
	RenderingServer *rs = RenderingServer::get_singleton();
	const RID view = rs->canvas_texture_page_view_create();
	const RID item = rs->canvas_item_create();
	rs->canvas_item_add_texture_page_rect_region(item, Rect2(2, 4, -16, 8), view, Rect2(1, 2, 8, -4), Color(1, 0.5, 0.25), true, false);

	const RendererCanvasCull::Item *data = RSG::canvas->canvas_item_owner.get_or_null(item);
	REQUIRE(data != nullptr);
	REQUIRE(data->commands != nullptr);
	REQUIRE(data->commands->type == RendererCanvasRender::Item::Command::TYPE_TEXTURE_PAGE_RECT);
	const RendererCanvasRender::Item::CommandTexturePageRect *rect = static_cast<const RendererCanvasRender::Item::CommandTexturePageRect *>(data->commands);
	CHECK(rect->view == view);
	CHECK(rect->texture.is_null());
	CHECK(rect->source == Rect2(1, 2, 8, 4));
	CHECK(rect->rect == Rect2(2, 4, 8, 16));
	CHECK(rect->modulate == Color(1, 0.5, 0.25));
	CHECK((rect->flags & RendererCanvasRender::CANVAS_RECT_FLIP_H) != 0);
	CHECK((rect->flags & RendererCanvasRender::CANVAS_RECT_FLIP_V) != 0);
	CHECK((rect->flags & RendererCanvasRender::CANVAS_RECT_TRANSPOSE) != 0);
	CHECK((rect->flags & RendererCanvasRender::CANVAS_RECT_CLIP_UV) == 0);
	CHECK((rect->flags & RendererCanvasRender::CANVAS_RECT_REGION_SAMPLING) != 0);

	rs->free_rid(item);
	rs->free_rid(view);
}

} // namespace TestCanvasPageRect
