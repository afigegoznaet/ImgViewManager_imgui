#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>

#include "FolderBrowser.hpp"
#include "ShutdownDeadline.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include <fontconfig/fontconfig.h>
#include "imgui_freetype.h"

namespace {
	void applyQtStyle() {
		ImGui::StyleColorsLight();
		auto &style = ImGui::GetStyle();
		style.WindowRounding = 0;
		style.ChildRounding = 0;
		style.FrameRounding = 3;
		style.PopupRounding = 3;
		style.ScrollbarRounding = 3;
		style.GrabRounding = 2;
		style.FrameBorderSize = 1;
		style.ChildBorderSize = 1;
		style.PopupBorderSize = 1;
		style.WindowPadding = ImVec2(8, 8);
		style.FramePadding = ImVec2(6, 3);
		style.ItemSpacing = ImVec2(8, 5);
		style.IndentSpacing = 20;
		style.ScrollbarSize = 14;
		const auto rgb = [](int r, int g, int b) {
			return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1);
		};
		auto	  *colors = style.Colors;
		const auto background = rgb(239, 240, 241);
		const auto border = rgb(171, 171, 171);
		const auto hover = rgb(229, 243, 255);	  // Qt tree hover: #e5f3ff.
		const auto selected = rgb(204, 232, 255); // Qt tree selection: #cce8ff.
		const auto accent = rgb(48, 140, 198);
		colors[ImGuiCol_Text] = rgb(35, 38, 41);
		colors[ImGuiCol_TextDisabled] = rgb(115, 115, 115);
		colors[ImGuiCol_WindowBg] = background;
		colors[ImGuiCol_ChildBg] = rgb(255, 255, 255);
		colors[ImGuiCol_PopupBg] = rgb(250, 250, 250);
		colors[ImGuiCol_Border] = border;
		colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
		colors[ImGuiCol_FrameBg] = rgb(255, 255, 255);
		colors[ImGuiCol_FrameBgHovered] = hover;
		colors[ImGuiCol_FrameBgActive] = selected;
		colors[ImGuiCol_TitleBg] = background;
		colors[ImGuiCol_TitleBgActive] = background;
		colors[ImGuiCol_MenuBarBg] = rgb(222, 224, 226);
		colors[ImGuiCol_Button] = rgb(235, 235, 235);
		colors[ImGuiCol_ButtonHovered] = hover;
		colors[ImGuiCol_ButtonActive] = selected;
		colors[ImGuiCol_Header] = selected;
		colors[ImGuiCol_HeaderHovered] = hover;
		colors[ImGuiCol_HeaderActive] = selected;
		colors[ImGuiCol_CheckMark] = accent;
		colors[ImGuiCol_SliderGrab] = accent;
		colors[ImGuiCol_SliderGrabActive] = rgb(35, 115, 170);
		colors[ImGuiCol_ScrollbarBg] = background;
		colors[ImGuiCol_ScrollbarGrab] = rgb(190, 190, 190);
		colors[ImGuiCol_ScrollbarGrabHovered] = rgb(165, 165, 165);
		colors[ImGuiCol_ScrollbarGrabActive] = rgb(140, 140, 140);
		colors[ImGuiCol_Separator] = border;
		colors[ImGuiCol_SeparatorHovered] = accent;
		colors[ImGuiCol_SeparatorActive] = accent;
		colors[ImGuiCol_ResizeGrip] = border;
		colors[ImGuiCol_ResizeGripHovered] = accent;
		colors[ImGuiCol_ResizeGripActive] = accent;
		colors[ImGuiCol_PlotHistogram] = accent;
		colors[ImGuiCol_TextSelectedBg] = selected;
	}

	struct Runtime {
		SDL_Window	 *window = nullptr;
		SDL_GLContext gl = nullptr;
		ImGuiContext *imgui = nullptr;
		std::string	  iniFile;
		bool		  platformReady = false;
		bool		  rendererReady = false;

		~Runtime() {
			if (rendererReady)
				ImGui_ImplOpenGL3_Shutdown();
			if (platformReady)
				ImGui_ImplSDL3_Shutdown();
			if (imgui)
				ImGui::DestroyContext(imgui);
			if (gl)
				SDL_GL_DestroyContext(gl);
			if (window)
				SDL_DestroyWindow(window);
			SDL_Quit();
		}
	};
}

