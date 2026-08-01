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
#include "scene/resources/drawable_texture_2d.h"
#include "scene/resources/image_texture.h"
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

} // namespace TestDrawableTexture2D
