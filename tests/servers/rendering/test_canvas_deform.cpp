/**************************************************************************/
/*  test_canvas_deform.cpp                                                */
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

TEST_FORCE_LINK(test_canvas_deform)

#include "servers/rendering/renderer_canvas_cull.h"
#include "servers/rendering/renderer_canvas_render.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"

#include <cfloat>

namespace TestCanvasDeform {

using Render = RendererCanvasRender;

// One joint: pivot, axis, blend, segment.
static Vector<float> joint(const Vector2 &p_pivot, const Vector2 &p_axis = Vector2(0, 1), float p_blend = 2.0f, float p_segment = 0.0f) {
	Vector<float> values;
	values.push_back(p_pivot.x);
	values.push_back(p_pivot.y);
	values.push_back(p_axis.x);
	values.push_back(p_axis.y);
	values.push_back(p_blend);
	values.push_back(p_segment);
	return values;
}

static Vector<float> values(std::initializer_list<float> p_values) {
	Vector<float> result;
	for (float value : p_values) {
		result.push_back(value);
	}
	return result;
}

// A canvas of rectangle items culled through the real cull walk; the canvas renderer of the test server draws nothing.
struct Scene {
	RenderingServer *rs = RS::get_singleton();
	RID canvas;
	LocalVector<RID> items;

	Scene() {
		canvas = rs->canvas_create();
	}

	~Scene() {
		for (RID mask : masks) {
			rs->free_rid(mask);
		}
		for (int64_t i = int64_t(items.size()) - 1; i >= 0; i--) {
			rs->free_rid(items[i]);
		}
		rs->free_rid(canvas);
	}

	RID add(RID p_parent, const Rect2 &p_rect, const Transform2D &p_transform = Transform2D()) {
		RID item = rs->canvas_item_create();
		rs->canvas_item_set_parent(item, p_parent.is_valid() ? p_parent : canvas);
		rs->canvas_item_set_transform(item, p_transform);
		rs->canvas_item_add_rect(item, p_rect, Color(1, 1, 1));
		items.push_back(item);
		return item;
	}

	RendererCanvasCull::Item *item(RID p_item) const {
		return RSG::canvas->canvas_item_owner.get_or_null(p_item);
	}

	// Sets the joints and turns each by p_angle, so the record moves something and is written.
	void bend(RID p_item, const Vector<float> &p_joints, float p_angle = 0.25f) {
		rs->canvas_item_set_deform_joints(p_item, p_joints);
		Vector<float> angles;
		angles.resize(p_joints.size() / 6);
		angles.fill(p_angle);
		rs->canvas_item_set_deform_angles(p_item, angles);
	}

	void render(const Transform2D &p_camera = Transform2D(), const Rect2 &p_view = Rect2(-1000, -1000, 2000, 2000)) {
		RendererCanvasCull::Canvas *canvas_data = RSG::canvas->canvas_owner.get_or_null(canvas);
		for (RID rid : items) {
			item(rid)->global_rect_cache = Rect2(-12345, -12345, 0, 0);
		}
		RSG::canvas->render_canvas(RID(), canvas_data, p_camera, nullptr, nullptr, p_view, RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED, false, false, 0xFFFFFFFF);
	}

	bool drawn(RID p_item) const {
		return item(p_item)->global_rect_cache != Rect2(-12345, -12345, 0, 0);
	}

	uint32_t chain(RID p_item) const {
		return item(p_item)->deform & Render::DEFORM_CHAIN_MASK;
	}

	// Record ids of a chain, innermost first.
	Vector<uint32_t> records(RID p_item) const {
		Vector<uint32_t> result;
		const uint32_t *row = RSG::canvas->deform_chain_rows.ptr() + chain(p_item) * 4;
		for (int i = 0; i < 4 && (row[i] & Render::DEFORM_RECORD_MASK) != 0; i++) {
			result.push_back(row[i] & Render::DEFORM_RECORD_MASK);
		}
		return result;
	}

	// A mask placed by p_owner over the owner's rectangle (0, 0) - (20, 60), added to p_item.
	RID add_mask(RID p_owner, RID p_item) {
		RID mask = rs->canvas_mask_create();
		rs->canvas_mask_set_owner(mask, p_owner);
		rs->canvas_mask_set_transform(mask, Transform2D(Vector2(20, 0), Vector2(0, 60), Vector2()));
		rs->canvas_mask_set_source(mask, 0, Rect2(0, 0, 32, 32), 0);
		rs->canvas_item_add_mask(p_item, mask);
		masks.push_back(mask);
		return mask;
	}

	// Follow code of p_mask in the mask set of p_item; mask slots are sorted by mask entry.
	uint32_t follow_code(RID p_item, RID p_mask) const {
		const uint16_t entry = RSG::canvas->canvas_mask_owner.get_or_null(p_mask)->entry;
		const RendererCanvasCull::MaskSet &set = RSG::canvas->mask_sets[item(p_item)->mask_set];
		for (uint32_t slot = 0; slot < 8; slot++) {
			if (set.entries[slot] == entry) {
				return RSG::canvas_render->deform_follow_code(chain(p_item), slot);
			}
		}
		return 99;
	}

