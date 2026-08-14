/**************************************************************************/
/*  test_canvas_page_shader.cpp                                           */
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

TEST_FORCE_LINK(test_canvas_page_shader)

#include "servers/rendering/shader_language.h"
#include "servers/rendering/shader_types.h"

namespace TestCanvasPageShader {

static Error compile_canvas_item(const String &p_code) {
	ShaderLanguage language;
	ShaderLanguage::ShaderCompileInfo info;
	info.functions = ShaderTypes::get_singleton()->get_functions(RSE::SHADER_CANVAS_ITEM);
	info.render_modes = ShaderTypes::get_singleton()->get_modes(RSE::SHADER_CANVAS_ITEM);
	info.stencil_modes = ShaderTypes::get_singleton()->get_stencil_modes(RSE::SHADER_CANVAS_ITEM);
	info.shader_types = ShaderTypes::get_singleton()->get_types();
	return language.compile(p_code, info);
}

TEST_CASE("[SceneTree][CanvasPageShader] Fragment and light code read the picture's page cell and region") {
	REQUIRE(ShaderTypes::get_singleton() != nullptr);
	CHECK(compile_canvas_item(R"(
shader_type canvas_item;
void fragment() {
	vec4 region = PAGE_REGION;
	vec2 inside = clamp(UV + vec2(0.01, 0.0), region.xy, region.xy + region.zw);
	COLOR = texture_page(inside) * 0.5 + texture_page_lod(inside, 1.0) * 0.5;
}
void light() {
	LIGHT = texture_page_lod(UV, 0.0) * LIGHT_COLOR;
}
)") == OK);
}

TEST_CASE("[SceneTree][CanvasPageShader] Vertex code cannot read the page and calls must match") {
	ERR_PRINT_OFF;
	CHECK(compile_canvas_item(R"(
shader_type canvas_item;
void vertex() {
	COLOR = texture_page(UV);
}
)") != OK);
	CHECK(compile_canvas_item(R"(
shader_type canvas_item;
void vertex() {
	COLOR = PAGE_REGION;
}
)") != OK);
	CHECK(compile_canvas_item(R"(
shader_type canvas_item;
void fragment() {
	COLOR = texture_page(UV, 1.0);
}
)") != OK);
	ERR_PRINT_ON;
}

} // namespace TestCanvasPageShader
