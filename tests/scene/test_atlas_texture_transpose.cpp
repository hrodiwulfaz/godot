/**************************************************************************/
/*  test_atlas_texture_transpose.cpp                                      */
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

TEST_FORCE_LINK(test_atlas_texture_transpose)

#include "core/io/image.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/canvas_texture_page_view.h"
#include "scene/resources/drawable_texture_2d.h"
#include "servers/rendering/rendering_server.h"
#include "scene/resources/image_texture.h"

namespace TestAtlasTextureTranspose {

// Where the canvas renderer puts a source point of a rectangle drawn at p_rect (size before the transpose swap,
// negative sizes flip), from the vertex and UV formulas of the canvas shaders: a destination point q in [0, 1]
// samples source (transpose ? m(q).yx : m(q)), where m flips x for a negative width and y for a negative height.
static Vector2 renderer_destination(const Rect2 &p_rect, const Vector2 &p_source_size, bool p_transpose, const Vector2 &p_source_point) {
	const bool flip_h = p_rect.size.x < 0;
	const bool flip_v = p_rect.size.y < 0;
	const Vector2 size = p_rect.size.abs();
	const Vector2 drawn_size = p_transpose ? Vector2(size.y, size.x) : size;
	const Vector2 normalized = p_source_point / p_source_size;
	Vector2 mapped = p_transpose ? Vector2(normalized.y, normalized.x) : normalized;
	if (flip_h) {
		mapped.x = 1.0 - mapped.x;
	}
	if (flip_v) {
		mapped.y = 1.0 - mapped.y;
	}
	return p_rect.position + drawn_size * mapped;
}

static Rect2 renderer_rect_of(const Rect2 &p_rect, const Vector2 &p_source_size, bool p_transpose, const Rect2 &p_source) {
	Rect2 result(renderer_destination(p_rect, p_source_size, p_transpose, p_source.position), Size2());
	result.expand_to(renderer_destination(p_rect, p_source_size, p_transpose, p_source.position + Vector2(p_source.size.x, 0)));
	result.expand_to(renderer_destination(p_rect, p_source_size, p_transpose, p_source.position + Vector2(0, p_source.size.y)));
	result.expand_to(renderer_destination(p_rect, p_source_size, p_transpose, p_source.get_end()));
	return result;
}

TEST_CASE("[SceneTree][CanvasTexturePageView] A source beyond the logical canvas is clipped in place under all eight flag sets") {
	Ref<DrawableTexture2D> page;
	page.instantiate();
	REQUIRE(page->setup_checked(64, 32, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), false) == OK);
	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(page->get_rid());
	Ref<CanvasTexturePageView> view;
	view.instantiate();
	REQUIRE(view->configure(page, Rect2i(8, 4, 24, 16), 0, 0, generation, Ref<Texture2D>()) == OK);
	const Rect2 logical(Vector2(), Vector2(24, 16));
	const Rect2 requested(-6, 10, 20, 12);
	const Rect2 visible = logical.intersection(requested);
	for (int flags = 0; flags < 8; flags++) {
		const bool transpose = flags & 1;
		const Rect2 destination(Vector2(-3, 7), Vector2((flags & 2) ? -40 : 40, (flags & 4) ? -36 : 36));
		Rect2 drawn;
		Rect2 source;
		REQUIRE(view->get_rect_region(destination, requested, transpose, drawn, source));
		CHECK_MESSAGE(source == visible, vformat("flags %d", flags));
		const Rect2 expected = renderer_rect_of(destination, requested.size, transpose, Rect2(visible.position - requested.position, visible.size));
		const Rect2 actual = renderer_rect_of(drawn, source.size, transpose, Rect2(Vector2(), source.size));
		CHECK_MESSAGE(actual.position.is_equal_approx(expected.position), vformat("flags %d: %s != %s", flags, actual, expected));
		CHECK_MESSAGE(actual.size.is_equal_approx(expected.size), vformat("flags %d: %s != %s", flags, actual, expected));
	}
}

} // namespace TestAtlasTextureTranspose
