/**************************************************************************/
/*  test_drawable_texture_2d.cpp                                          */
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

TEST_FORCE_LINK(test_drawable_texture_2d)

#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "scene/resources/canvas_texture_page_view.h"
#include "scene/resources/drawable_texture_2d.h"
#include "scene/resources/drawable_texture_2d_array.h"
#include "scene/resources/image_texture.h"
#include "servers/rendering/renderer_canvas_render.h"
#include "servers/rendering/rendering_server.h"

namespace TestDrawableTexture2D {

static Ref<Image> make_rgba8_image(const Size2i &p_size, uint8_t p_seed, bool p_mipmaps = false) {
	Vector<uint8_t> data;
	data.resize(Image::get_image_data_size(p_size.x, p_size.y, Image::FORMAT_RGBA8, p_mipmaps));
	for (int i = 0; i < data.size(); i++) {
		data.write[i] = uint8_t(p_seed + i * 17);
	}
	return Image::create_from_data(p_size.x, p_size.y, p_mipmaps, Image::FORMAT_RGBA8, data);
}

static void check_region(const Ref<Image> &p_readback, const Rect2i &p_region, const Ref<Image> &p_expected) {
	const Vector<uint8_t> readback_data = p_readback->get_data();
	const Vector<uint8_t> expected_data = p_expected->get_data();
	for (int y = 0; y < p_readback->get_height(); y++) {
		for (int x = 0; x < p_readback->get_width(); x++) {
			const int readback_offset = (y * p_readback->get_width() + x) * 4;
			if (p_region.has_point(Point2i(x, y))) {
				const Point2i source = Point2i(x, y) - p_region.position;
				const int expected_offset = (source.y * p_expected->get_width() + source.x) * 4;
				for (int channel = 0; channel < 4; channel++) {
					CHECK(readback_data[readback_offset + channel] == expected_data[expected_offset + channel]);
				}
			} else {
				for (int channel = 0; channel < 4; channel++) {
					CHECK(readback_data[readback_offset + channel] == 0);
				}
			}
		}
	}
}

static Error update_subresources(RID p_texture, uint64_t p_generation, const TypedArray<Image> &p_images, const TypedArray<Rect2i> &p_regions, const PackedInt32Array &p_mipmaps, const PackedInt32Array &p_layers) {
	return RS::get_singleton()->texture_drawable_update_subresources(p_texture, p_images, p_regions, p_mipmaps, p_layers, p_generation);
}

static void check_rejected_batch(RID p_texture, uint64_t p_generation, const TypedArray<Image> &p_images, const TypedArray<Rect2i> &p_regions, const PackedInt32Array &p_mipmaps, const PackedInt32Array &p_layers, Error p_expected_error, uint64_t p_submitted_generation = 0) {
	const Ref<Image> before_base = RS::get_singleton()->texture_drawable_get_subresource(p_texture, 0, p_generation, 0);
	const Ref<Image> before_mipmap = RS::get_singleton()->texture_drawable_get_subresource(p_texture, 1, p_generation, 0);
	REQUIRE(before_base.is_valid());
	REQUIRE(before_mipmap.is_valid());

	ERR_PRINT_OFF;
	const Error error = update_subresources(p_texture, p_submitted_generation == 0 ? p_generation : p_submitted_generation, p_images, p_regions, p_mipmaps, p_layers);
	ERR_PRINT_ON;
	CHECK(error == p_expected_error);

	const Ref<Image> after_base = RS::get_singleton()->texture_drawable_get_subresource(p_texture, 0, p_generation, 0);
	const Ref<Image> after_mipmap = RS::get_singleton()->texture_drawable_get_subresource(p_texture, 1, p_generation, 0);
	REQUIRE(after_base.is_valid());
	REQUIRE(after_mipmap.is_valid());
	CHECK(after_base->get_data() == before_base->get_data());
	CHECK(after_mipmap->get_data() == before_mipmap->get_data());
}

static void check_rejected_second_write(RID p_texture, uint64_t p_generation, const Ref<Image> &p_image, const Rect2i &p_region, int p_mipmap, int p_layer, Error p_expected_error) {
	TypedArray<Image> images;
	images.push_back(make_rgba8_image(Size2i(2, 2), 181));
	images.push_back(p_image);
	TypedArray<Rect2i> regions;
	regions.push_back(Rect2i(0, 0, 2, 2));
	regions.push_back(p_region);
	PackedInt32Array mipmaps;
	mipmaps.push_back(0);
	mipmaps.push_back(p_mipmap);
	PackedInt32Array layers;
	layers.push_back(0);
	layers.push_back(p_layer);
	check_rejected_batch(p_texture, p_generation, images, regions, mipmaps, layers, p_expected_error);
}

static const Point2i STRESS_SOURCE_ORIGINS[5] = {
	Point2i(0, 0),
	Point2i(1024, 0),
	Point2i(2048, 0),
	Point2i(3072, 0),
	Point2i(0, 1024),
};

static uint8_t stress_seed(bool p_reuse_images, int p_source, int p_mipmap) {
	return p_reuse_images ? uint8_t(31 + p_mipmap * 13) : uint8_t(31 + p_source * 37 + p_mipmap * 13);
}

struct ExactUpdateStressCase {
	RID texture;
	uint64_t generation = 0;
	bool complete_mip_chains = false;
	bool reuse_images = false;
	Vector<Error> results;
	SafeFlag done;
};

static void run_exact_update_stress(void *p_userdata) {
	ExactUpdateStressCase *test_case = static_cast<ExactUpdateStressCase *>(p_userdata);
	const int mipmap_count = test_case->complete_mip_chains ? 11 : 1;

	Vector<Ref<Image>> reused_images;
	if (test_case->reuse_images) {
		reused_images.resize(mipmap_count);
		for (int mipmap = 0; mipmap < mipmap_count; mipmap++) {
			const int size = MAX(1, 1024 >> mipmap);
			reused_images.write[mipmap] = make_rgba8_image(Size2i(size, size), stress_seed(true, 0, mipmap));
		}
	}

	test_case->results.resize(5 * mipmap_count);
	int result_index = 0;
	for (int source = 0; source < 5; source++) {
		for (int mipmap = 0; mipmap < mipmap_count; mipmap++) {
			const int size = MAX(1, 1024 >> mipmap);
			const Point2i destination(STRESS_SOURCE_ORIGINS[source].x >> mipmap, STRESS_SOURCE_ORIGINS[source].y >> mipmap);
			const Ref<Image> upload = test_case->reuse_images
					? reused_images[mipmap]
					: make_rgba8_image(Size2i(size, size), stress_seed(false, source, mipmap));
			test_case->results.write[result_index++] = RS::get_singleton()->texture_drawable_update_subresource(
					test_case->texture, upload, Rect2i(destination, Size2i(size, size)), mipmap, test_case->generation, 0);
		}
	}
	test_case->done.set();
}

static void check_exact_update_stress(bool p_worker_thread, bool p_complete_mip_chains, bool p_reuse_images) {
	Ref<DrawableTexture2D> texture;
	texture.instantiate();
	REQUIRE(texture->setup_checked(4096, 4096, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);

	ExactUpdateStressCase test_case;
	test_case.texture = texture->get_rid();
	test_case.generation = RS::get_singleton()->texture_drawable_get_generation(test_case.texture);
	test_case.complete_mip_chains = p_complete_mip_chains;
	test_case.reuse_images = p_reuse_images;

	if (p_worker_thread) {
		// The test harness rendering server is not threaded, so the main thread must keep
		// flushing the command queue while the worker blocks on synchronous queued updates.
		const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + 60000000;
		Thread worker;
		REQUIRE(worker.start(run_exact_update_stress, &test_case) != Thread::UNASSIGNED_ID);
		while (!test_case.done.is_set() && OS::get_singleton()->get_ticks_usec() < deadline) {
			RS::get_singleton()->sync();
			OS::get_singleton()->delay_usec(100);
		}
		if (!test_case.done.is_set()) {
			CRASH_NOW_MSG("Timed out while pumping worker-thread exact drawable updates.");
		}
		worker.wait_to_finish();
	} else {
		run_exact_update_stress(&test_case);
	}

	const int expected_result_count = 5 * (p_complete_mip_chains ? 11 : 1);
	REQUIRE(test_case.results.size() == expected_result_count);
	for (const Error result : test_case.results) {
		CHECK(result == OK);
	}

	// The dummy renderer verifies byte-exact RenderingServer routing and subresource selection.
	const int mipmap_count = p_complete_mip_chains ? 11 : 1;
	for (int mipmap = 0; mipmap < mipmap_count; mipmap++) {
		const int page_axis = MAX(1, 4096 >> mipmap);
		const int source_axis = MAX(1, 1024 >> mipmap);
		Ref<Image> expected = Image::create_empty(page_axis, page_axis, false, Image::FORMAT_RGBA8);
		for (int source = 0; source < 5; source++) {
			const Ref<Image> upload = make_rgba8_image(Size2i(source_axis, source_axis), stress_seed(p_reuse_images, source, mipmap));
			expected->blit_rect(upload, Rect2i(0, 0, source_axis, source_axis),
					Point2i(STRESS_SOURCE_ORIGINS[source].x >> mipmap, STRESS_SOURCE_ORIGINS[source].y >> mipmap));
		}
		const Ref<Image> readback = RS::get_singleton()->texture_drawable_get_subresource(test_case.texture, mipmap, test_case.generation, 0);
		REQUIRE(readback.is_valid());
		CHECK(readback->get_data() == expected->get_data());
	}
}

TEST_CASE("[SceneTree][DrawableTexture2D][RenderingServerExactTransfers] main-thread base-level updates") {
	check_exact_update_stress(false, false, false);
}

TEST_CASE("[SceneTree][DrawableTexture2D][RenderingServerExactTransfers] main-thread mip-chain updates") {
	check_exact_update_stress(false, true, false);
}

TEST_CASE("[SceneTree][DrawableTexture2D][RenderingServerExactTransfers] worker-thread command-queue round trip") {
	check_exact_update_stress(true, false, true);
}

TEST_CASE("[SceneTree][DrawableTexture2D] exact subresource writes and readback") {
	Ref<DrawableTexture2D> texture;
	texture.instantiate();
	CHECK(texture->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);

	const RID rid = texture->get_rid();
	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(rid);
	CHECK(generation == 1);

	const Rect2i regions[] = {
		Rect2i(2, 1, 3, 2),
		Rect2i(1, 1, 2, 2),
		Rect2i(0, 1, 2, 1),
		Rect2i(0, 0, 1, 1),
	};
	for (int mipmap = 0; mipmap < 4; mipmap++) {
		Ref<Image> upload = make_rgba8_image(regions[mipmap].size, uint8_t(11 + mipmap * 31));
		CHECK(RS::get_singleton()->texture_drawable_update_subresource(rid, upload, regions[mipmap], mipmap, generation, 0) == OK);
		Ref<Image> readback = RS::get_singleton()->texture_drawable_get_subresource(rid, mipmap, generation, 0);
		REQUIRE(readback.is_valid());
		CHECK(readback->get_size() == Size2i(MAX(1, 8 >> mipmap), MAX(1, 8 >> mipmap)));
		CHECK(readback->get_format() == Image::FORMAT_RGBA8);
		CHECK_FALSE(readback->has_mipmaps());
		check_region(readback, regions[mipmap], upload);
	}
}

TEST_CASE("[SceneTree][DrawableTexture2D] batched exact writes update disjoint regions, mipmaps, and untouched bytes") {
	Ref<DrawableTexture2D> texture;
	texture.instantiate();
	REQUIRE(texture->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	const RID rid = texture->get_rid();
	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(rid);

	const Ref<Image> base_first = make_rgba8_image(Size2i(3, 2), 11);
	const Ref<Image> mip_write = make_rgba8_image(Size2i(2, 2), 47);
	const Ref<Image> base_second = make_rgba8_image(Size2i(2, 1), 89);
	TypedArray<Image> images;
	images.push_back(base_first);
	images.push_back(mip_write);
	images.push_back(base_second);
	TypedArray<Rect2i> regions;
	regions.push_back(Rect2i(1, 2, 3, 2));
	regions.push_back(Rect2i(1, 1, 2, 2));
	regions.push_back(Rect2i(5, 3, 2, 1));
	PackedInt32Array mipmaps;
	mipmaps.push_back(0);
	mipmaps.push_back(1);
	mipmaps.push_back(0);
	PackedInt32Array layers;
	layers.push_back(0);
	layers.push_back(0);
	layers.push_back(0);

	CHECK(update_subresources(rid, generation, images, regions, mipmaps, layers) == OK);

	Ref<Image> expected_base = Image::create_empty(8, 8, false, Image::FORMAT_RGBA8);
	expected_base->blit_rect(base_first, Rect2i(Vector2i(), base_first->get_size()), Point2i(1, 2));
	expected_base->blit_rect(base_second, Rect2i(Vector2i(), base_second->get_size()), Point2i(5, 3));
	const Ref<Image> actual_base = RS::get_singleton()->texture_drawable_get_subresource(rid, 0, generation, 0);
	REQUIRE(actual_base.is_valid());
	CHECK(actual_base->get_data() == expected_base->get_data());

	Ref<Image> expected_mipmap = Image::create_empty(4, 4, false, Image::FORMAT_RGBA8);
	expected_mipmap->blit_rect(mip_write, Rect2i(Vector2i(), mip_write->get_size()), Point2i(1, 1));
	const Ref<Image> actual_mipmap = RS::get_singleton()->texture_drawable_get_subresource(rid, 1, generation, 0);
	REQUIRE(actual_mipmap.is_valid());
	CHECK(actual_mipmap->get_data() == expected_mipmap->get_data());
}

TEST_CASE("[SceneTree][DrawableTexture2D] invalid batched writes are all-or-nothing") {
	Ref<DrawableTexture2D> texture;
	texture.instantiate();
	REQUIRE(texture->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	const RID rid = texture->get_rid();
	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(rid);
	const Ref<Image> valid = make_rgba8_image(Size2i(2, 2), 131);

	check_rejected_second_write(rid, generation, Ref<Image>(), Rect2i(0, 0, 2, 2), 0, 0, ERR_INVALID_PARAMETER);
	Ref<Image> empty;
	empty.instantiate();
	check_rejected_second_write(rid, generation, empty, Rect2i(0, 0, 2, 2), 0, 0, ERR_INVALID_PARAMETER);
	check_rejected_second_write(rid, generation, make_rgba8_image(Size2i(2, 2), 137, true), Rect2i(0, 0, 2, 2), 0, 0, ERR_INVALID_PARAMETER);
	check_rejected_second_write(rid, generation, Image::create_empty(2, 2, false, Image::FORMAT_RGBAH), Rect2i(0, 0, 2, 2), 0, 0, ERR_INVALID_DATA);
	check_rejected_second_write(rid, generation, make_rgba8_image(Size2i(1, 2), 139), Rect2i(0, 0, 2, 2), 0, 0, ERR_INVALID_PARAMETER);
	check_rejected_second_write(rid, generation, valid, Rect2i(-1, 0, 2, 2), 0, 0, ERR_INVALID_PARAMETER);
	check_rejected_second_write(rid, generation, valid, Rect2i(0, 0, 0, 2), 0, 0, ERR_INVALID_PARAMETER);
	check_rejected_second_write(rid, generation, valid, Rect2i(7, 7, 2, 2), 0, 0, ERR_INVALID_PARAMETER);
	check_rejected_second_write(rid, generation, valid, Rect2i(0, 0, 2, 2), 4, 0, ERR_INVALID_PARAMETER);
	check_rejected_second_write(rid, generation, valid, Rect2i(0, 0, 2, 2), 0, 1, ERR_INVALID_PARAMETER);

	TypedArray<Image> images;
	images.push_back(valid);
	images.push_back(valid);
	TypedArray<Rect2i> regions;
	regions.push_back(Rect2i(0, 0, 2, 2));
	PackedInt32Array mipmaps;
	mipmaps.push_back(0);
	mipmaps.push_back(0);
	PackedInt32Array layers;
	layers.push_back(0);
	layers.push_back(0);
	check_rejected_batch(rid, generation, images, regions, mipmaps, layers, ERR_INVALID_PARAMETER);

	regions.push_back(Rect2i(1, 1, 2, 2));
	check_rejected_batch(rid, generation, images, regions, mipmaps, layers, ERR_INVALID_PARAMETER);

	TypedArray<Image> empty_images;
	TypedArray<Rect2i> empty_regions;
	PackedInt32Array empty_mipmaps;
	PackedInt32Array empty_layers;
	check_rejected_batch(rid, generation, empty_images, empty_regions, empty_mipmaps, empty_layers, ERR_INVALID_PARAMETER);

	regions.clear();
	regions.push_back(Rect2i(0, 0, 2, 2));
	regions.push_back(Rect2i(2, 0, 2, 2));
	check_rejected_batch(rid, generation, images, regions, mipmaps, layers, ERR_INVALID_DATA, generation + 1);

	Ref<ImageTexture> ordinary = ImageTexture::create_from_image(make_rgba8_image(Size2i(8, 8), 149));
	ERR_PRINT_OFF;
	CHECK(update_subresources(ordinary->get_rid(), 0, images, regions, mipmaps, layers) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

TEST_CASE("[SceneTree][DrawableTexture2DArray] batched writes address exact layers and mipmaps") {
	const int max_layers = RS::get_singleton()->texture_drawable_get_max_array_layers();
	if (max_layers < 3) {
		return;
	}

	Ref<DrawableTexture2DArray> texture;
	texture.instantiate();
	REQUIRE(texture->setup(8, 8, 3, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	const RID rid = texture->get_rid();
	const uint64_t generation = texture->get_generation();

	const Ref<Image> layer_zero = make_rgba8_image(Size2i(2, 2), 157);
	const Ref<Image> layer_two_mipmap = make_rgba8_image(Size2i(2, 1), 163);
	const Ref<Image> layer_zero_second = make_rgba8_image(Size2i(1, 1), 167);
	TypedArray<Image> images;
	images.push_back(layer_zero);
	images.push_back(layer_two_mipmap);
	images.push_back(layer_zero_second);
	TypedArray<Rect2i> regions;
	regions.push_back(Rect2i(1, 1, 2, 2));
	regions.push_back(Rect2i(1, 2, 2, 1));
	regions.push_back(Rect2i(4, 2, 1, 1));
	PackedInt32Array mipmaps;
	mipmaps.push_back(0);
	mipmaps.push_back(1);
	mipmaps.push_back(0);
	PackedInt32Array layers;
	layers.push_back(0);
	layers.push_back(2);
	layers.push_back(0);
	REQUIRE(update_subresources(rid, generation, images, regions, mipmaps, layers) == OK);

	Ref<Image> expected_layer_zero = Image::create_empty(8, 8, false, Image::FORMAT_RGBA8);
	expected_layer_zero->blit_rect(layer_zero, Rect2i(Vector2i(), layer_zero->get_size()), Point2i(1, 1));
	expected_layer_zero->blit_rect(layer_zero_second, Rect2i(Vector2i(), layer_zero_second->get_size()), Point2i(4, 2));
	const Ref<Image> actual_layer_zero = RS::get_singleton()->texture_drawable_get_subresource(rid, 0, generation, 0);
	REQUIRE(actual_layer_zero.is_valid());
	CHECK(actual_layer_zero->get_data() == expected_layer_zero->get_data());

	Ref<Image> expected_layer_two_mipmap = Image::create_empty(4, 4, false, Image::FORMAT_RGBA8);
	expected_layer_two_mipmap->blit_rect(layer_two_mipmap, Rect2i(Vector2i(), layer_two_mipmap->get_size()), Point2i(1, 2));
	const Ref<Image> actual_layer_two_mipmap = RS::get_singleton()->texture_drawable_get_subresource(rid, 1, generation, 2);
	REQUIRE(actual_layer_two_mipmap.is_valid());
	CHECK(actual_layer_two_mipmap->get_data() == expected_layer_two_mipmap->get_data());

	const Ref<Image> untouched_layer = RS::get_singleton()->texture_drawable_get_subresource(rid, 0, generation, 1);
	REQUIRE(untouched_layer.is_valid());
	CHECK(untouched_layer->get_data() == Image::create_empty(8, 8, false, Image::FORMAT_RGBA8)->get_data());
}

TEST_CASE("[SceneTree][DrawableTexture2D] invalid exact writes preserve bytes") {
	Ref<DrawableTexture2D> texture;
	texture.instantiate();
	REQUIRE(texture->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	const RID rid = texture->get_rid();
	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(rid);
	const Ref<Image> before = RS::get_singleton()->texture_drawable_get_subresource(rid, 1, generation, 0);
	REQUIRE(before.is_valid());

	ERR_PRINT_OFF;
	const Ref<Image> valid = make_rgba8_image(Size2i(2, 2), 43);
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(rid, valid, Rect2i(3, 3, 2, 2), 1, generation, 0) == ERR_INVALID_PARAMETER);
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(rid, valid, Rect2i(0, 0, 2, 2), 4, generation, 0) == ERR_INVALID_PARAMETER);
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(rid, valid, Rect2i(0, 0, 2, 2), 1, generation, 1) == ERR_INVALID_PARAMETER);
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(rid, valid, Rect2i(0, 0, 2, 2), 1, generation + 1, 0) == ERR_INVALID_DATA);

	const Ref<Image> wrong_size = make_rgba8_image(Size2i(1, 2), 59);
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(rid, wrong_size, Rect2i(0, 0, 2, 2), 1, generation, 0) == ERR_INVALID_PARAMETER);

	Ref<Image> wrong_format = Image::create_empty(2, 2, false, Image::FORMAT_RGBAH);
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(rid, wrong_format, Rect2i(0, 0, 2, 2), 1, generation, 0) == ERR_INVALID_DATA);

	const Ref<Image> embedded_mips = make_rgba8_image(Size2i(2, 2), 71, true);
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(rid, embedded_mips, Rect2i(0, 0, 2, 2), 1, generation, 0) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;

	const Ref<Image> after = RS::get_singleton()->texture_drawable_get_subresource(rid, 1, generation, 0);
	REQUIRE(after.is_valid());
	CHECK(after->get_data() == before->get_data());
	ERR_PRINT_OFF;
	CHECK(RS::get_singleton()->texture_drawable_get_subresource(rid, 1, generation + 1, 0).is_null());
	ERR_PRINT_ON;
}

TEST_CASE("[SceneTree][DrawableTexture2D] setup is checked and generation protected") {
	Ref<DrawableTexture2D> texture;
	texture.instantiate();
	REQUIRE(texture->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	const RID rid = texture->get_rid();
	CHECK(RS::get_singleton()->texture_drawable_get_generation(rid) == 1);

	ERR_PRINT_OFF;
	CHECK(texture->setup_checked(0, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
	CHECK(texture->get_rid() == rid);
	CHECK(texture->get_width() == 8);
	CHECK(texture->get_height() == 8);
	CHECK(RS::get_singleton()->texture_drawable_get_generation(rid) == 1);

	CHECK(texture->setup_checked(4, 4, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	CHECK(texture->get_rid() == rid);
	CHECK(texture->get_width() == 4);
	CHECK(texture->get_height() == 4);
	CHECK(RS::get_singleton()->texture_drawable_get_generation(rid) == 2);
	ERR_PRINT_OFF;
	CHECK(RS::get_singleton()->texture_drawable_get_subresource(rid, 0, 1, 0).is_null());
	ERR_PRINT_ON;
	CHECK(RS::get_singleton()->texture_drawable_get_subresource(rid, 0, 2, 0).is_valid());

	Ref<Image> ordinary_image = make_rgba8_image(Size2i(2, 2), 83);
	Ref<ImageTexture> ordinary = ImageTexture::create_from_image(ordinary_image);
	CHECK(RS::get_singleton()->texture_drawable_get_generation(ordinary->get_rid()) == 0);
	ERR_PRINT_OFF;
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(ordinary->get_rid(), ordinary_image, Rect2i(0, 0, 2, 2), 0, 0, 0) != OK);
	RS::get_singleton()->texture_drawable_generate_mipmaps(RID());
	ERR_PRINT_ON;
}

TEST_CASE("[SceneTree][DrawableTexture2D] proxy commands preserve base contract through dummy routing") {
	Ref<DrawableTexture2D> texture;
	texture.instantiate();
	REQUIRE(texture->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	Ref<DrawableTexture2D> replacement;
	replacement.instantiate();
	REQUIRE(replacement->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);

	const RID texture_rid = texture->get_rid();
	const RID proxy = RS::get_singleton()->texture_proxy_create(texture_rid);
	REQUIRE(proxy.is_valid());
	RS::get_singleton()->texture_proxy_update(proxy, replacement->get_rid());
	RS::get_singleton()->texture_proxy_update(proxy, texture_rid);
	RS::get_singleton()->free_rid(proxy);

	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(texture_rid);
	const Ref<Image> upload = make_rgba8_image(Size2i(2, 2), 101);
	CHECK(RS::get_singleton()->texture_drawable_update_subresource(texture_rid, upload, Rect2i(0, 0, 2, 2), 1, generation, 0) == OK);
	RS::get_singleton()->texture_drawable_generate_mipmaps(texture_rid);
	CHECK(RS::get_singleton()->texture_drawable_get_subresource(texture_rid, 1, generation, 0).is_valid());
}

TEST_CASE("[SceneTree][DrawableTexture2DArray] dummy capability is deterministic") {
	CHECK(RS::get_singleton()->texture_drawable_get_max_array_layers() == 0);

	Ref<DrawableTexture2DArray> texture;
	texture.instantiate();
	ERR_PRINT_OFF;
	CHECK(texture->setup(8, 8, 2, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == ERR_CANT_CREATE);
	ERR_PRINT_ON;
	CHECK(texture->get_rid().is_null());
	CHECK(texture->get_width() == 0);
	CHECK(texture->get_height() == 0);
	CHECK(texture->get_layers() == 0);
	CHECK_FALSE(texture->has_mipmaps());
	CHECK(texture->get_generation() == 0);
	CHECK(texture->get_layered_type() == TextureLayered::LAYERED_TYPE_2D_ARRAY);
}

TEST_CASE("[SceneTree][CanvasTexturePageView] immutable logical adapter and fallback") {
	Ref<DrawableTexture2D> page;
	page.instantiate();
	REQUIRE(page->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(page->get_rid());

	Ref<Image> fallback_image = make_rgba8_image(Size2i(4, 4), 97);
	Ref<ImageTexture> fallback = ImageTexture::create_from_image(fallback_image);
	REQUIRE(fallback.is_valid());

	Ref<CanvasTexturePageView> view;
	view.instantiate();
	REQUIRE(view->configure(page, Rect2i(4, 0, 4, 4), 0, 2, generation, fallback) == OK);
	CHECK(view->get_width() == 4);
	CHECK(view->get_height() == 4);
	CHECK(view->get_mipmap_count() == 2);
	CHECK(view->has_mipmaps());
	CHECK(view->get_format() == Image::FORMAT_RGBA8);
	CHECK(view->get_rid() == fallback->get_rid());
	REQUIRE(view->get_image().is_valid());
	CHECK(view->get_image()->get_data() == fallback_image->get_data());

	Rect2 clipped_rect;
	Rect2 clipped_source;
	CHECK(view->get_rect_region(Rect2(0, 0, 40, 40), Rect2(-1, -1, 4, 4), clipped_rect, clipped_source));
	CHECK(clipped_rect == Rect2(10, 10, 30, 30));
	CHECK(clipped_source == Rect2(0, 0, 3, 3));

	const RID canvas_item = RS::get_singleton()->canvas_item_create();
	view->draw(canvas_item, Point2(3, 5));
	view->draw_rect(canvas_item, Rect2(0, 0, 8, 8), false);
	view->draw_rect(canvas_item, Rect2(0, 0, 8, 8), true);
	view->draw_rect_region(canvas_item, Rect2(0, 0, 8, 8), Rect2(1, 1, 2, 2), Color(1, 1, 1, 1), false, false);
	view->draw_rect_region(canvas_item, Rect2(0, 0, 8, 8), Rect2(1, 1, 2, 2), Color(1, 1, 1, 1), false, true);
	RS::get_singleton()->free_rid(canvas_item);

	ERR_PRINT_OFF;
	CHECK(view->configure(page, Rect2i(0, 0, 4, 4), 0, 2, generation, fallback) == ERR_ALREADY_IN_USE);
	ERR_PRINT_ON;
}

TEST_CASE("[SceneTree][CanvasTexturePageView] rejects invalid physical contracts") {
	Ref<DrawableTexture2D> page;
	page.instantiate();
	REQUIRE(page->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	const uint64_t generation = RS::get_singleton()->texture_drawable_get_generation(page->get_rid());
	Ref<ImageTexture> fallback = ImageTexture::create_from_image(make_rgba8_image(Size2i(4, 4), 109));
	Ref<ImageTexture> shared_fallback = ImageTexture::create_from_image(make_rgba8_image(Size2i(1, 1), 113));

	ERR_PRINT_OFF;
	Ref<CanvasTexturePageView> array_layer;
	array_layer.instantiate();
	CHECK(array_layer->configure(page, Rect2i(0, 0, 4, 4), 1, 2, generation, fallback) == ERR_INVALID_PARAMETER);

	Ref<CanvasTexturePageView> misaligned;
	misaligned.instantiate();
	CHECK(misaligned->configure(page, Rect2i(2, 0, 4, 4), 0, 2, generation, fallback) == ERR_INVALID_PARAMETER);

	Ref<CanvasTexturePageView> outside;
	outside.instantiate();
	CHECK(outside->configure(page, Rect2i(8, 0, 4, 4), 0, 2, generation, fallback) == ERR_INVALID_PARAMETER);

	Ref<CanvasTexturePageView> stale;
	stale.instantiate();
	CHECK(stale->configure(page, Rect2i(0, 0, 4, 4), 0, 2, generation + 1, fallback) == ERR_INVALID_DATA);

	Ref<CanvasTexturePageView> wrong_kind;
	wrong_kind.instantiate();
	CHECK(wrong_kind->configure(fallback, Rect2i(0, 0, 4, 4), 0, 2, generation, fallback) == ERR_UNAVAILABLE);

	Ref<DrawableTexture2D> no_mips;
	no_mips.instantiate();
	REQUIRE(no_mips->setup_checked(8, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), false) == OK);
	Ref<CanvasTexturePageView> missing_mips;
	missing_mips.instantiate();
	CHECK(missing_mips->configure(no_mips, Rect2i(0, 0, 4, 4), 0, 2, RS::get_singleton()->texture_drawable_get_generation(no_mips->get_rid()), fallback) == ERR_INVALID_PARAMETER);

	Ref<DrawableTexture2D> odd_page;
	odd_page.instantiate();
	REQUIRE(odd_page->setup_checked(9, 8, DrawableTexture2D::DRAWABLE_FORMAT_RGBA8, Color(0, 0, 0, 0), true) == OK);
	Ref<CanvasTexturePageView> non_divisible;
	non_divisible.instantiate();
	CHECK(non_divisible->configure(odd_page, Rect2i(0, 0, 4, 4), 0, 1, RS::get_singleton()->texture_drawable_get_generation(odd_page->get_rid()), fallback) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;

	Ref<CanvasTexturePageView> shared;
	shared.instantiate();
	REQUIRE(shared->configure(page, Rect2i(0, 0, 4, 4), 0, 2, generation, shared_fallback) == OK);
	CHECK(shared->get_size() == Size2(4, 4));
	CHECK(shared->get_rid() == shared_fallback->get_rid());

	Ref<CanvasTexturePageView> absent;
	absent.instantiate();
	REQUIRE(absent->configure(page, Rect2i(4, 0, 4, 4), 0, 2, generation, Ref<Texture2D>()) == OK);
	CHECK(absent->get_size() == Size2(4, 4));
	CHECK(absent->get_rid().is_null());
	CHECK(absent->get_image().is_null());
	CHECK(absent->has_alpha());
	const RID canvas_item = RS::get_singleton()->canvas_item_create();
	absent->draw(canvas_item, Point2());
	RS::get_singleton()->free_rid(canvas_item);
}

TEST_CASE("[CanvasTexturePageView] page command geometry validation") {
	static_assert(RendererCanvasRender::CANVAS_RECT_REGION_SAMPLING == (1 << 9));
	static_assert(RendererCanvasRender::CANVAS_RECT_ARRAY_LAYER == (1 << 10));

	RendererCanvasRender::Item::CommandTexturePageRect command;
	command.rect = Rect2(0, 0, 16, 16);
	command.source = Rect2(4, 0, 4, 4);
	command.sampler_domain = Rect2(4, 0, 4, 4);
	command.fallback_source = Rect2();
	command.expected_generation = 1;
	command.max_region_lod = 2;
	command.flags = RendererCanvasRender::CANVAS_RECT_REGION | RendererCanvasRender::CANVAS_RECT_REGION_SAMPLING;
	CHECK(command.is_geometry_valid(Size2i(8, 8), 4));
	CHECK_FALSE(command.is_geometry_valid(Size2i(8, 8), 1));

	command.source.position = Point2(7, 0);
	CHECK_FALSE(command.is_geometry_valid(Size2i(8, 8), 4));
	command.source.position = Point2(4, 0);

	command.sampler_domain.position = Point2(6, 0);
	CHECK_FALSE(command.is_geometry_valid(Size2i(8, 8), 4));
	command.sampler_domain.position = Point2(4, 0);

	command.max_region_lod = 3;
	CHECK_FALSE(command.is_geometry_valid(Size2i(8, 8), 4));
	command.max_region_lod = 2;

	command.flags |= RendererCanvasRender::CANVAS_RECT_TILE;
	CHECK_FALSE(command.is_geometry_valid(Size2i(8, 8), 4));
	command.flags &= ~RendererCanvasRender::CANVAS_RECT_TILE;

	command.max_region_lod = 1;
	CHECK_FALSE(command.is_geometry_valid(Size2i(9, 8), 4));

	command.source = Rect2(3072, 3072, 1024, 1024);
	command.sampler_domain = command.source;
	command.max_region_lod = 10;
	CHECK(command.is_geometry_valid(Size2i(4096, 4096), 13));
}

} // namespace TestDrawableTexture2D
