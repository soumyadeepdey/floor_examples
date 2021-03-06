/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2017 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FLOOR_CAMERA_HPP__
#define __FLOOR_CAMERA_HPP__

#include <floor/floor/floor.hpp>
#include <floor/core/core.hpp>
#include <floor/core/event.hpp>
#include <floor/math/quaternion.hpp>
#include <chrono>

//! floor camera functions
class camera {
public:
	camera();
	~camera();

	void run();

	void set_position(const float& x, const float& y, const float& z);
	void set_position(const float3& pos);
	float3& get_position();
	const float3& get_position() const;
	const float3& get_prev_position() const;
	
	void set_rotation(const float& x, const float& y);
	void set_rotation(const float2& rot);
	float2& get_rotation();
	const float2& get_rotation() const;
	void set_rotation_wrapping(const bool& state);
	const bool& get_rotation_wrapping() const;
	
	void set_single_frame(const bool& state);
	float3 get_single_frame_direction() const;
	const quaternionf& get_single_frame_rotation() const;

	void set_rotation_speed(const float& speed);
	float get_rotation_speed() const;
	void set_cam_speed(const float& speed);
	float get_cam_speed() const;

	void set_keyboard_input(const bool& state);
	bool get_keyboard_input() const;
	void set_mouse_input(const bool& state);
	bool get_mouse_input() const;
	void set_wasd_input(const bool& state);
	bool get_wasd_input() const;
	
	float3 get_direction() const;
	static float3 get_direction(const float2 rotation);

protected:
	event* evt;

	float3 position, prev_position;
	float2 rotation;
	
	bool is_single_frame = false;
	quaternionf single_frame_quat;
	float2 single_frame_direction;
	float single_frame_distance = 0.0f;
	
	float up_down = 0.0f;
	float rotation_speed = 150.0f;
	float cam_speed = 75.0f;

	bool keyboard_input = true;
	bool mouse_input = false;
	bool wasd_input = false;
	bool rotation_wrapping = true;
	unsigned int ignore_next_rotation = 0;
	
	// hack for mice on os x 10.12+ that don't have proper warp support
	bool delta_hack { false };
	int2 last_delta;
	
	// [right, left, up, down]
	atomic<bool> key_state[4] { { false }, { false }, { false }, { false } };
	
	event::handler keyboard_handler;
	bool key_handler(EVENT_TYPE type, shared_ptr<event_object> obj);
	
	//
	chrono::time_point<chrono::high_resolution_clock> time_keeper { chrono::high_resolution_clock::now() };
	
};

#endif
