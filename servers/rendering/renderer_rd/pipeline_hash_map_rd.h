/**************************************************************************/
/*  pipeline_hash_map_rd.h                                                */
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

#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rb_map.h"
#include "core/templates/rb_set.h"
#include "core/templates/rid.h"
#include "core/templates/vector.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server_enums.h"

#include <atomic>

#define PRINT_PIPELINE_COMPILATION_KEYS 0

// A background pipeline compilation. The worker pool keeps a task's record until someone waits for it, and the task reads this state when it starts, so both stay alive until the task completes.
class PipelineCompilationRD {
	static Mutex retired_mutex;
	static LocalVector<PipelineCompilationRD *> retired;

public:
	enum State : uint32_t {
		STATE_QUEUED,
		STATE_RUNNING,
		STATE_DONE,
		// Taken from the task before it started: a waiting thread built the pipeline itself, or the pipelines were cleared.
		STATE_TAKEN,
	};

	std::atomic<uint32_t> state = { STATE_QUEUED };
	WorkerThreadPool::TaskID task_id = WorkerThreadPool::INVALID_TASK_ID;

	// Returns true if the task had not started; it then returns without building anything.
	bool take() {
		uint32_t expected = STATE_QUEUED;
		return state.compare_exchange_strong(expected, STATE_TAKEN);
	}

	bool start() {
		uint32_t expected = STATE_QUEUED;
		return state.compare_exchange_strong(expected, STATE_RUNNING);
	}

	// Keeps a compilation that no map tracks anymore until its task completes.
	static void retire(PipelineCompilationRD *p_compilation);
	// Releases the retired compilations whose tasks completed. It never waits for a task.
	static void release_completed_retired();
	// Waits for and releases every retired compilation.
	static void release_all_retired();

	virtual ~PipelineCompilationRD() {}
};

template <typename Key, typename CreationClass, typename CreationFunction>
class PipelineHashMapRD {
private:
	struct Compilation : public PipelineCompilationRD {
		PipelineHashMapRD *map = nullptr;
		Key key;
		uint32_t key_hash = 0;
	};

	CreationClass *creation_object = nullptr;
	CreationFunction creation_function = nullptr;
	Mutex *compilations_mutex = nullptr;
	uint32_t *compilations = nullptr;
	std::atomic<uint32_t> *waits = nullptr;
	RBMap<uint32_t, RID> hash_map;
	LocalVector<Pair<uint32_t, RID>> compiled_queue;
	Mutex compiled_queue_mutex;
	RBSet<uint32_t> compilation_set;
	HashMap<uint32_t, Compilation *> compilation_tasks;
	// Compilations whose pipeline was published or built by a waiting thread. They are released once their task completes.
	LocalVector<Compilation *> published_tasks;
	Mutex local_mutex;

	// Number of pipelines published by the calling thread.
	static uint32_t &_published_on_thread() {
		thread_local uint32_t published = 0;
		return published;
	}

	// A failed creation publishes nothing, so an empty pipeline is published for it: the hash is not built again and its task is released like a finished one.
	void _build_pipeline(const Key &p_key, uint32_t p_key_hash) {
		const uint32_t published = _published_on_thread();
		(creation_object->*creation_function)(p_key);
		if (_published_on_thread() == published) {
			add_compiled_pipeline(p_key_hash, RID());
		}
	}

	static void _compile_task(void *p_compilation) {
		Compilation *compilation = static_cast<Compilation *>(p_compilation);
		if (!compilation->start()) {
			return;
		}

		compilation->map->_build_pipeline(compilation->key, compilation->key_hash);
		compilation->state.store(PipelineCompilationRD::STATE_DONE);
	}

	void _count_compilation(uint32_t p_key_hash, RSE::PipelineSource p_source) {
		if (compilations_mutex != nullptr) {
			MutexLock compilations_lock(*compilations_mutex);
			compilations[p_source]++;
		}

#if PRINT_PIPELINE_COMPILATION_KEYS
		String source_name = "UNKNOWN";
		switch (p_source) {
			case RSE::PIPELINE_SOURCE_CANVAS:
				source_name = "CANVAS";
				break;
			case RSE::PIPELINE_SOURCE_MESH:
				source_name = "MESH";
				break;
			case RSE::PIPELINE_SOURCE_SURFACE:
				source_name = "SURFACE";
				break;
			case RSE::PIPELINE_SOURCE_DRAW:
				source_name = "DRAW";
				break;
			case RSE::PIPELINE_SOURCE_SPECIALIZATION:
				source_name = "SPECIALIZATION";
				break;
		}

		print_line("HASH:", p_key_hash, "SOURCE:", source_name);
#endif
	}

