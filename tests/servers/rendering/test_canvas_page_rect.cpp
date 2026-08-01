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

#include "servers/rendering/renderer_canvas_render.h"

namespace TestCanvasPageRect {

static RendererCanvasRender::Item::CommandTexturePageRect make_page_rect() {
	RendererCanvasRender::Item::CommandTexturePageRect rect;
	rect.flags = RendererCanvasRender::CANVAS_RECT_REGION | RendererCanvasRender::CANVAS_RECT_REGION_SAMPLING;
	rect.rect = Rect2(0, 0, 32, 32);
	rect.source = Rect2(64, 32, 32, 32);
	rect.sampler_domain = Rect2(64, 32, 32, 32);
	rect.max_region_lod = 2;
	rect.expected_generation = 7;
	return rect;
}

TEST_CASE("[CanvasPageRect] The kept geometry check matches a fresh check for the same page") {
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	CHECK(rect.is_geometry_valid(Size2i(128, 64), 3));
	CHECK(rect.is_geometry_valid_cached(7, Size2i(128, 64), 3));
	CHECK(rect.is_geometry_valid_cached(7, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] A size override without a new generation checks again") {
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	REQUIRE(rect.is_geometry_valid_cached(7, Size2i(128, 64), 3));
	CHECK_FALSE(rect.is_geometry_valid_cached(7, Size2i(80, 64), 3));
	CHECK(rect.is_geometry_valid_cached(7, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] A mip count change checks again") {
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	REQUIRE(rect.is_geometry_valid_cached(7, Size2i(128, 64), 3));
	CHECK_FALSE(rect.is_geometry_valid_cached(7, Size2i(128, 64), 2));
	CHECK(rect.is_geometry_valid_cached(7, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] A new page generation checks again") {
	RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	REQUIRE(rect.is_geometry_valid_cached(7, Size2i(128, 64), 3));
	rect.max_region_lod = 3;
	CHECK(rect.is_geometry_valid_cached(7, Size2i(128, 64), 3));
	CHECK_FALSE(rect.is_geometry_valid_cached(8, Size2i(128, 64), 3));
}

TEST_CASE("[CanvasPageRect] A page without a generation is never kept") {
	const RendererCanvasRender::Item::CommandTexturePageRect rect = make_page_rect();
	CHECK(rect.is_geometry_valid_cached(0, Size2i(128, 64), 3));
	CHECK_FALSE(rect.is_geometry_valid_cached(0, Size2i(80, 64), 3));
	CHECK(rect.is_geometry_valid_cached(0, Size2i(128, 64), 3));
}

} // namespace TestCanvasPageRect
