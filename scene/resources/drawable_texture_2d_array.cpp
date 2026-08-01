/**************************************************************************/
/*  drawable_texture_2d_array.cpp                                         */
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

#include "drawable_texture_2d_array.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Error DrawableTexture2DArray::setup(int p_width, int p_height, int p_layers, DrawableTexture2D::DrawableFormat p_format, const Color &p_color, bool p_use_mipmaps) {
	ERR_FAIL_COND_V(texture.is_valid(), ERR_ALREADY_IN_USE);

	RID candidate = RenderingServer::get_singleton()->texture_drawable_layered_create(
			p_width,
			p_height,
			p_layers,
			static_cast<RSE::TextureDrawableFormat>(p_format),
			p_color,
			p_use_mipmaps);
	ERR_FAIL_COND_V(candidate.is_null(), ERR_CANT_CREATE);

	const uint64_t candidate_generation = RenderingServer::get_singleton()->texture_drawable_get_generation(candidate);
	if (candidate_generation == 0) {
		RenderingServer::get_singleton()->free_rid(candidate);
		return ERR_CANT_CREATE;
	}

	texture = candidate;
	width = p_width;
	height = p_height;
	layers = p_layers;
	mipmaps = p_use_mipmaps;
	drawable_format = p_format;
	generation = candidate_generation;
	emit_changed();
	return OK;
}

Image::Format DrawableTexture2DArray::get_format() const {
	switch (drawable_format) {
		case DrawableTexture2D::DRAWABLE_FORMAT_RGBA8:
		case DrawableTexture2D::DRAWABLE_FORMAT_RGBA8_SRGB:
			return Image::FORMAT_RGBA8;
		case DrawableTexture2D::DRAWABLE_FORMAT_RGBAH:
			return Image::FORMAT_RGBAH;
		case DrawableTexture2D::DRAWABLE_FORMAT_RGBAF:
			return Image::FORMAT_RGBAF;
		default:
			return Image::FORMAT_MAX;
	}
}

TextureLayered::LayeredType DrawableTexture2DArray::get_layered_type() const {
	return LAYERED_TYPE_2D_ARRAY;
}

int DrawableTexture2DArray::get_width() const {
	return width;
}

int DrawableTexture2DArray::get_height() const {
	return height;
}

int DrawableTexture2DArray::get_layers() const {
	return layers;
}

bool DrawableTexture2DArray::has_mipmaps() const {
	return mipmaps;
}

Ref<Image> DrawableTexture2DArray::get_layer_data(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, layers, Ref<Image>());
	ERR_FAIL_COND_V(texture.is_null(), Ref<Image>());

	const Image::Format image_format = get_format();
	ERR_FAIL_COND_V(image_format == Image::FORMAT_MAX, Ref<Image>());
	const int mipmap_count = mipmaps ? Image::get_image_required_mipmaps(width, height, image_format) + 1 : 1;
	Vector<uint8_t> data;
	for (int mipmap = 0; mipmap < mipmap_count; mipmap++) {
		const Ref<Image> image = RenderingServer::get_singleton()->texture_drawable_get_subresource(texture, mipmap, generation, p_layer);
		ERR_FAIL_COND_V(image.is_null(), Ref<Image>());
		ERR_FAIL_COND_V(image->get_width() != MAX(1, width >> mipmap) ||
						image->get_height() != MAX(1, height >> mipmap) ||
						image->get_format() != image_format ||
						image->has_mipmaps(),
				Ref<Image>());
		data.append_array(image->get_data());
	}

	ERR_FAIL_COND_V(data.size() != Image::get_image_data_size(width, height, image_format, mipmaps), Ref<Image>());
	return Image::create_from_data(width, height, mipmaps, image_format, data);
}

RID DrawableTexture2DArray::get_rid() const {
	return texture;
}

uint64_t DrawableTexture2DArray::get_generation() const {
	return generation;
}

void DrawableTexture2DArray::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "width", "height", "layers", "format", "color", "use_mipmaps"), &DrawableTexture2DArray::setup, DEFVAL(Color(0, 0, 0, 0)), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_generation"), &DrawableTexture2DArray::get_generation);
}

DrawableTexture2DArray::~DrawableTexture2DArray() {
	if (texture.is_valid()) {
		ERR_FAIL_NULL(RenderingServer::get_singleton());
		RenderingServer::get_singleton()->free_rid(texture);
	}
}
