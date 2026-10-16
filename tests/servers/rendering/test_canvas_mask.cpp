/**************************************************************************/
/*  test_canvas_mask.cpp                                                  */
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

TEST_FORCE_LINK(test_canvas_mask)

#include "servers/rendering/renderer_canvas_cull.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"

#include <cfloat>

namespace TestCanvasMask {

TEST_CASE("[SceneTree][CanvasMask] Overflowing region ends keep the last valid source") {
	RenderingServer *rs = RS::get_singleton();
	RID mask = rs->canvas_mask_create();
	const Rect2 region(4, 8, 16, 32);
	rs->canvas_mask_set_source(mask, 2, region, 3);
	const RendererCanvasCull::Mask *state = RSG::canvas->canvas_mask_owner.get_or_null(mask);
	REQUIRE(state != nullptr);
	for (const Rect2 &invalid : { Rect2(FLT_MAX, 0, FLT_MAX, 20), Rect2(0, FLT_MAX, 20, FLT_MAX) }) {
		ERR_PRINT_OFF;
		rs->canvas_mask_set_source(mask, 5, invalid, 6);
		ERR_PRINT_ON;
		CHECK(state->region == region);
		CHECK(state->layer == 2);
		CHECK(state->max_lod == 3);
	}
	rs->free_rid(mask);
}

} // namespace TestCanvasMask