int main(int argc, char **argv) {
	ShutdownDeadline shutdownDeadline;
	Runtime			 runtime;
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("SDL initialization failed: %s", SDL_GetError());
		return 1;
	}

	std::filesystem::path statePath;
	if (char *pref = SDL_GetPrefPath("ImgViewManager", "imgui_ivm")) {
		statePath = std::filesystem::u8path(pref) / "state.conf";
		runtime.iniFile =
			(std::filesystem::u8path(pref) / "imgui.ini").string();
		SDL_free(pref);
	}
	AppState state = loadState(statePath);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
						SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
						SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	const char *shaderVersion = "#version 150";
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	const char *shaderVersion = "#version 330";
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	runtime.window = SDL_CreateWindow("ImgViewManager (ImGui)", 420, 180,
									  SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS
										  | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!runtime.window) {
		SDL_Log("Window creation failed: %s", SDL_GetError());
		return 1;
	}
	SDL_SetWindowPosition(runtime.window, SDL_WINDOWPOS_CENTERED,
						  SDL_WINDOWPOS_CENTERED);
	runtime.gl = SDL_GL_CreateContext(runtime.window);
	if (!runtime.gl || !SDL_GL_MakeCurrent(runtime.window, runtime.gl)) {
		SDL_Log("OpenGL initialization failed: %s", SDL_GetError());
		return 1;
	}
	const bool vsync = SDL_GL_SetSwapInterval(1);

	IMGUI_CHECKVERSION();
	runtime.imgui = ImGui::CreateContext();
	ImGui::GetIO().IniFilename =
		runtime.iniFile.empty() ? nullptr : runtime.iniFile.c_str();
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	applyQtStyle();
	ImGui::GetIO().Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
	ImGui::GetIO().Fonts->FontLoaderFlags =
		ImGuiFreeTypeLoaderFlags_LightHinting;
	// ImGui uses full ascender/descender height; 18px gives Noto Sans
	// roughly the visible size of the desktop's 10pt UI font.
	ImGui::GetStyle().FontSizeBase = 18.0f;
	// Resolve the desktop sans-serif font instead of using ImGui's bitmap
	// default.
	FcPattern *pattern =
		FcNameParse(reinterpret_cast<const FcChar8 *>("sans-serif"));
	if (pattern) {
		FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
		FcDefaultSubstitute(pattern);
		FcResult   result;
		FcPattern *matched = FcFontMatch(nullptr, pattern, &result);
		FcChar8	  *file = nullptr;
		if (matched
			&& FcPatternGetString(matched, FC_FILE, 0, &file) == FcResultMatch)
			ImGui::GetIO().FontDefault =
				ImGui::GetIO().Fonts->AddFontFromFileTTF(
					reinterpret_cast<const char *>(file), 18.0f);
		if (matched)
			FcPatternDestroy(matched);
		FcPatternDestroy(pattern);
	}
	runtime.platformReady =
		ImGui_ImplSDL3_InitForOpenGL(runtime.window, runtime.gl);
	if (!runtime.platformReady)
		return 1;
	runtime.rendererReady = ImGui_ImplOpenGL3_Init(shaderVersion);
	if (!runtime.rendererReady)
		return 1;

	std::error_code pathError;
	auto			start = argc > 1 ? std::filesystem::absolute(
										   std::filesystem::u8path(argv[1]), pathError)
									 : state.folder;
	if (argc <= 1) {
		while (!start.empty()
			   && !std::filesystem::is_directory(start, pathError)) {
			const auto parent = start.parent_path();
			if (parent == start) {
				start.clear();
				break;
			}
			start = parent;
		}
		if (start.empty())
			start = std::filesystem::current_path(pathError);
	}
	if (pathError || !std::filesystem::is_directory(start, pathError)) {
		SDL_Log("Invalid starting folder");
		return 1;
	}
	if (state.folder != start) {
		state.selectedImage.clear();
		state.gridScroll = 0;
	}
	FolderBrowser  browser(start.lexically_normal(), state);
	auto		   lastSaved = serializeState(state);
	double		   lastSaveTime = 0;
	bool		   mainWindowVisible = false;
	bool		   windowStateRestored = false;
	const AppState startupState = state;
	const auto	   capture = [&] {
		browser.saveInto(state);
		if (!mainWindowVisible || !windowStateRestored)
			return;
		const auto flags = SDL_GetWindowFlags(runtime.window);
		state.fullscreen = flags & SDL_WINDOW_FULLSCREEN;
		if (!state.fullscreen)
			state.maximized = flags & SDL_WINDOW_MAXIMIZED;
		if (!(flags
			  & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED
				 | SDL_WINDOW_FULLSCREEN))) {
			SDL_GetWindowSize(runtime.window, &state.windowWidth,
							  &state.windowHeight);
			state.windowPosition = SDL_GetWindowPosition(
				runtime.window, &state.windowX, &state.windowY);
		}
	};
	std::string lastWindowTitle;
	bool		running = true;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			if ((event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_X
				 && (event.key.mod & SDL_KMOD_ALT))
				|| event.type == SDL_EVENT_QUIT
				|| (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
					&& event.window.windowID
						   == SDL_GetWindowID(runtime.window))) {
				running = false;
			}
		}
		if (!running)
			break;
		if (SDL_GetWindowFlags(runtime.window) & SDL_WINDOW_MINIMIZED) {
			SDL_Delay(10);
			continue;
		}

		if (!mainWindowVisible && !browser.startupLoading()) {
			// Create the main window with its saved state instead of converting
			// the splash.
			SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
									| SDL_WINDOW_HIGH_PIXEL_DENSITY
									| SDL_WINDOW_HIDDEN;
			if (startupState.fullscreen)
				flags |= SDL_WINDOW_FULLSCREEN;
			else if (startupState.maximized)
				flags |= SDL_WINDOW_MAXIMIZED;
			SDL_Window *mainWindow = SDL_CreateWindow(
				"ImgViewManager (ImGui)", startupState.windowWidth,
				startupState.windowHeight, flags);
			if (!mainWindow) {
				SDL_Log("Main window creation failed: %s", SDL_GetError());
				return 1;
			}
			if (startupState.windowPosition && !startupState.fullscreen)
				SDL_SetWindowPosition(mainWindow, startupState.windowX,
									  startupState.windowY);
			ImGui_ImplOpenGL3_Shutdown();
			runtime.rendererReady = false;
			ImGui_ImplSDL3_Shutdown();
			runtime.platformReady = false;
			SDL_GL_DestroyContext(runtime.gl);
			runtime.gl = nullptr;
			SDL_DestroyWindow(runtime.window);
			runtime.window = mainWindow;
			runtime.gl = SDL_GL_CreateContext(mainWindow);
			if (!runtime.gl || !SDL_GL_MakeCurrent(mainWindow, runtime.gl)) {
				SDL_Log("Main OpenGL context failed: %s", SDL_GetError());
				return 1;
			}
			SDL_GL_SetSwapInterval(vsync ? 1 : 0);
			runtime.platformReady =
				ImGui_ImplSDL3_InitForOpenGL(mainWindow, runtime.gl);
			runtime.rendererReady = ImGui_ImplOpenGL3_Init(shaderVersion);
			if (!runtime.platformReady || !runtime.rendererReady)
				return 1;
			SDL_ShowWindow(mainWindow);
			windowStateRestored = SDL_SyncWindow(mainWindow);
			if (!windowStateRestored)
				SDL_Log("Window state restoration failed: %s", SDL_GetError());
			mainWindowVisible = true;
		}
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		const ImGuiViewport *viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::Begin("Main", nullptr,
					 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
						 | ImGuiWindowFlags_NoSavedSettings
						 | (mainWindowVisible ? ImGuiWindowFlags_MenuBar : 0));
		browser.draw();
		ImGui::End();
		if (mainWindowVisible) {
			const auto &folder = browser.selectedFolder();
			const auto	name = folder.filename().empty()
								   ? folder.u8string()
								   : folder.filename().u8string();
			const auto	title =
				name + " — ImgViewManager (" + folder.u8string() + ")";
			if (title != lastWindowTitle
				&& SDL_SetWindowTitle(runtime.window, title.c_str()))
				lastWindowTitle = title;
		}

		ImGui::Render();
		int width = 0;
		int height = 0;
		SDL_GetWindowSizeInPixels(runtime.window, &width, &height);
		glViewport(0, 0, width, height);
		const auto background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
		glClearColor(background.x, background.y, background.z, background.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(runtime.window);
		if (ImGui::GetTime() - lastSaveTime >= 1.0) {
			capture();
			const auto serialized = serializeState(state);
			if (serialized != lastSaved && !statePath.empty()) {
				if (saveState(statePath, state))
					lastSaved = serialized;
				else
					SDL_Log("Could not save session state");
			}
			lastSaveTime = ImGui::GetTime();
		}
		if (!vsync)
			SDL_Delay(16);
	}
	shutdownDeadline.arm();
	capture();
	if (!statePath.empty() && !saveState(statePath, state))
		SDL_Log("Could not save session state");
	return 0;
}
