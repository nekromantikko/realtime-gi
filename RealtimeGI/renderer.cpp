#include "renderer.h"
#include <iostream>

namespace Rendering {
	Renderer::Renderer(HINSTANCE hInst, HWND hWindow) {
		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

		// Enable validation layers for debug
		const char* validationLayer = "VK_LAYER_KHRONOS_validation";
		createInfo.enabledLayerCount = 1;
		createInfo.ppEnabledLayerNames = &validationLayer;

		const char* extensionNames[2];
		extensionNames[0] = "VK_KHR_surface";
#ifdef VK_USE_PLATFORM_WIN32_KHR
		extensionNames[1] = "VK_KHR_win32_surface";
#endif
		createInfo.enabledExtensionCount = 2;
		createInfo.ppEnabledExtensionNames = extensionNames;

		vkCreateInstance(&createInfo, nullptr, &vkInstance);

		VkWin32SurfaceCreateInfoKHR surfaceCreateInfo{};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.hwnd = hWindow;
		surfaceCreateInfo.hinstance = hInst;

		vkCreateWin32SurfaceKHR(vkInstance, &surfaceCreateInfo, nullptr, &surface);

		if (!GetSuitablePhysicalDevice()) {
			// TODO: Handle
			std::cout << "Failed to create physical device" << std::endl;
		}
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);
		if (!CreateLogicalDevice()) {
			// TODO
			std::cout << "Failed to create logical device" << std::endl;
		}
		vkGetDeviceQueue(device, primaryQueueFamilyIndex, 0, &primaryQueue);

	}
	Renderer::~Renderer() {
		vkDestroyDevice(device, nullptr);
		vkDestroySurfaceKHR(vkInstance, surface, nullptr);
		vkDestroyInstance(vkInstance, nullptr);
	}

	// Returns true if suitable physical device found, false otherwise
	bool Renderer::GetSuitablePhysicalDevice() {
		u32 physicalDeviceCount;
		vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, nullptr);
		if (physicalDeviceCount == 0) {
			return false;
		}
		std::vector<VkPhysicalDevice> availableDevices(physicalDeviceCount);
		vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, availableDevices.data());

		for (const auto& device : availableDevices) {
			u32 queueFamilyIndex;
			if (IsPhysicalDeviceSuitable(device, queueFamilyIndex)) {
				physicalDevice = device;
				primaryQueueFamilyIndex = queueFamilyIndex;
				return true;
			}
		}

		return false;
	}

	bool Renderer::IsPhysicalDeviceSuitable(VkPhysicalDevice physicalDevice, u32& outQueueFamilyIndex) {
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);

		u32 extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

		bool hasSwapchainSupport = false;
		for (const auto& extension : availableExtensions) {
			std::cout << extension.extensionName << std::endl;
			if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
				hasSwapchainSupport = true;
				break;
			}
		}

		if (!hasSwapchainSupport) {
			return false;
		}

		// TODO: Actually check what formats are available
		u32 surfaceFormatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr);
		u32 presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

		if (surfaceFormatCount == 0 || presentModeCount == 0) {
			return false;
		}

		u32 queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

		for (int i = 0; i < queueFamilyCount; i++) {
			const VkQueueFamilyProperties& queueFamily = queueFamilies.at(i);

			if (queueFamily.queueCount == 0) {
				continue;
			}

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);

			if (!presentSupport) {
				continue;
			}

			// For now, to keep things simple I want to use one queue for everything, so the family needs to support all of these:
			if ((queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))) {
				outQueueFamilyIndex = i;
				return true;
			}
		}

		return false;
	}

	bool Renderer::CreateLogicalDevice() {
		float queuePriority = 1.0f;

		VkDeviceQueueCreateInfo queueCreateInfo;
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.pNext = nullptr;
		queueCreateInfo.flags = 0;
		queueCreateInfo.queueFamilyIndex = primaryQueueFamilyIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceFeatures deviceFeatures{};

		const char* swapchainExtensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pNext = nullptr;
		createInfo.flags = 0;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = 1;
		createInfo.ppEnabledExtensionNames = &swapchainExtensionName;

		VkResult err = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
		if (err != VK_SUCCESS) {
			return false;
		}

		return true;
	}
}