	LocalVector<RID> masks;
};

TEST_CASE("[SceneTree][CanvasDeform] Joints, angles and stretch are set, checked and cleared") {
	Scene scene;
	RID item = scene.add(RID(), Rect2(0, 0, 10, 40));
	const uint32_t states = RSG::canvas->deform_state_count;

	Vector<float> two = joint(Vector2(5, 10));
	two.append_array(joint(Vector2(5, 25), Vector2(0, 2)));
	scene.rs->canvas_item_set_deform_joints(item, two);
	RendererCanvasCull::Item::DeformState *state = scene.item(item)->deform_state;
	REQUIRE(state != nullptr);
	CHECK(RSG::canvas->deform_state_count == states + 1);
	CHECK(state->joints.size() == 16);
	CHECK(state->joints[8 + 3] == doctest::Approx(1.0f));
	CHECK(state->joints[4] == 0.0f);
	CHECK(state->joints[6] == 1.0f);

	scene.rs->canvas_item_set_deform_angles(item, values({ 0.5f, -0.25f }));
	scene.rs->canvas_item_set_deform_stretch(item, values({ 1.0f, 0.5f }));
	CHECK(state->joints[4] == 0.5f);
	CHECK(state->joints[8 + 4] == -0.25f);
	CHECK(state->joints[8 + 6] == 0.5f);

	ERR_PRINT_OFF;
	scene.rs->canvas_item_set_deform_angles(item, values({ 1.0f }));
	scene.rs->canvas_item_set_deform_stretch(item, values({ 1.0f, 0.0f }));
	ERR_PRINT_ON;
	CHECK(state->joints[4] == 0.5f);
	CHECK(state->joints[8 + 6] == 0.5f);

	scene.rs->canvas_item_set_deform_joints(item, Vector<float>());
	CHECK(scene.item(item)->deform_state == nullptr);
	CHECK(RSG::canvas->deform_state_count == states);
}

TEST_CASE("[SceneTree][CanvasDeform] Malformed joints and warps are rejected") {
	Scene scene;
	RID item = scene.add(RID(), Rect2(0, 0, 10, 40));
	ERR_PRINT_OFF;
	scene.rs->canvas_item_set_deform_angles(item, values({ 1.0f }));
	CHECK(scene.item(item)->deform_state == nullptr);

	Vector<float> nine;
	for (int i = 0; i < 9; i++) {
		nine.append_array(joint(Vector2(0, i)));
	}
	scene.rs->canvas_item_set_deform_joints(item, nine);
	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(), Vector2()));
	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(), Vector2(0, 1), -1.0f));
	scene.rs->canvas_item_set_deform_joints(item, values({ 1, 2, 3, 4, 5 }));
	CHECK(scene.item(item)->deform_state == nullptr);

	Vector<Vector2> four;
	four.resize(4);
	scene.rs->canvas_item_set_deform_warp(item, 1, 4, Rect2(0, 0, 10, 10), four, 1.0f);
	scene.rs->canvas_item_set_deform_warp(item, 17, 2, Rect2(0, 0, 10, 10), four, 1.0f);
	scene.rs->canvas_item_set_deform_warp(item, 2, 3, Rect2(0, 0, 10, 10), four, 1.0f);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, 0, 0, 10), four, 1.0f);
	ERR_PRINT_ON;
	CHECK(scene.item(item)->deform_state == nullptr);

	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, 0, 10, 10), four, 1.0f);
	REQUIRE(scene.item(item)->deform_state != nullptr);
	CHECK(scene.item(item)->deform_state->warp_columns == 2);
	scene.rs->canvas_item_set_deform_warp(item, 0, 0, Rect2(), Vector<Vector2>(), 0.0f);
	CHECK(scene.item(item)->deform_state == nullptr);
}

TEST_CASE("[SceneTree][CanvasDeform] Non-finite values are rejected and the last valid record stays") {
	Scene scene;
	RID item = scene.add(RID(), Rect2(0, 0, 10, 40));
	const float nan = float(Math::NaN);
	const float inf = float(Math::INF);

	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(5, 10), Vector2(0, 1), 2.0f, 4.0f));
	scene.rs->canvas_item_set_deform_angles(item, values({ 0.5f }));
	scene.rs->canvas_item_set_deform_stretch(item, values({ 1.5f }));
	Vector<Vector2> offsets;
	offsets.resize(4);
	offsets.write[3] = Vector2(1, 2);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, 0, 10, 10), offsets, 0.5f);
	RendererCanvasCull::Item::DeformState *state = scene.item(item)->deform_state;
	REQUIRE(state != nullptr);
	Vector<float> joints;
	for (float value : state->joints) {
		joints.push_back(value);
	}

	ERR_PRINT_OFF;
	for (int value = 0; value < 6; value++) {
		for (float bad : { nan, inf, -inf }) {
			Vector<float> broken = joint(Vector2(1, 2), Vector2(0, 1), 3.0f, 0.0f);
			broken.write[value] = bad;
			scene.rs->canvas_item_set_deform_joints(item, broken);
		}
	}
	scene.rs->canvas_item_set_deform_angles(item, values({ nan }));
	scene.rs->canvas_item_set_deform_angles(item, values({ inf }));
	scene.rs->canvas_item_set_deform_stretch(item, values({ nan }));
	scene.rs->canvas_item_set_deform_stretch(item, values({ inf }));
	Vector<Vector2> broken_offsets = offsets;
	broken_offsets.write[1] = Vector2(nan, 0);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, 0, 20, 20), broken_offsets, 1.0f);
	broken_offsets.write[1] = Vector2(0, -inf);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, 0, 20, 20), broken_offsets, 1.0f);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, 0, 20, 20), offsets, nan);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, 0, 20, 20), offsets, inf);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(nan, 0, 20, 20), offsets, 1.0f);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, 0, inf, 20), offsets, 1.0f);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(FLT_MAX, 0, FLT_MAX, 20), offsets, 1.0f);
	scene.rs->canvas_item_set_deform_warp(item, 2, 2, Rect2(0, FLT_MAX, 20, FLT_MAX), offsets, 1.0f);
	ERR_PRINT_ON;

	REQUIRE(scene.item(item)->deform_state == state);
	REQUIRE(state->joints.size() == uint32_t(joints.size()));
	for (int i = 0; i < joints.size(); i++) {
		CHECK(state->joints[i] == joints[i]);
	}
	CHECK(state->joints[4] == 0.5f);
	CHECK(state->joints[6] == 1.5f);
	CHECK(state->warp_rect == Rect2(0, 0, 10, 10));
	CHECK(state->warp_strength == 0.5f);
	CHECK(state->warp_offsets[1] == Vector2());
	CHECK(state->warp_offsets[3] == Vector2(1, 2));
}

