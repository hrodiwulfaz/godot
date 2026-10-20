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

TEST_CASE("[AtlasTexture] Uneven margins land in place under all eight flag sets") {
	// A 128x128 logical canvas whose art is a 64x32 crop at (48, 16), stored at (100, 50) of a larger atlas.
	Ref<Image> image = Image::create_empty(256, 128, false, Image::FORMAT_RGBA8);
	Ref<ImageTexture> atlas = ImageTexture::create_from_image(image);
	Ref<AtlasTexture> texture;
	texture.instantiate();
	texture->set_atlas(atlas);
	texture->set_region(Rect2(100, 50, 64, 32));
	texture->set_margin(Rect2(48, 16, 64, 96));
	REQUIRE(texture->get_size() == Size2(128, 128));

	const Vector2 canvas(128, 128);
	const Rect2 crop(48, 16, 64, 32);
	for (int flags = 0; flags < 8; flags++) {
		const bool transpose = flags & 1;
		const bool flip_h = flags & 2;
		const bool flip_v = flags & 4;
		const Rect2 full(Vector2(10, 20), Vector2(flip_h ? -128 : 128, flip_v ? -128 : 128));
		Rect2 drawn;
		Rect2 source;
		REQUIRE(texture->get_rect_region(full, Rect2(Vector2(), canvas), transpose, drawn, source));
		CHECK_MESSAGE(source == Rect2(100, 50, 64, 32), vformat("flags %d", flags));
		CHECK_MESSAGE((drawn.size.x < 0) == flip_h, vformat("flags %d", flags));
		CHECK_MESSAGE((drawn.size.y < 0) == flip_v, vformat("flags %d", flags));
		const Rect2 expected = renderer_rect_of(full, canvas, transpose, crop);
		const Rect2 actual = renderer_rect_of(drawn, crop.size, transpose, Rect2(Vector2(), crop.size));
		CHECK_MESSAGE(actual.position.is_equal_approx(expected.position), vformat("flags %d: %s != %s", flags, actual, expected));
		CHECK_MESSAGE(actual.size.is_equal_approx(expected.size), vformat("flags %d: %s != %s", flags, actual, expected));
	}
}

TEST_CASE("[AtlasTexture] Scaled transposed draws keep the crop in place") {
	Ref<Image> image = Image::create_empty(64, 64, false, Image::FORMAT_RGBA8);
	Ref<AtlasTexture> texture;
	texture.instantiate();
	texture->set_atlas(ImageTexture::create_from_image(image));
	texture->set_region(Rect2(0, 0, 20, 10));
	texture->set_margin(Rect2(5, 30, 25, 50));
	const Vector2 canvas = texture->get_size();
	const Rect2 crop(5, 30, 20, 10);
	for (int flags = 0; flags < 8; flags++) {
		const bool transpose = flags & 1;
		const Rect2 full(Vector2(-7, 3), Vector2((flags & 2) ? -90 : 90, (flags & 4) ? -180 : 180));
		Rect2 drawn;
		Rect2 source;
		REQUIRE(texture->get_rect_region(full, Rect2(Vector2(), canvas), transpose, drawn, source));
		const Rect2 expected = renderer_rect_of(full, canvas, transpose, crop);
		const Rect2 actual = renderer_rect_of(drawn, crop.size, transpose, Rect2(Vector2(), crop.size));
		CHECK_MESSAGE(actual.position.is_equal_approx(expected.position), vformat("flags %d: %s != %s", flags, actual, expected));
		CHECK_MESSAGE(actual.size.is_equal_approx(expected.size), vformat("flags %d: %s != %s", flags, actual, expected));
	}
}

TEST_CASE("[AtlasTexture] A clipped source region lands in place under all eight flag sets") {
	Ref<Image> image = Image::create_empty(256, 128, false, Image::FORMAT_RGBA8);
	Ref<AtlasTexture> texture;
	texture.instantiate();
	texture->set_atlas(ImageTexture::create_from_image(image));
	texture->set_region(Rect2(100, 50, 64, 32));
	texture->set_margin(Rect2(48, 16, 64, 96));
	const Rect2 crop(48, 16, 64, 32);
	// The requested source covers the left part of the crop and empty canvas above and to the left of it.
	const Rect2 requested(30, 4, 50, 40);
	const Rect2 visible = crop.intersection(requested);
	for (int flags = 0; flags < 8; flags++) {
		const bool transpose = flags & 1;
		const Rect2 destination(Vector2(5, -9), Vector2((flags & 2) ? -100 : 100, (flags & 4) ? -80 : 80));
		Rect2 drawn;
		Rect2 source;
		REQUIRE(texture->get_rect_region(destination, requested, transpose, drawn, source));
		CHECK_MESSAGE(source.size.is_equal_approx(visible.size), vformat("flags %d", flags));
		const Rect2 expected = renderer_rect_of(destination, requested.size, transpose, Rect2(visible.position - requested.position, visible.size));
		const Rect2 actual = renderer_rect_of(drawn, source.size, transpose, Rect2(Vector2(), source.size));
		CHECK_MESSAGE(actual.position.is_equal_approx(expected.position), vformat("flags %d: %s != %s", flags, actual, expected));
		CHECK_MESSAGE(actual.size.is_equal_approx(expected.size), vformat("flags %d: %s != %s", flags, actual, expected));
	}
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

TEST_CASE("[SceneTree][CanvasTexturePageView] A trimmed atlas over a page view keeps its crop in place under all eight flag sets") {
	Ref<DrawableTexture2D> page;
	page.instantiate();
	REQUIRE(page->setup_checked(128, 64, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(page->get_rid());
	Ref<CanvasTexturePageView> view;
	view.instantiate();
	REQUIRE(view->configure(page, Rect2i(32, 16, 64, 32), 0, 2, generation, Ref<Texture2D>()) == OK);
	// A 40x70 logical picture whose art is a 22x12 crop at (6, 50), stored at (30, 4) of the page view.
	Ref<AtlasTexture> texture;
	texture.instantiate();
	texture->set_atlas(view);
	texture->set_region(Rect2(30, 4, 22, 12));
	texture->set_margin(Rect2(6, 50, 18, 58));
	REQUIRE(texture->get_size() == Size2(40, 70));
	const Vector2 canvas(40, 70);
	const Rect2 crop(6, 50, 22, 12);
	for (int flags = 0; flags < 8; flags++) {
		const bool transpose = flags & 1;
		const Rect2 full(Vector2(11, -4), Vector2((flags & 2) ? -80 : 80, (flags & 4) ? -140 : 140));
		Rect2 atlas_drawn;
		Rect2 atlas_source;
		REQUIRE(texture->get_rect_region(full, Rect2(Vector2(), canvas), transpose, atlas_drawn, atlas_source));
		Rect2 drawn;
		Rect2 source;
		REQUIRE(view->get_rect_region(atlas_drawn, atlas_source, transpose, drawn, source));
		CHECK_MESSAGE(source == Rect2(30, 4, 22, 12), vformat("flags %d", flags));
		const Rect2 expected = renderer_rect_of(full, canvas, transpose, crop);
		const Rect2 actual = renderer_rect_of(drawn, source.size, transpose, Rect2(Vector2(), source.size));
		CHECK_MESSAGE(actual.position.is_equal_approx(expected.position), vformat("flags %d: %s != %s", flags, actual, expected));
		CHECK_MESSAGE(actual.size.is_equal_approx(expected.size), vformat("flags %d: %s != %s", flags, actual, expected));
	}
}

} // namespace TestAtlasTextureTranspose
