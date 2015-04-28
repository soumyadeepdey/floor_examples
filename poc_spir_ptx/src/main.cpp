
#include <floor/floor/floor.hpp>
#include <floor/ios/ios_helper.hpp>

#include "poc_spir_ptx.hpp"

// uncomment this to enable host execution
//#define DEBUG_HOST_EXEC 1

int main(int, char* argv[]) {
#if !defined(FLOOR_IOS)
	floor::init(argv[0], (const char*)"../../data/"); // call path, data path
#else
	floor::init(argv[0], (const char*)"data/");
#endif
	
	// get the compute context that has been automatically created (opencl/cuda/metal/host, depending on the config)
	auto compute_ctx = floor::get_compute_context();
	
	// create a compute queue (aka command queue or stream) for the fastest device in the context
	auto fastest_device = compute_ctx->get_device(compute_device::TYPE::FASTEST);
	auto dev_queue = compute_ctx->create_queue(fastest_device);
	
	//
	//static const uint2 img_size { floor::get_width(), floor::get_height() };
	static const uint2 img_size { 512, 512 }; // fixed for now, b/c random function makes this look horrible at higher res
	static const uint32_t pixel_count { img_size.x * img_size.y };
	
#if !defined(DEBUG_HOST_EXEC)
	// compile the program and get the kernel function
#if !defined(FLOOR_IOS)
	auto path_tracer_prog = compute_ctx->add_program_file(floor::data_path("../poc_spir_ptx/src/poc_spir_ptx.cpp"), "-I" + floor::data_path("../poc_spir_ptx/src"));
#else
	// for now: use a precompiled metal lib instead of compiling at runtime
	const vector<llvm_compute::kernel_info> kernel_infos {
		{
			"path_trace",
			{ 12, 4, 4, 8 },
			{ llvm_compute::kernel_info::ARG_ADDRESS_SPACE::GLOBAL, llvm_compute::kernel_info::ARG_ADDRESS_SPACE::CONSTANT,
				llvm_compute::kernel_info::ARG_ADDRESS_SPACE::CONSTANT, llvm_compute::kernel_info::ARG_ADDRESS_SPACE::CONSTANT }
		}
	};
	auto path_tracer_prog = compute_ctx->add_precompiled_program_file(floor::data_path("poc_spir_ptx.metallib"), kernel_infos);
#endif
	if(path_tracer_prog == nullptr) {
		log_error("program compilation failed");
		return -1;
	}
	auto path_tracer_kernel = path_tracer_prog->get_kernel_fuzzy("path_trace"); // c++ name mangling is hard
	if(path_tracer_kernel == nullptr) {
		log_error("failed to retrieve kernel from program");
		return -1;
	}
	
	// create the image buffer on the device
	auto img_buffer = compute_ctx->create_buffer(fastest_device, sizeof(float3) * pixel_count);
#endif
	
	bool done = false;
	static constexpr const uint32_t iteration_count { 16384 };
#if defined(DEBUG_HOST_EXEC)
	vector<float3> img_data(pixel_count);
	for(uint32_t iteration = 0; iteration < iteration_count; ++iteration) {
		reset_counter(); // necessary workaround for now
		for(uint32_t i = 0; i < pixel_count; ++i) {
			path_trace((buffer<float3>)img_data.data(), iteration, core::rand<uint32_t>(), img_size);
		}
#else
	for(uint32_t iteration = 0; iteration < iteration_count; ++iteration) {
		dev_queue->execute(path_tracer_kernel,
						   // total amount of work:
						   size1 { pixel_count },
						   // work per work-group:
						   size1 { fastest_device->max_work_group_size },
						   // kernel arguments:
						   img_buffer, iteration, core::rand<uint32_t>(), img_size);
#endif

		// draw every 10th frame (except for the first 10 frames)
		if(iteration < 10 || iteration % 10 == 0) {
#if !defined(DEBUG_HOST_EXEC)
			// grab the current image buffer data (read-only + blocking) ...
			auto img_data = (float3*)img_buffer->map(dev_queue, COMPUTE_MEMORY_MAP_FLAG::READ | COMPUTE_MEMORY_MAP_FLAG::BLOCK);
#endif

			// path tracer output needs gamma correction (fixed 2.2), otherwise it'll look too dark
			const auto gamma_correct = [](const float3& color) {
				static constexpr const float gamma { 2.2f };
				float3 gamma_corrected {
					powf(color.r, 1.0f / gamma) * 255.0f,
					powf(color.g, 1.0f / gamma) * 255.0f,
					powf(color.b, 1.0f / gamma) * 255.0f
				};
				gamma_corrected.clamp(0.0f, 255.0f);
				return uchar3 { (uint8_t)gamma_corrected.x, (uint8_t)gamma_corrected.y, (uint8_t)gamma_corrected.z };
			};
			
			// ... and blit it into the window
			// (crude and I'd usually do this via GL, but this way it's not a requirement)
			const auto wnd_surface = SDL_GetWindowSurface(floor::get_window());
			SDL_LockSurface(wnd_surface);
			const uint2 render_dim = img_size.minned(uint2 { floor::get_width(), floor::get_height() });
			for(uint32_t y = 0; y < render_dim.y; ++y) {
				uint32_t* px_ptr = (uint32_t*)wnd_surface->pixels + ((size_t)wnd_surface->pitch / sizeof(uint32_t)) * y;
				uint32_t img_idx = img_size.x * y;
				for(uint32_t x = 0; x < render_dim.x; ++x, ++img_idx) {
					// map and gamma correct each pixel according to the window format
					const auto rgb = gamma_correct(img_data[img_idx]);
					*px_ptr++ = SDL_MapRGB(wnd_surface->format, rgb.r, rgb.g, rgb.b);
				}
			}
#if !defined(DEBUG_HOST_EXEC)
			img_buffer->unmap(dev_queue, img_data);
#endif
			
			SDL_UnlockSurface(wnd_surface);
			SDL_UpdateWindowSurface(floor::get_window());
		}
		floor::set_caption("frame #" + to_string(iteration + 1));
		
		// handle quit event
		SDL_Event event_handle;
		while(SDL_PollEvent(&event_handle)) {
			if(event_handle.type == SDL_QUIT) {
				done = true;
				break;
			}
		}
		if(done) break;
	}
	log_msg("done!");
	
	// kthxbye
	floor::destroy();
	return 0;
}