TEST_CASE("[SceneTree][CanvasDeform] Records pass down the canvas tree and stack with the children's own") {
	Scene scene;
	RID torso = scene.add(RID(), Rect2(0, 0, 20, 40));
	RID leg = scene.add(torso, Rect2(0, 30, 10, 40));
	RID foot = scene.add(leg, Rect2(0, 65, 10, 8));
	RID boot = scene.add(foot, Rect2(0, 65, 10, 10));
	scene.bend(torso, joint(Vector2(10, 35)));
	scene.bend(leg, joint(Vector2(5, 50)));
	scene.render();

	REQUIRE(scene.chain(torso) != 0);
	CHECK(scene.records(torso).size() == 1);
	REQUIRE(scene.records(leg).size() == 2);
	CHECK(scene.records(leg)[1] == scene.records(torso)[0]);
	CHECK(scene.chain(foot) == scene.chain(leg));
	CHECK(scene.chain(boot) == scene.chain(leg));
	// Torso and leg wrote one record each.
	CHECK(RSG::canvas->deform_record_rows.size() == 2 * (4 + 2) * 4);
	CHECK(RSG::canvas_render->deform_data.record_base == RSG::canvas->deform_chain_rows.size() / 4);
}

TEST_CASE("[SceneTree][CanvasDeform] Tip ordering includes diagonal joints and inherited transforms") {
	for (int x : { -1, 1 }) {
		for (int y : { -1, 1 }) {
			Scene scene;
			const Rect2 rect(0, 0, 200, 200);
			const Transform2D world(0.4f, Vector2(20, 30));
			const RID owner = scene.add(RID(), rect, world);
			const RID child = scene.add(owner, rect);
			scene.bend(owner, joint(Vector2(100, 100), Vector2(x, y)), Math::PI * 0.999f);
			scene.rs->canvas_item_set_deform_flags(owner, RSE::CANVAS_DEFORM_FLAG_TIP_UNDER);
			scene.render();
			const uint32_t word = scene.item(child)->deform;
			for (int flip : { -1, 1 }) {
				const uint32_t ordered = RSG::canvas_render->deform_rect_order(word, world, rect, Vector2(flip, flip), false);
				CHECK(bool(ordered & Render::DEFORM_GRID_REVERSE_ROWS) == (y * flip > 0));
				CHECK(bool(ordered & Render::DEFORM_GRID_REVERSE_COLUMNS) == (x * flip > 0));
			}
			CHECK(RSG::canvas_render->deform_rect_order(word & ~Render::DEFORM_TIP_UNDER, world, rect, Vector2(1, 1), false) == 0);
		}
	}
}

TEST_CASE("[SceneTree][CanvasDeform] Tip ordering includes warp-only folds in either axis") {
	for (bool across : { false, true }) {
		for (bool toward_start : { false, true }) {
			Scene scene;
			const Rect2 rect(0, 0, across ? 200 : 20, across ? 20 : 200);
			const RID item = scene.add(RID(), rect);
			const int columns = across ? 3 : 2;
			const int height = across ? 2 : 3;
			Vector<Vector2> offsets;
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < columns; x++) {
					const bool tip = (across ? x : y) == (toward_start ? 0 : 2);
					offsets.push_back(tip ? (across ? Vector2(200, 0) : Vector2(0, 200)) * (toward_start ? 1 : -1) : Vector2());
				}
			}
			scene.rs->canvas_item_set_deform_warp(item, columns, height, rect, offsets, 1.0f);
			scene.rs->canvas_item_set_deform_flags(item, RSE::CANVAS_DEFORM_FLAG_TIP_UNDER);
			scene.render();
			const uint32_t ordered = RSG::canvas_render->deform_rect_order(scene.item(item)->deform, Transform2D(), rect, Vector2(-1, -1), true);
			CHECK(bool(ordered & Render::DEFORM_GRID_REVERSE_ROWS) == (!across && !toward_start));
			CHECK(bool(ordered & Render::DEFORM_GRID_REVERSE_COLUMNS) == (across && !toward_start));
		}
	}
}

