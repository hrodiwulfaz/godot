/**************************************************************************/
/*  pipeline_hash_map_rd.cpp                                              */
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

#include "pipeline_hash_map_rd.h"

Mutex PipelineCompilationRD::retired_mutex;
LocalVector<PipelineCompilationRD *> PipelineCompilationRD::retired;

void PipelineCompilationRD::retire(PipelineCompilationRD *p_compilation) {
	MutexLock lock(retired_mutex);
	retired.push_back(p_compilation);
}

void PipelineCompilationRD::release_completed_retired() {
	thread_local LocalVector<PipelineCompilationRD *> completed;
	completed.clear();
	{
		MutexLock lock(retired_mutex);
		for (uint32_t i = 0; i < retired.size();) {
			if (WorkerThreadPool::get_singleton()->is_task_completed(retired[i]->task_id)) {
				completed.push_back(retired[i]);
				retired.remove_at_unordered(i);
			} else {
				i++;
			}
		}
	}

	// The tasks are complete, so waiting only releases their records.
	for (PipelineCompilationRD *compilation : completed) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(compilation->task_id);
		memdelete(compilation);
	}
}

void PipelineCompilationRD::release_all_retired() {
	LocalVector<PipelineCompilationRD *> all;
	{
		MutexLock lock(retired_mutex);
		all = retired;
		retired.clear();
	}

	for (PipelineCompilationRD *compilation : all) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(compilation->task_id);
		memdelete(compilation);
	}
}
