#pragma once
#include "vulkan.h"

namespace Rendering {
	class Renderer {
	public:
		Renderer(HINSTANCE hInst, HWND hWindow);
		~Renderer();

		void Render();
		void ResizeSurface();
	private:
		Vulkan vulkan;
	};
}