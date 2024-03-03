#include "renderer.h"

namespace Rendering {
	Renderer::Renderer(HINSTANCE hInst, HWND hWindow): vulkan(hInst, hWindow) {
		
	}

	Renderer::~Renderer() {
		
	}

	void Renderer::Render() {
		vulkan.BeginRenderCommands();
		vulkan.DoFinalBlit();
		vulkan.EndRenderCommands();
	}

	void Renderer::ResizeSurface() {
		vulkan.RecreateSwapchain();
	}
}