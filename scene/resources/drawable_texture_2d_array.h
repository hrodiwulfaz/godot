/**************************************************************************/
/*  drawable_texture_2d_array.h                                           */
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

#pragma once

#include "scene/resources/drawable_texture_2d.h"

class DrawableTexture2DArray : public TextureLayered {
	GDCLASS(DrawableTexture2DArray, TextureLayered);
	RES_BASE_EXTENSION("tex");

	mutable RID texture;
	int width = 0;
	int height = 0;
	int layers = 0;
	bool mipmaps = false;
	DrawableTexture2D::DrawableFormat drawable_format = DrawableTexture2D::DRAWABLE_FORMAT_RGBA8;
	uint64_t generation = 0;

protected:
	static void _bind_methods();

public:
	Error setup(int p_width, int p_height, int p_layers, DrawableTexture2D::DrawableFormat p_format, const Color &p_color = Color(0, 0, 0, 0), bool p_use_mipmaps = false);

	virtual Image::Format get_format() const override;
	virtual LayeredType get_layered_type() const override;
	virtual int get_width() const override;
	virtual int get_height() const override;
	virtual int get_layers() const override;
	virtual bool has_mipmaps() const override;
	virtual Ref<Image> get_layer_data(int p_layer) const override;
	virtual RID get_rid() const override;

	uint64_t get_generation() const;

	~DrawableTexture2DArray();
};
