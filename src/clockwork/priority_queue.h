#ifndef _CLOCKWORK_PRIORITY_QUEUE_H_
#define _CLOCKWORK_PRIORITY_QUEUE_H_

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace clockwork {
	/* This is a priority queue, but one where priorities also define a minimum
time that an enqueued task is eligible to be dequeued.  The queue will block
if no eligible tasks are available */
template <typename T> class time_release_priority_queue {
private:
	struct container {
		T* element;

		// TODO: priority should be a chrono timepoint not the uint64_t, to avoid
		//       expensive conversions.  Or, a different clock altogether
		uint64_t priority;

		friend bool operator < (const container& lhs, const container &rhs) {
			return lhs.priority < rhs.priority;
		}
		friend bool operator > (const container& lhs, const container &rhs) {
			return lhs.priority > rhs.priority;
		}
	};

	std::atomic_bool alive;

	std::atomic_flag in_use;
	std::atomic_uint64_t version; 
	std::priority_queue<container, std::vector<container>, std::greater<container>> queue;

public:

	time_release_priority_queue() : alive(true), in_use(ATOMIC_FLAG_INIT), version(0) {}

	bool enqueue(T* element, uint64_t priority) {
		while (in_use.test_and_set());

		// TODO: will have to convert priority to a chrono::timepoint
		if (alive) {
			queue.push(container{element, priority});
			version++;
		}

		in_use.clear();

		return alive;
	}

	bool try_dequeue(T* &element) {
		while (in_use.test_and_set());

		if (!alive || queue.empty() || queue.top().priority > util::now()) {
			in_use.clear();
			return false;
		}

		element = queue.top().element;
		queue.pop();

		in_use.clear();
		return true;
	}

	T* dequeue() {
		while (alive) {
			while (in_use.test_and_set());

			if (queue.empty()) {
				uint64_t version_seen = version.load();
				in_use.clear();

				// Spin until something is enqueued
				while (alive && version.load() == version_seen);

			} else if (queue.top().priority > util::now()) {
				uint64_t next_eligible = queue.top().priority;
				uint64_t version_seen = version.load();
				in_use.clear();

				// Spin until the top element is eligible or something new is enqueued
				while (alive && version.load() == version_seen && next_eligible > util::now());

			} else {
				T* element = queue.top().element;
				queue.pop();
				in_use.clear();
				return element;

			}
		}
		return nullptr;
	}

	std::vector<T*> drain() {
		while (in_use.test_and_set());

		std::vector<T*> elements;
		while (!queue.empty()) {
			elements.push_back(queue.top().element);
			queue.pop();
		}

		in_use.clear();

		return elements;
	}

	void shutdown() {
		while (in_use.test_and_set());

		alive = false;
		version++;

		in_use.clear();
	}
	
};
}

#endif