TEST_CASE("[CanvasDeform] Grid index orders preserve every vertex and cell edge") {
	const uint32_t columns[] = { Render::DEFORM_GRID_COLUMNS, Render::DEFORM_GRID_COLUMNS, Render::DEFORM_WARP_GRID_COLUMNS };
	const uint32_t rows[] = { Render::DEFORM_GRID_ROWS, Render::DEFORM_DENSE_GRID_ROWS, Render::DEFORM_WARP_GRID_ROWS };
	for (uint32_t grid = 0; grid < Render::DEFORM_GRID_KIND_COUNT; grid++) {
		const uint32_t stride = columns[grid] + 1;
		const uint32_t vertex_count = stride * (rows[grid] + 1);
		const uint32_t first[] = { 0, rows[grid] * stride, columns[grid], vertex_count - 1 };
		for (uint32_t order = 0; order < Render::DEFORM_GRID_ORDER_COUNT; order++) {
			HashSet<uint32_t> seen;
			CHECK(Render::deform_grid_vertex(0, 0, columns[grid], rows[grid], order) == first[order]);
			CHECK(Render::deform_grid_vertex(columns[grid], rows[grid], columns[grid], rows[grid], order) == first[3 - order]);
			for (uint32_t y = 0; y <= rows[grid]; y++) {
				for (uint32_t x = 0; x <= columns[grid]; x++) {
					const uint32_t vertex = Render::deform_grid_vertex(x, y, columns[grid], rows[grid], order);
					CHECK(vertex < vertex_count);
					CHECK_FALSE(seen.has(vertex));
					seen.insert(vertex);
					if (x < columns[grid]) {
						const int next = Render::deform_grid_vertex(x + 1, y, columns[grid], rows[grid], order);
						CHECK(Math::abs(next - int(vertex)) == 1);
					}
					if (y < rows[grid]) {
						const int next = Render::deform_grid_vertex(x, y + 1, columns[grid], rows[grid], order);
						CHECK(Math::abs(next - int(vertex)) == int(stride));
					}
				}
			}
			CHECK(seen.size() == vertex_count);
		}
	}
}

TEST_CASE("[SceneTree][CanvasDeform] An excluded item and its subtree drop inherited records") {
	Scene scene;
	RID torso = scene.add(RID(), Rect2(0, 0, 20, 40));
	RID arm = scene.add(torso, Rect2(0, 0, 6, 30));
	RID hand = scene.add(arm, Rect2(0, 30, 6, 6));
	RID forearm = scene.add(torso, Rect2(14, 0, 6, 30));
	scene.bend(torso, joint(Vector2(10, 35)));
	scene.rs->canvas_item_set_deform_excluded(arm, true);
	scene.rs->canvas_item_set_deform_excluded(forearm, true);
	scene.bend(forearm, joint(Vector2(17, 15)));
	scene.render();

	CHECK(scene.chain(torso) != 0);
	CHECK(scene.chain(arm) == 0);
	CHECK(scene.chain(hand) == 0);
	REQUIRE(scene.records(forearm).size() == 1);
	CHECK(scene.records(forearm)[0] != scene.records(torso)[0]);
}

TEST_CASE("[SceneTree][CanvasDeform] Y-sorted children inherit the record of their parent") {
	Scene scene;
	RID body = scene.add(RID(), Rect2(0, 0, 20, 40));
	scene.rs->canvas_item_set_sort_children_by_y(body, true);
	RID back_leg = scene.add(body, Rect2(0, 30, 10, 40), Transform2D(0, Vector2(0, -5)));
	RID front_leg = scene.add(body, Rect2(10, 30, 10, 40), Transform2D(0, Vector2(0, 5)));
	RID foot = scene.add(front_leg, Rect2(10, 65, 10, 8));
	scene.bend(body, joint(Vector2(10, 35)));
	scene.bend(front_leg, joint(Vector2(15, 50)));
	scene.render();

	REQUIRE(scene.records(body).size() == 1);
	CHECK(scene.chain(back_leg) == scene.chain(body));
	REQUIRE(scene.records(front_leg).size() == 2);
	CHECK(scene.records(front_leg)[1] == scene.records(body)[0]);
	CHECK(scene.chain(foot) == scene.chain(front_leg));
}

TEST_CASE("[SceneTree][CanvasDeform] A fifth nested record drops the outermost") {
	Scene scene;
	RID parent;
	LocalVector<RID> owners;
	for (int i = 0; i < 5; i++) {
		parent = scene.add(parent, Rect2(0, i * 10, 10, 10));
		scene.bend(parent, joint(Vector2(5, i * 10 + 5)));
		owners.push_back(parent);
	}
	scene.render();

	REQUIRE(scene.records(owners[4]).size() == 4);
	CHECK(scene.records(owners[4])[3] == scene.records(owners[1])[0]);
	CHECK(scene.records(owners[3])[3] == scene.records(owners[0])[0]);
}

TEST_CASE("[SceneTree][CanvasDeform] Only visible items write records") {
	Scene scene;
	RID shown = scene.add(RID(), Rect2(0, 0, 10, 10));
	RID hidden = scene.add(RID(), Rect2(0, 0, 10, 10));
	scene.bend(shown, joint(Vector2(5, 5)));
	scene.bend(hidden, joint(Vector2(5, 5)));
	scene.rs->canvas_item_set_visible(hidden, false);
	scene.render();

	CHECK(scene.chain(shown) != 0);
	CHECK(RSG::canvas->deform_record_rows.size() == (4 + 2) * 4);
}