	void _count_wait() {
		if (waits != nullptr) {
			waits->fetch_add(1, std::memory_order_relaxed);
		}
	}

	bool _add_new_pipelines_to_map() {
		thread_local Vector<uint32_t> hashes_added;
		hashes_added.clear();

		{
			MutexLock lock(compiled_queue_mutex);
			for (const Pair<uint32_t, RID> &pair : compiled_queue) {
				hash_map[pair.first] = pair.second;
				hashes_added.push_back(pair.first);
			}

			compiled_queue.clear();
		}

		thread_local LocalVector<Compilation *> completed_tasks;
		completed_tasks.clear();
		{
			MutexLock local_lock(local_mutex);
			for (uint32_t hash : hashes_added) {
				typename HashMap<uint32_t, Compilation *>::Iterator task_it = compilation_tasks.find(hash);
				if (task_it != compilation_tasks.end()) {
					published_tasks.push_back(task_it->value);
					compilation_tasks.remove(task_it);
				}
			}
			for (uint32_t i = 0; i < published_tasks.size();) {
				if (WorkerThreadPool::get_singleton()->is_task_completed(published_tasks[i]->task_id)) {
					completed_tasks.push_back(published_tasks[i]);
					published_tasks.remove_at_unordered(i);
				} else {
					i++;
				}
			}
		}

		// The tasks are complete, so waiting only releases their records.
		for (Compilation *compilation : completed_tasks) {
			WorkerThreadPool::get_singleton()->wait_for_task_completion(compilation->task_id);
			memdelete(compilation);
		}

		return !hashes_added.is_empty();
	}

	// Tasks that have not started are taken and released later, so only running tasks are waited for.
	void _stop_all_compilations() {
		thread_local LocalVector<Compilation *> running_tasks;
		running_tasks.clear();
		{
			MutexLock local_lock(local_mutex);
			for (KeyValue<uint32_t, Compilation *> key_value : compilation_tasks) {
				published_tasks.push_back(key_value.value);
			}
			for (Compilation *compilation : published_tasks) {
				if (compilation->take() || compilation->state.load() != PipelineCompilationRD::STATE_RUNNING) {
					PipelineCompilationRD::retire(compilation);
				} else {
					running_tasks.push_back(compilation);
				}
			}
			compilation_tasks.clear();
			published_tasks.clear();
		}

		for (Compilation *compilation : running_tasks) {
			if (WorkerThreadPool::get_singleton()->wait_for_task_completion(compilation->task_id) == OK) {
				memdelete(compilation);
			} else {
				PipelineCompilationRD::retire(compilation);
			}
		}
	}

	// Builds a pipeline on the calling thread, or waits for its task if the task already started. Returns false if there was nothing to build or wait for.
	bool _build_or_wait(uint32_t p_key_hash) {
		Compilation *running = nullptr;
		Key key;
		bool build = false;
		{
			MutexLock local_lock(local_mutex);
			if (!compilation_set.has(p_key_hash)) {
				// The pipeline was never submitted, we can't wait for it.
				return false;
			}

			typename HashMap<uint32_t, Compilation *>::Iterator task_it = compilation_tasks.find(p_key_hash);
			if (task_it == compilation_tasks.end()) {
				return false;
			}

			Compilation *compilation = task_it->value;
			compilation_tasks.remove(task_it);
			if (compilation->take()) {
				build = true;
				key = compilation->key;
				published_tasks.push_back(compilation);
			} else if (compilation->state.load() == PipelineCompilationRD::STATE_RUNNING) {
				running = compilation;
			} else {
				published_tasks.push_back(compilation);
			}
		}

		if (build) {
			_build_pipeline(key, p_key_hash);
			return true;
		}

		if (running != nullptr) {
			if (WorkerThreadPool::get_singleton()->wait_for_task_completion(running->task_id) == OK) {
				memdelete(running);
			} else {
				MutexLock local_lock(local_mutex);
				published_tasks.push_back(running);
			}
			return true;
		}

		return false;
	}

public:
	void add_compiled_pipeline(uint32_t p_hash, RID p_pipeline) {
		compiled_queue_mutex.lock();
		compiled_queue.push_back({ p_hash, p_pipeline });
		compiled_queue_mutex.unlock();
		_published_on_thread()++;
	}

