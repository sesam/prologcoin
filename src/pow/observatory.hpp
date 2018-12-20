#pragma once
#ifndef _pow_observatory_hpp
#define _pow_observatory_hpp

#include "galaxy.hpp"
#include "camera.hpp"
#include "siphash.hpp"
#include "dipper_detector.hpp"
#include "../common/utime.hpp"
#include <boost/thread.hpp>
#include <fstream>
#include <math.h>

namespace prologcoin { namespace pow {

template<size_t N, typename T> class observatory {
public:
    inline observatory() : keys_("hella42", 7), galaxy_(keys_) { }

    void init(const char *msg, size_t len, size_t num_stars = 0);

    inline const siphash_keys & keys() const {
	return keys_;
    }

    void set_target(const vec3<T> &v, size_t cam_id = 0);
    const vec3<T> & get_target(size_t cam_id = 0) const;
    void set_target(size_t proof_num, size_t index, size_t cam_id = 0);

    size_t new_camera();

    inline star get_star(uint32_t id) const {
	uint64_t out[3];
	siphash(keys_, common::checked_cast<uint64_t>(3*id),
		common::checked_cast<uint64_t>(3*id+3), out);
	return star(id, out[0], out[1], out[2]);
    }

    void take_picture(std::vector<projected_star> &stars, size_t cam_id = 0) const;

    bool scan(size_t proof_num, std::vector<projected_star> &found, size_t &nonce);

    void status() const; 
    void memory() const; 

    size_t num_buckets() const;
    T step_vector_length() const;

private:
    inline galaxy<N, T> & get_galaxy() {
	return galaxy_;
    }

    inline camera<N, T> & get_camera(size_t id = 0) {
	return cameras_[id];
    }

    inline const galaxy<N, T> & get_galaxy() const {
	return galaxy_;
    }

    inline const camera<N, T> & get_camera(size_t id = 0) const {
	return cameras_[id];
    }

    siphash_keys keys_;
    galaxy<N, T> galaxy_;
    std::vector<camera<N, T> > cameras_;
};

template<size_t N, typename T> void observatory<N,T>::init(const char *msg, size_t len, size_t num_stars)
{
    keys_ = siphash_keys(msg, len);
    if (num_stars == 0) {
	galaxy_.init();
    } else {
	galaxy_.init(num_stars);
    }
    cameras_.clear();
    new_camera(); // Default camera id=0
}

template<size_t N, typename T> size_t observatory<N,T>::new_camera() {
    size_t id = cameras_.size();
    camera<N, T> cam(galaxy_, id);
    cameras_.push_back(cam);
    return id;
}

template<size_t N, typename T> size_t observatory<N,T>::num_buckets() const {
    return get_galaxy().num_buckets;
}

template<size_t N, typename T> T observatory<N,T>::step_vector_length() const {
    return get_galaxy().step_vector_length();
}

template<size_t N, typename T> void observatory<N,T>::status() const {
    get_galaxy().status();
}

template<size_t N, typename T> void observatory<N,T>::memory() const {
    get_galaxy().memory();
}


template<size_t N, typename T> void observatory<N,T>::set_target(const vec3<T> &v, size_t cam_id)
{
    get_camera(cam_id).set_target(v);
}

template<size_t N, typename T> void observatory<N,T>::set_target(size_t proof_num, size_t index, size_t cam_id)
{
    get_camera(cam_id).set_target(proof_num, index);
}

template<size_t N, typename T> const vec3<T> & observatory<N,T>::get_target(size_t cam_id) const
{
    return get_camera(cam_id).get_target();
}

template<size_t N, typename T> void observatory<N,T>::take_picture(std::vector<projected_star> &stars, size_t cam_id) const
{
    get_camera(cam_id).take_picture(stars);
}

template<size_t N, typename T> class worker_pool;

template<size_t N, typename T> class worker {
public:
    worker(worker_pool<N,T> &workers);

    worker(const worker &other) = delete;
    void operator = (const worker &other) = delete;

    inline void join() {
	
    }

    inline void kill() {
	killed_ = true;
	set_ready(false);
    }

    inline bool is_ready() const {
	return ready_;
    }

    inline void set_ready(bool r) {
	boost::unique_lock<boost::mutex> lockit(ready_lock_);
	ready_ = r;
	ready_cv_.notify_one();
    }

    inline bool is_done() const {
	return found_done_;
    }
    
    inline void set_index_range(size_t proof_num, size_t index_start, size_t index_end) {
	proof_num_ = proof_num;
	index_ = index_start;
	index_end_ = index_end;
	found_done_ = false;
    }

    inline size_t index() const {
	return index_;
    }

    void set_target(size_t proof_num, size_t index);
    void take_picture();
    void run();

    inline const std::vector<projected_star> & get_found() const {
	return found_;
    }

private:
    worker_pool<N,T> &workers_;
    std::vector<projected_star> stars_;
    std::vector<projected_star> found_;
    dipper_detector detector_;
    size_t cam_id_;
    bool ready_;
    bool killed_;
    boost::mutex ready_lock_;
    boost::condition_variable ready_cv_;
    size_t proof_num_, index_, index_end_;
    bool found_done_;
};

template<size_t N, typename T> class worker_pool {
public:
    static const size_t DEFAULT_NUM_WORKERS = 1;

    worker_pool(observatory<N,T> &obs, size_t num_workers = DEFAULT_NUM_WORKERS) : observatory_(obs), num_workers_(num_workers), busy_count_(0) {
	for (size_t i = 0; i < num_workers_; i++) {
	    auto *w = new worker<N,T>(*this);
	    all_workers_.push_back(w);
	    ready_workers_.push_back(w);
	}
	for (auto *w : ready_workers_) {
	    threads_.create_thread( [=]{w->run();} );
	}
    }

    void kill_all_workers() {
	boost::unique_lock<boost::mutex> lockit(workers_lock_);
	for (auto *w : all_workers_) {
	    w->kill();
	}
    }
    
    worker<N,T> & find_ready_worker() {
	boost::unique_lock<boost::mutex> lockit(workers_lock_);
	while (busy_count_ == num_workers_) {
	    workers_cv_.wait(lockit);
	}
	worker<N,T> *w = ready_workers_.back();
	ready_workers_.pop_back();
	busy_count_++;
	return *w;
    }

    void wait_until_no_more_busy_workers() {
	{
	    boost::unique_lock<boost::mutex> lockit(workers_lock_);
	    while (busy_count_ != 0) {
		workers_cv_.wait(lockit);
	    }
	}
	threads_.join_all();
    }

    worker<N,T> * find_successful_worker() {
	for (auto *worker : all_workers_) {
	    if (worker->is_done()) {
		return worker;
	    }
	}
	return nullptr;
    }

    void add_ready_worker(worker<N,T> *w) {
	boost::unique_lock<boost::mutex> lockit(workers_lock_);
	busy_count_--;
	ready_workers_.push_back(w);
	workers_cv_.notify_all();
    }

    void remove_worker(worker<N,T> *w) {
	boost::unique_lock<boost::mutex> lockit(workers_lock_);
	busy_count_--;
	workers_cv_.notify_all();
    }

private:
    observatory<N,T> observatory_;
    size_t num_workers_;
    std::vector<worker<N,T> *> all_workers_;
    std::vector<worker<N,T> *> ready_workers_;
    boost::mutex workers_lock_;
    boost::condition_variable workers_cv_;
    size_t busy_count_;
    boost::thread_group threads_;

    friend class worker<N,T>;
};

template<size_t N, typename T> worker<N,T>::worker(worker_pool<N,T> &workers) 
    : workers_(workers), detector_(stars_), ready_(true), killed_(false), proof_num_(0), index_(0), index_end_(0), found_done_(false) {
    cam_id_ = workers_.observatory_.new_camera();
}

template<size_t N, typename T> void worker<N,T>::set_target(size_t proof_num, size_t index) {
    workers_.observatory_.set_target(proof_num, index, cam_id_);
}

template<size_t N, typename T> void worker<N,T>::take_picture() {
    workers_.observatory_.take_picture(stars_, cam_id_);
}

template<size_t N, typename T> void worker<N,T>::run() {
    while (!killed_) {
	boost::unique_lock<boost::mutex> lockit(ready_lock_);
	while (ready_) {
	    ready_cv_.wait(lockit);
	}
	if (!killed_) {
	    // std::cout << "Worker: " << cam_id_ << " index=" << index_ << std::endl;
	    for (; index_ != index_end_; index_++) {
		set_target(proof_num_, index_);
		take_picture();
		if (detector_.search(found_)) {
		    // std::cout << "FOUND IT!" << std::endl;
		    found_done_ = true;
		    break;
		}
	    }
    	    ready_ = true;
	    workers_.add_ready_worker(this);
	}
    }
    // Don't add this to the ready queue (as it is killed) but
    // reduce busy count as we go out of scope and the thread will die.
    workers_.remove_worker(this);
}

template<size_t N, typename T> bool observatory<N,T>::scan(size_t proof_num, std::vector<projected_star> &found, size_t &nonce)
{
    using namespace prologcoin::common;

    worker_pool<N,T> workers(*this);

    auto start_clock = utime::now_seconds();

    size_t index = 0, index_delta = 100;
    bool is_done = false;

    while (!is_done) {
	auto &worker = workers.find_ready_worker();
	if (worker.is_done()) {
	    is_done = true;
	    break;
	}
	worker.set_index_range(proof_num, index, index + index_delta);
	index += index_delta;
	worker.set_ready(false);
    }
    // std::cout << "kill all workers..." << std::endl;
    workers.kill_all_workers();
    // std::cout << "wait until all workers notify..." << std::endl;
    workers.wait_until_no_more_busy_workers();
    auto *w = workers.find_successful_worker();
    if (w) {
	found = w->get_found();
	nonce = w->index();
    }

    auto end_clock = utime::now_seconds();
    auto dt = end_clock - start_clock;
    (void)dt;

    // std::cout << "observatory::scan end: total_time=" << dt.in_ss() << std::endl;

    return w != nullptr;
}    

}}

#endif