TEST_CASE("[SceneTree][CanvasDeform] Flags pass down with the record and a child can set its own") {
	Scene scene;
	RID leg = scene.add(RID(), Rect2(0, 0, 10, 40));
	RID pant = scene.add(leg, Rect2(0, 0, 10, 40));
	RID sock = scene.add(leg, Rect2(0, 30, 10, 10));
	RID lone = scene.add(RID(), Rect2(0, 0, 10, 10));
	scene.bend(leg, joint(Vector2(5, 20)));
	scene.rs->canvas_item_set_deform_flags(leg, RSE::CANVAS_DEFORM_FLAG_TIP_UNDER);
	scene.rs->canvas_item_set_deform_flags(sock, RSE::CANVAS_DEFORM_FLAG_DENSE_GRID);
	scene.rs->canvas_item_set_deform_flags(lone, RSE::CANVAS_DEFORM_FLAG_DENSE_GRID);
	scene.render();

	CHECK((scene.item(leg)->deform & Render::DEFORM_TIP_UNDER) != 0);
	CHECK((scene.item(pant)->deform & Render::DEFORM_TIP_UNDER) != 0);
	CHECK((scene.item(sock)->deform & Render::DEFORM_TIP_UNDER) == 0);
	CHECK((scene.item(sock)->deform & Render::DEFORM_DENSE_GRID) != 0);
	CHECK(scene.chain(sock) == scene.chain(leg));
	CHECK(scene.item(lone)->deform == 0);
}

TEST_CASE("[SceneTree][CanvasDeform] A bent child at the view edge is not culled") {
	Scene scene;
	RID upper = scene.add(RID(), Rect2(0, 0, 10, 50));
	// Straight, the lower part lies right of the view; the joint turns it down into the view.
	RID lower = scene.add(upper, Rect2(60, 0, 40, 10));
	scene.rs->canvas_item_set_deform_joints(upper, joint(Vector2(5, 5), Vector2(1, 0)));
	scene.rs->canvas_item_set_deform_angles(upper, values({ float(Math::PI / 2) }));
	const Rect2 view(0, 0, 50, 300);
	scene.render(Transform2D(), view);
	CHECK(scene.drawn(lower));

	scene.rs->canvas_item_set_deform_joints(upper, Vector<float>());
	scene.render(Transform2D(), view);
	CHECK_FALSE(scene.drawn(lower));
}

TEST_CASE("[SceneTree][CanvasDeform] Nothing runs while no item has a deform state") {
	Scene scene;
	RID item = scene.add(RID(), Rect2(0, 0, 10, 10));
	scene.bend(item, joint(Vector2(5, 5)));
	scene.render();
	CHECK(RSG::canvas_render->deform_data.rows.size() > 0);
	scene.rs->canvas_item_set_deform_joints(item, Vector<float>());
	scene.render();
	CHECK_FALSE(RSG::canvas->deform_active);
	CHECK(RSG::canvas_render->deform_data.rows.is_empty());
	CHECK(scene.item(item)->deform == 0);
}

TEST_CASE("[SceneTree][CanvasDeform] A rig at rest writes no record and its children inherit none") {
	Scene scene;
	RID torso = scene.add(RID(), Rect2(0, 0, 20, 40));
	RID leg = scene.add(torso, Rect2(0, 30, 10, 40));
	RID foot = scene.add(leg, Rect2(0, 65, 10, 8));
	Vector<float> two = joint(Vector2(10, 35));
	two.append_array(joint(Vector2(10, 20), Vector2(1, 0)));
	scene.rs->canvas_item_set_deform_joints(torso, two);
	scene.rs->canvas_item_set_deform_joints(leg, joint(Vector2(5, 50)));
	scene.rs->canvas_item_set_deform_stretch(leg, values({ 1.0f }));
	scene.render();
	CHECK(scene.chain(torso) == 0);
	CHECK(scene.chain(leg) == 0);
	CHECK(scene.chain(foot) == 0);
	CHECK(RSG::canvas->deform_record_rows.is_empty());
	CHECK(RSG::canvas_render->deform_data.rows.is_empty());

	// One turning joint of the torso: the leg at rest adds no record, so leg and foot share the torso's chain.
	scene.rs->canvas_item_set_deform_angles(torso, values({ 0.0f, 0.5f }));
	scene.render();
	REQUIRE(scene.records(torso).size() == 1);
	CHECK(scene.chain(leg) == scene.chain(torso));
	CHECK(scene.chain(foot) == scene.chain(torso));
	CHECK(RSG::canvas->deform_record_rows.size() == (4 + 2 * 2) * 4);

	// A stretch moves too; with its angles back at rest, the torso writes nothing again.
	scene.rs->canvas_item_set_deform_stretch(leg, values({ 1.5f }));
	scene.rs->canvas_item_set_deform_angles(torso, values({ 0.0f, 0.0f }));
	scene.render();
	CHECK(scene.chain(torso) == 0);
	REQUIRE(scene.records(leg).size() == 1);
	CHECK(scene.chain(foot) == scene.chain(leg));
}

