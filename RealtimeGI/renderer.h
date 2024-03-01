#pragma once
#include <windows.h>
#include <vector>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "typedef.h"

namespace Rendering {
	class Renderer {
	public:
		Renderer(HINSTANCE hInst, HWND hWindow);
		~Renderer();

	private:
		bool GetSuitablePhysicalDevice();
		bool IsPhysicalDeviceSuitable(VkPhysicalDevice physicalDevice, u32& outQueueFamilyIndex);
		bool CreateLogicalDevice();

		VkInstance vkInstance;

		VkSurfaceKHR surface;
		VkSurfaceCapabilitiesKHR surfaceCapabilities;

		VkPhysicalDevice physicalDevice;
		struct PhysicalDeviceInfo
		{
			VkPhysicalDeviceProperties properties;
			VkPhysicalDeviceMemoryProperties memProperties;

			std::vector<VkQueueFamilyProperties> queueFamilies;
		} physicalDeviceInfo;

		VkDevice device;
		u32 primaryQueueFamilyIndex = 0;
		VkQueue primaryQueue;

	};
}