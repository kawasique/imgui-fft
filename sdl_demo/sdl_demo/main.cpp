#include <string>
#include <cstring>
#include <mutex>

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "kiss_fftr.h"

#define SAMPLES_COUNT 4096

float time_series[SAMPLES_COUNT];
float frequency_series[SAMPLES_COUNT];

float recorded_data[SAMPLES_COUNT];
float buffered_data[SAMPLES_COUNT];
kiss_fft_cpx out[SAMPLES_COUNT / 2 + 1];
float transformed_data[SAMPLES_COUNT];

std::mutex mtx;

size_t callback_id = 0;
void audioRecordingCallback(void* userdata, Uint8* stream, int len)
{
	float* samples = reinterpret_cast<float*>(stream);
	size_t count = len / sizeof(float);

	for (size_t i = 0; i < count; i++)
	{
		recorded_data[i] = samples[i];
	}

	callback_id++;
}

SDL_AudioDeviceID recordingDeviceId = 0;
SDL_AudioSpec desiredRecordingSpec, receivedRecordingSpec;
void initAudioDevice(int deviceId)
{
	if (recordingDeviceId != 0)
		SDL_CloseAudioDevice(recordingDeviceId);

	recordingDeviceId = 0;

	SDL_zero(desiredRecordingSpec);
	desiredRecordingSpec.freq = 44100;
	desiredRecordingSpec.format = AUDIO_F32;
	desiredRecordingSpec.channels = 1;
	desiredRecordingSpec.samples = SAMPLES_COUNT;
	desiredRecordingSpec.callback = audioRecordingCallback;
	//Open recording device
	recordingDeviceId = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(deviceId, SDL_TRUE), SDL_TRUE, &desiredRecordingSpec, &receivedRecordingSpec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);

	// Device failed to open
	if (recordingDeviceId == 0)
	{
		//Report error
		printf("Failed to open recording device! SDL Error: %s", SDL_GetError());
		exit(-1);
	}

	float T0 = (float)receivedRecordingSpec.samples / receivedRecordingSpec.freq;
	float f0 = 1.0f / T0;

	for (size_t i = 0; i < SAMPLES_COUNT; i++)
	{
		time_series[i] = (float)i * T0 / SAMPLES_COUNT;
		frequency_series[i] = (float)i * f0;
	}

	SDL_PauseAudioDevice(recordingDeviceId, SDL_FALSE);
}

void CalculateData(kiss_fftr_cfg cfg, float mult)
{
	kiss_fftr(cfg, buffered_data, out);

	for (size_t i = 0; i < SAMPLES_COUNT / 2 + 1; i++)
	{
		transformed_data[i] = mult * sqrt(out[i].r * out[i].r + out[i].i * out[i].i) / SAMPLES_COUNT;
	}
}