TEST_CASE("[SceneTree][CanvasDeform] Whether a rig moves is decided when its values are set, and rigs at rest run no pass") {
	const uint32_t moving = RSG::canvas->deform_moving_count;
	{
		Scene scene;
		RID torso = scene.add(RID(), Rect2(0, 0, 20, 40));
		RID leg = scene.add(torso, Rect2(0, 30, 10, 40));
		scene.rs->canvas_item_set_deform_joints(torso, joint(Vector2(10, 35)));
		scene.rs->canvas_item_set_deform_joints(leg, joint(Vector2(5, 50)));
		scene.rs->canvas_item_set_deform_flags(leg, RSE::CANVAS_DEFORM_FLAG_TIP_UNDER);
		REQUIRE(scene.item(torso)->deform_state != nullptr);
		CHECK_FALSE(scene.item(torso)->deform_state->moving);
		CHECK_FALSE(scene.item(leg)->deform_state->moving);
		CHECK(RSG::canvas->deform_moving_count == moving);

		const uint64_t pass = RSG::canvas->deform_pass;
		scene.render();
		if (moving == 0) {
			CHECK(RSG::canvas->deform_pass == pass);
		}
		CHECK(scene.chain(torso) == 0);
		CHECK(scene.chain(leg) == 0);

		// A turn moves the torso; the leg at rest keeps its chain and adds its own flags to it.
		scene.rs->canvas_item_set_deform_angles(torso, values({ 0.5f }));
		CHECK(scene.item(torso)->deform_state->moving);
		CHECK(RSG::canvas->deform_moving_count == moving + 1);
		scene.render();
		CHECK(RSG::canvas->deform_pass > pass);
		REQUIRE(scene.chain(torso) != 0);
		CHECK(scene.chain(leg) == scene.chain(torso));
		CHECK((scene.item(leg)->deform & Render::DEFORM_TIP_UNDER) != 0);
		CHECK((scene.item(torso)->deform & Render::DEFORM_TIP_UNDER) == 0);

		scene.rs->canvas_item_set_deform_angles(torso, values({ 0.0f }));
		CHECK_FALSE(scene.item(torso)->deform_state->moving);
		CHECK(RSG::canvas->deform_moving_count == moving);

		// A stretch and a warp move too; clearing the warp leaves the stretch.
		scene.rs->canvas_item_set_deform_stretch(leg, values({ 1.5f }));
		CHECK(scene.item(leg)->deform_state->moving);
		Vector<Vector2> offsets;
		offsets.resize(4);
		offsets.fill(Vector2());
		scene.rs->canvas_item_set_deform_warp(torso, 2, 2, Rect2(0, 0, 20, 40), offsets, 1.0f);
		CHECK(scene.item(torso)->deform_state->moving);
		CHECK(RSG::canvas->deform_moving_count == moving + 2);
		scene.rs->canvas_item_set_deform_warp(torso, 0, 0, Rect2(), Vector<Vector2>(), 0.0f);
		CHECK_FALSE(scene.item(torso)->deform_state->moving);
		CHECK(RSG::canvas->deform_moving_count == moving + 1);

		// New joints start at rest.
		scene.rs->canvas_item_set_deform_joints(leg, joint(Vector2(5, 55)));
		CHECK_FALSE(scene.item(leg)->deform_state->moving);
		CHECK(RSG::canvas->deform_moving_count == moving);
		scene.rs->canvas_item_set_deform_stretch(leg, values({ 2.0f }));
		CHECK(RSG::canvas->deform_moving_count == moving + 1);
	}
	// Freeing an item whose rig moves gives its count back.
	CHECK(RSG::canvas->deform_moving_count == moving);
}

// Classification against hand-written rows: chains 1 (hip) and 2 (knee inside hip), records from row 3.
struct ClassifyRows {
	ClassifyRows(float p_hip_angle, float p_knee_angle, float p_knee_stretch = 1.0f, float p_knee_segment = 0.0f, bool p_warp = false) {
		Render::DeformData &data = RSG::canvas_render->deform_data;
		LocalVector<float> rows;
		const uint32_t chains[3][4] = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 9, 1, 0, 0 } };
		for (const uint32_t *chain : chains) {
			for (int i = 0; i < 4; i++) {
				float value;
				memcpy(&value, &chain[i], sizeof(float));
				rows.push_back(value);
			}
		}
		// Hip record (id 1): identity owner, one joint at (0, 10) along +y, then the warp.
		const float hip[] = { 1, 0, 0, 1, 0, 0, 1, p_warp ? 2.0f : 0.0f, p_warp ? 2.0f : 0.0f, 1, 0, 0, 0, 30, 10, 10,
			0, 10, 0, 1, p_hip_angle, 2, 1, 0 };
		for (float value : hip) {
			rows.push_back(value);
		}
		// Warp offsets (one row of two points per row pair) keep the record 6 rows long without a warp.
		const float warp[] = { 1, 0, 0, 0, 0, 0, 0, 0 };
		for (float value : warp) {
			rows.push_back(value);
		}
		// Knee record (id 9): owner moved by (0, 20), joint at its (0, 5).
		const float knee[] = { 1, 0, 0, 1, 0, 20, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 5, 0, 1, p_knee_angle, 2, p_knee_stretch, p_knee_segment };
		for (float value : knee) {
			rows.push_back(value);
		}
		data.rows = rows;
		data.record_base = 3;
	}
	~ClassifyRows() {
		RSG::canvas_render->deform_data.rows.clear();
		RSG::canvas_render->deform_data.record_base = 0;
	}
};

