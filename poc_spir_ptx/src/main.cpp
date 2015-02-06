
#include <floor/floor/floor.hpp>

#include "poc_spir_ptx.hpp"

// uncomment this to enable host execution (instead of using opencl/cuda)
//#define DEBUG_HOST_EXEC 1

int main(int, char* argv[]) {
	floor::init(argv[0], "../../data/"); // call path, data path
	
	// get the compute context that has been automatically created (either opencl or cuda, depending on the config)
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
	auto path_tracer_prog = compute_ctx->add_program_file(floor::data_path("../poc_spir_ptx/src/poc_spir_ptx.cpp"), "-I" + floor::data_path("../poc_spir_ptx/src"));
	if(path_tracer_prog == nullptr) {
		log_error("program compilation failed");
		return -1;
	}
	auto path_tracer_kernel = path_tracer_prog->get_kernel_fuzzy("path_trace"); // c++ name mangling is hard
	if(path_tracer_kernel == nullptr) {
		log_error("failed to retrieve kernel from program");
		return -1;
	}
	
	// create the image buffer on the device, map it to host accessible memory and initialize it
	auto img_buffer = compute_ctx->create_buffer(fastest_device, sizeof(float3) * pixel_count);
	auto mapped_img_data = img_buffer->map(dev_queue);
	fill_n((float3*)mapped_img_data, pixel_count, float3 { 0.0f });
	img_buffer->unmap(dev_queue, mapped_img_data);
#endif
	
	bool done = false;
	static constexpr const uint32_t iteration_count { 16384 };
#if defined(DEBUG_HOST_EXEC)
	array<float3, pixel_count> img_data;
	for(uint32_t iteration = 0; iteration < iteration_count; ++iteration) {
		reset_counter(); // necessary workaround for now
		for(uint32_t i = 0; i < pixel_count; ++i) {
			path_trace(img_data.data(), iteration, core::rand<uint32_t>(), img_size);
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
			// grab the current image buffer data ...
			auto img_data = (float3*)img_buffer->map(dev_queue);
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
			const auto pitch_offset = ((size_t)wnd_surface->pitch / sizeof(uint32_t)) - (size_t)img_size.x;
			uint32_t* px_ptr = (uint32_t*)wnd_surface->pixels;
			for(uint32_t y = 0, img_idx = 0; y < img_size.y; ++y) {
				for(uint32_t x = 0; x < img_size.x; ++x, ++img_idx) {
					// map and gamma correct each pixel according to the window format
					const auto rgb = gamma_correct(img_data[img_idx]);
					*px_ptr++ = SDL_MapRGB(wnd_surface->format, rgb.r, rgb.g, rgb.b);
				}
				px_ptr += pitch_offset;
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