	// Start compilation of a pipeline ahead of time in the background. Returns true if the compilation was started, false if it wasn't required. Source is only used for collecting statistics.
	bool compile_pipeline(const Key &p_key, uint32_t p_key_hash, RSE::PipelineSource p_source, bool p_high_priority) {
		DEV_ASSERT((creation_object != nullptr) && (creation_function != nullptr) && "Creation object and function was not set before attempting to compile a pipeline.");

		MutexLock local_lock(local_mutex);
		if (compilation_set.has(p_key_hash)) {
			// Check if the pipeline was already submitted.
			return false;
		}

		// Record the pipeline as submitted, a task can't be started for it again.
		compilation_set.insert(p_key_hash);
		_count_compilation(p_key_hash, p_source);

		// Queue a background compilation task.
		Compilation *compilation = memnew(Compilation);
		compilation->map = this;
		compilation->key = p_key;
		compilation->key_hash = p_key_hash;
		compilation->task_id = WorkerThreadPool::get_singleton()->add_native_task(&PipelineHashMapRD::_compile_task, compilation, p_high_priority, "PipelineCompilation");
		compilation_tasks.insert(p_key_hash, compilation);
		return true;
	}

	// Makes sure a submitted pipeline is built. A task that has not started yet is taken and built on the calling thread instead of waiting for the tasks queued before it.
	void wait_for_pipeline(uint32_t p_key_hash) {
		if (_build_or_wait(p_key_hash)) {
			_count_wait();
		}
	}

	// Imports the pipelines finished in the background and releases the records of completed tasks. It never waits. Returns true while background compilations are still outstanding.
	bool release_completed_compilations() {
		_add_new_pipelines_to_map();
		MutexLock local_lock(local_mutex);
		return !compilation_tasks.is_empty() || !published_tasks.is_empty();
	}

	// Retrieve a pipeline only if it's already compiled. It doesn't request a compilation.
	RID get_compiled_pipeline(uint32_t p_key_hash) {
		RBMap<uint32_t, RID>::Element *e = hash_map.find(p_key_hash);
		if (e == nullptr && _add_new_pipelines_to_map()) {
			e = hash_map.find(p_key_hash);
		}
		return e != nullptr ? e->value() : RID();
	}

	// Retrieve a pipeline. It'll return an empty pipeline if it's not available yet, but it'll be guaranteed to succeed if 'wait for compilation' is true and stall as necessary. Source is just an optional number to aid debugging.
	RID get_pipeline(const Key &p_key, uint32_t p_key_hash, bool p_wait_for_compilation, RSE::PipelineSource p_source) {
		RBMap<uint32_t, RID>::Element *e = hash_map.find(p_key_hash);

		if (e == nullptr) {
			// Check if there's any new pipelines that need to be added and try again. This method triggers a mutex lock.
			if (_add_new_pipelines_to_map()) {
				e = hash_map.find(p_key_hash);
			}
		}

		if (e == nullptr) {
			if (!p_wait_for_compilation) {
				// Request compilation. The method will ignore the request if it's already being compiled.
				compile_pipeline(p_key, p_key_hash, p_source, false);
				return RID();
			}

			bool build = false;
			{
				MutexLock local_lock(local_mutex);
				if (!compilation_set.has(p_key_hash)) {
					// Nothing was submitted, so the pipeline is built here instead of in a task this thread would wait for.
					compilation_set.insert(p_key_hash);
					_count_compilation(p_key_hash, p_source);
					build = true;
				}
			}

			if (build) {
				_build_pipeline(p_key, p_key_hash);
				_count_wait();
			} else {
				wait_for_pipeline(p_key_hash);
			}
			_add_new_pipelines_to_map();

			e = hash_map.find(p_key_hash);
			if (e != nullptr) {
				return e->value();
			} else {
				// Pipeline could not be compiled due to an internal error. Store an empty RID so compilation is not attempted again.
				hash_map[p_key_hash] = RID();
				return RID();
			}
		} else {
			return e->value();
		}
	}

	// Delete all cached pipelines. Compilations that have not started are dropped; only running ones are waited for.
	void clear_pipelines() {
		_stop_all_compilations();
		_add_new_pipelines_to_map();

		for (KeyValue<uint32_t, RID> entry : hash_map) {
			if (entry.value.is_valid()) {
				RD::get_singleton()->free_rid(entry.value);
			}
		}

		hash_map.clear();
		compilation_set.clear();
	}

	// Set the external pipeline compilations array to increase the counters on every time a pipeline is compiled.
	void set_compilations(uint32_t *p_compilations, Mutex *p_compilations_mutex) {
		compilations = p_compilations;
		compilations_mutex = p_compilations_mutex;
	}

	// Set the external counter increased every time a thread waits for a pipeline to be built.
	void set_waits(std::atomic<uint32_t> *p_waits) {
		waits = p_waits;
	}

	void set_creation_object_and_function(CreationClass *p_creation_object, CreationFunction p_creation_function) {
		creation_object = p_creation_object;
		creation_function = p_creation_function;
	}

	PipelineHashMapRD() {}

	~PipelineHashMapRD() {
		clear_pipelines();
	}
};