static Transform2D turn_about(float p_angle, const Vector2 &p_pivot) {
	Transform2D turn(p_angle, Vector2());
	turn.columns[2] = p_pivot - turn.basis_xform(p_pivot);
	return turn;
}

TEST_CASE("[SceneTree][CanvasDeform] Rectangles before, beyond and across a joint band") {
	ClassifyRows rows(0.5f, 0.0f);
	Transform2D move;
	CHECK(RSG::canvas_render->deform_classify_rect(1, Transform2D(), Rect2(-5, -10, 10, 17), move) == Render::DEFORM_RECT_STILL);
	REQUIRE(RSG::canvas_render->deform_classify_rect(1, Transform2D(), Rect2(-5, 13, 10, 5), move) == Render::DEFORM_RECT_RIGID);
	CHECK(move.is_equal_approx(turn_about(0.5f, Vector2(0, 10))));
	CHECK(RSG::canvas_render->deform_classify_rect(1, Transform2D(), Rect2(-5, 5, 10, 10), move) == Render::DEFORM_RECT_GRID);
	// A mirrored item maps its rectangle to the same canvas place.
	REQUIRE(RSG::canvas_render->deform_classify_rect(1, Transform2D(Vector2(-1, 0), Vector2(0, 1), Vector2()), Rect2(-5, 13, 10, 5), move) == Render::DEFORM_RECT_RIGID);
	CHECK(move.is_equal_approx(turn_about(0.5f, Vector2(0, 10))));
}

TEST_CASE("[SceneTree][CanvasDeform] Weights of a chain come from the straight position") {
	ClassifyRows rows(0.5f, 0.75f);
	Transform2D move;
	// Beyond the knee (canvas y 25): the knee turns first, then the hip.
	REQUIRE(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 30, 10, 5), move) == Render::DEFORM_RECT_RIGID);
	CHECK(move.is_equal_approx(turn_about(0.5f, Vector2(0, 10)) * turn_about(0.75f, Vector2(0, 25))));
	// Between hip and knee bands: the hip only.
	REQUIRE(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 14, 10, 6), move) == Render::DEFORM_RECT_RIGID);
	CHECK(move.is_equal_approx(turn_about(0.5f, Vector2(0, 10))));
	CHECK(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 20, 10, 10), move) == Render::DEFORM_RECT_GRID);
}

TEST_CASE("[SceneTree][CanvasDeform] Stretch pieces move or stretch whole rectangles") {
	ClassifyRows rows(0.0f, 0.0f, 1.5f, 10.0f);
	Transform2D move;
	// Beyond the stretched segment: moved by segment * (stretch - 1) along the axis.
	REQUIRE(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 40, 10, 5), move) == Render::DEFORM_RECT_RIGID);
	CHECK(move.is_equal_approx(Transform2D(0, Vector2(0, 5))));
	// Inside the segment: stretched along the axis from the joint.
	REQUIRE(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 27, 10, 5), move) == Render::DEFORM_RECT_RIGID);
	CHECK(move.xform(Vector2(0, 27)).is_equal_approx(Vector2(0, 28)));
	CHECK(move.xform(Vector2(3, 32)).is_equal_approx(Vector2(3, 35.5)));
	// Across the segment end: a grid.
	CHECK(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 33, 10, 5), move) == Render::DEFORM_RECT_GRID);
}

TEST_CASE("[SceneTree][CanvasDeform] A warp rectangle makes the rectangles it overlaps warp grids") {
	ClassifyRows rows(0.0f, 0.5f, 1.0f, 0.0f, true);
	Transform2D move;
	CHECK(RSG::canvas_render->deform_classify_rect(1, Transform2D(), Rect2(-5, 32, 10, 5), move) == Render::DEFORM_RECT_WARP_GRID);
	CHECK(RSG::canvas_render->deform_classify_rect(1, Transform2D(), Rect2(-5, 45, 10, 5), move) == Render::DEFORM_RECT_STILL);
	// A rectangle across the knee band of the inner record: a joint grid, or a warp grid when the outer warp overlaps it.
	CHECK(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 24, 10, 2), move) == Render::DEFORM_RECT_GRID);
	CHECK(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 24, 10, 8), move) == Render::DEFORM_RECT_WARP_GRID);
}

TEST_CASE("[SceneTree][CanvasDeform] Joints that neither turn nor stretch leave rectangles plain") {
	Transform2D move;
	{
		// Both joints neutral: rectangles across their bands keep the plain quad.
		ClassifyRows rows(0.0f, 0.0f);
		CHECK(RSG::canvas_render->deform_classify_rect(1, Transform2D(), Rect2(-5, 5, 10, 10), move) == Render::DEFORM_RECT_STILL);
		CHECK(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 0, 10, 40), move) == Render::DEFORM_RECT_STILL);
	}
	{
		// A neutral knee inside a turning hip: only the hip band makes a grid.
		ClassifyRows rows(0.5f, 0.0f);
		REQUIRE(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 20, 10, 10), move) == Render::DEFORM_RECT_RIGID);
		CHECK(move.is_equal_approx(turn_about(0.5f, Vector2(0, 10))));
		CHECK(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 5, 10, 10), move) == Render::DEFORM_RECT_GRID);
	}
	{
		// A knee that stretches but does not turn: its band alone makes no grid.
		ClassifyRows rows(0.0f, 0.0f, 1.5f, 0.0f);
		REQUIRE(RSG::canvas_render->deform_classify_rect(2, Transform2D(), Rect2(-5, 26, 10, 4), move) == Render::DEFORM_RECT_RIGID);
		CHECK(move.xform(Vector2(0, 26)).is_equal_approx(Vector2(0, 26.5)));
	}
}