int main(int, char**)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
	{
		printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
		exit(-1);
	}

	// GL 3.0 + GLSL 130
	const char* glsl_version = "#version 130";
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

	// Create window with graphics context
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	SDL_Window* window = SDL_CreateWindow("SDL Demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1); // Enable vsync

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	//ImGui::StyleColorsLight();
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
	ImGui_ImplOpenGL3_Init(glsl_version);

	kiss_fftr_cfg fftr_cfg = kiss_fftr_alloc(SAMPLES_COUNT, false, 0, 0);

	// Our state
	bool show_demo_window = false;
	bool show_plot_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done = false;
	while (!done)
	{
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT)
				done = true;
			if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
				done = true;
		}

		static size_t local_callback_id = callback_id;
		bool new_data = false;
		float T0 = (float)receivedRecordingSpec.samples / receivedRecordingSpec.freq;
		float f0 = 1.0f / T0;

		SDL_LockAudioDevice(recordingDeviceId);
		std::memcpy(buffered_data, recorded_data, SAMPLES_COUNT * sizeof(float));
		if (local_callback_id != callback_id)
		{
			local_callback_id = callback_id;
			new_data = true;
		}
		else
		{
			new_data = false;
		}
		SDL_UnlockAudioDevice(recordingDeviceId);

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		static float fft_mult = 5.0f;
		if (new_data)
			CalculateData(fftr_cfg, fft_mult);

		// Demo window
		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);

		if (show_plot_window)
			ImPlot::ShowDemoWindow();

		// Main window
		{
			ImGui::Begin("Main");

			ImGui::ColorEdit3("clear color", (float*)&clear_color);
			ImGui::SliderFloat("Spectrum scaling", &fft_mult, 0.1f, 25.0f);

			if (ImGui::Button("Show demo"))
				show_demo_window = !show_demo_window;
			ImGui::SameLine();
			if (ImGui::Button("Show plot"))
				show_plot_window = !show_plot_window;

			ImGui::End();
		}

		// Plot window
		if (recordingDeviceId)
		{
			ImGui::Begin("Plot");
			ImVec2 half_space = ImGui::GetContentRegionAvail();
			half_space.y = half_space.y / 2 - 2;
			half_space.x = half_space.x;
			if (ImPlot::BeginPlot("##Data", half_space)) {
				ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
				ImPlot::SetupAxisLimits(ImAxis_X1, 0, T0);
				ImPlot::SetupAxisLimits(ImAxis_Y1, -1, 1);
				ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, T0);
				ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -1, 1);
				ImPlot::PlotLine("Recorded data", time_series, buffered_data, SAMPLES_COUNT);
				ImPlot::EndPlot();
			}
			if (ImPlot::BeginPlot("##Spectrum", half_space)) {
				ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
				ImPlot::SetupAxisLimits(ImAxis_X1, 0, f0 * (SAMPLES_COUNT / 2 + 1));
				ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1);
				ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, f0 * (SAMPLES_COUNT / 2 + 1));
				ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, 1);
				//ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
				ImPlot::PlotLine("Spectrum", frequency_series, transformed_data, SAMPLES_COUNT/2 + 1);
				ImPlot::EndPlot();
			}


			ImGui::End();
		}

		// Device window
		{
			static int dev = 2;

			ImGui::Begin("Device");
			ImGui::Text("Avaiable devices:");

			for (int i = 0; i < SDL_GetNumAudioDevices(SDL_TRUE); i++)
			{
				ImGui::Text("Recording device %d: %s", i, SDL_GetAudioDeviceName(i, SDL_TRUE));
			}

			ImGui::SliderInt("device id", &dev, 0, SDL_GetNumAudioDevices(SDL_TRUE) - 1);
			if (ImGui::Button("start"))
			{
				initAudioDevice(dev);
			}
			ImGui::SameLine();
			if (ImGui::Button("stop"))
			{
				SDL_UnlockAudioDevice(recordingDeviceId);
				if (recordingDeviceId != 0)
					SDL_CloseAudioDevice(recordingDeviceId);
				recordingDeviceId = 0;
			}

			if (recordingDeviceId != 0)
			{
				ImGui::Text(
					"\nCurrent device:\nId: %d\nFreq: %d\nFormat: 0x%X\nSamples: %d\nChannels: %d",
					recordingDeviceId,
					receivedRecordingSpec.freq,
					receivedRecordingSpec.format,
					receivedRecordingSpec.samples,
					receivedRecordingSpec.channels
				);

				ImGui::Text(
					"\nT0 = Samples / Freq = %f s\nf0 = 1 / T0 = %f Hz", T0, f0);
			}

			ImGui::End();
		}
		// Rendering
		ImGui::Render();
		glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
		glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);
	}

	// Cleanup
	kiss_fftr_free(fftr_cfg);
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	SDL_GL_DeleteContext(gl_context);
	SDL_CloseAudioDevice(recordingDeviceId);
	SDL_DestroyWindow(window);

	SDL_Quit();

	return 0;
}