TEST_CASE("[SceneTree][CanvasDeform] Large and tiny finite axes keep their direction") {
	Scene scene;
	RID item = scene.add(RID(), Rect2(0, 0, 10, 40));
	RendererCanvasCull::Item::DeformState *state = nullptr;

	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(5, 10), Vector2(0, 1.0e30f)));
	state = scene.item(item)->deform_state;
	REQUIRE(state != nullptr);
	CHECK(state->joints[2] == 0.0f);
	CHECK(state->joints[3] == doctest::Approx(1.0f));

	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(5, 10), Vector2(1.0e-30f, 0)));
	CHECK(state->joints[2] == doctest::Approx(1.0f));
	CHECK(state->joints[3] == 0.0f);

	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(5, 10), Vector2(-3.0e38f, 3.0e38f)));
	CHECK(state->joints[2] == doctest::Approx(-Math::SQRT12));
	CHECK(state->joints[3] == doctest::Approx(Math::SQRT12));

	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(5, 10), Vector2(0, 2)));
	ERR_PRINT_OFF;
	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(1, 1), Vector2(0, 0)));
	scene.rs->canvas_item_set_deform_joints(item, joint(Vector2(1, 1), Vector2(-0.0f, 0)));
	ERR_PRINT_ON;
	REQUIRE(scene.item(item)->deform_state == state);
	CHECK(state->joints[0] == 5.0f);
	CHECK(state->joints[2] == 0.0f);
	CHECK(state->joints[3] == doctest::Approx(1.0f));
}

TEST_CASE("[SceneTree][CanvasDeform] A mask follows only the records of its owner's chain") {
	Scene scene;
	RID grandparent = scene.add(RID(), Rect2(0, 0, 20, 20));
	RID parent = scene.add(grandparent, Rect2(0, 0, 20, 20));
	RID child = scene.add(parent, Rect2(0, 10, 20, 40));
	RID plain_child = scene.add(parent, Rect2(0, 10, 20, 40));
	RID stranger = scene.add(RID(), Rect2(100, 0, 20, 20));
	RID parent_mask = scene.add_mask(parent, child);
	scene.rs->canvas_item_add_mask(plain_child, parent_mask);
	RID own_mask = scene.add_mask(child, child);
	RID stranger_mask = scene.add_mask(stranger, child);
	RID grandparent_mask = scene.add_mask(grandparent, child);
	scene.bend(stranger, joint(Vector2(5, 5)));
	scene.bend(child, joint(Vector2(0, 0), Vector2(0, 1), 1.0f), 1.5f);

	// The parent has no record: its mask stays; the child's own mask follows the child's whole bend.
	scene.render();
	REQUIRE(scene.records(child).size() == 1);
	CHECK(scene.follow_code(child, parent_mask) == 0);
	CHECK(scene.follow_code(child, own_mask) == 1);
	CHECK(scene.follow_code(child, stranger_mask) == 0);
	CHECK(scene.follow_code(child, grandparent_mask) == 0);
	CHECK(scene.chain(plain_child) == 0);

	// A record on the parent: its mask follows the parent's record only, sampled after the child's record.
	scene.bend(parent, joint(Vector2(0, 0), Vector2(0, 1), 1.0f));
	scene.render();
	REQUIRE(scene.records(child).size() == 2);
	CHECK(scene.follow_code(child, parent_mask) == 2);
	CHECK(scene.follow_code(child, own_mask) == 1);
	CHECK(scene.follow_code(child, stranger_mask) == 0);
	CHECK(scene.follow_code(child, grandparent_mask) == 0);
	CHECK(scene.follow_code(plain_child, parent_mask) == 1);
	CHECK(scene.records(plain_child) == scene.records(parent));

	// A grandparent record: its mask is sampled after the child's and the parent's records.
	scene.bend(grandparent, joint(Vector2(0, 0)));
	scene.render();
	REQUIRE(scene.records(child).size() == 3);
	CHECK(scene.follow_code(child, parent_mask) == 2);
	CHECK(scene.follow_code(child, own_mask) == 1);
	CHECK(scene.follow_code(child, grandparent_mask) == 3);
	// Chains with codes hold the same records as the plain chain.
	CHECK(scene.records(child)[1] == scene.records(parent)[0]);
	CHECK(scene.records(child)[2] == scene.records(grandparent)[0]);
}

TEST_CASE("[SceneTree][CanvasDeform] A Y-sorted child follows only its owner's records") {
	Scene scene;
	RID body = scene.add(RID(), Rect2(0, 0, 20, 40));
	scene.rs->canvas_item_set_sort_children_by_y(body, true);
	RID leg = scene.add(body, Rect2(0, 10, 20, 40), Transform2D(0, Vector2(0, 5)));
	RID mask = scene.add_mask(body, leg);
	scene.bend(body, joint(Vector2(0, 0), Vector2(0, 1), 1.0f));
	scene.bend(leg, joint(Vector2(0, 0), Vector2(0, 1), 1.0f));
	scene.render();
	REQUIRE(scene.records(leg).size() == 2);
	CHECK(scene.follow_code(leg, mask) == 2);
}

} // namespace TestCanvasDeform
