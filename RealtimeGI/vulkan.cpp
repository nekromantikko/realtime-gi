#include "vulkan.h"
#include "system.h"
#include "math.h"

namespace Rendering {
	Vulkan::Vulkan(HINSTANCE hInst, HWND hWindow) {
		DEBUG_LOG("Initializing vulkan...");

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Test";
		appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
		appInfo.pEngineName = "Nekro Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_2;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

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

		GetSuitablePhysicalDevice();
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);
		CreateLogicalDevice();
		vkGetDeviceQueue(device, primaryQueueFamilyIndex, 0, &primaryQueue);

		CreateRenderPasses();
		CreateSwapchain(finalBlitRenderPass);

		VkDescriptorPoolSize poolSizes[3]{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 100;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = 100;
		poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSizes[2].descriptorCount = 100;

		VkDescriptorPoolCreateInfo descriptorPoolInfo{};
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.poolSizeCount = 3;
		descriptorPoolInfo.pPoolSizes = poolSizes;
		descriptorPoolInfo.maxSets = 100;

		vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool);

		CreatePrimaryCommandPoolAndBuffers();

		CreateFramebufferAttachments();
		CreatePrimaryFramebuffer();

		CreateBlitPipeline();
	}
	Vulkan::~Vulkan() {
		// Wait for all commands to execute first
		WaitForAllCommands();

		FreeBlitPipeline();

		FreePrimaryFramebuffer();
		FreeFramebufferAttachments();

		FreePrimaryCommandPoolAndBuffers();

		vkDestroyDescriptorPool(device, descriptorPool, nullptr);

		FreeRenderPasses();
		FreeSwapchain();
		vkDestroyDevice(device, nullptr);
		vkDestroySurfaceKHR(vkInstance, surface, nullptr);
		vkDestroyInstance(vkInstance, nullptr);
	}

	void Vulkan::RecreateSwapchain() {
		// Wait for all commands to execute first
		WaitForAllCommands();

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

		FreePrimaryFramebuffer();
		FreeFramebufferAttachments();
		FreeSwapchain();
		CreateSwapchain(finalBlitRenderPass);
		CreateFramebufferAttachments();
		CreatePrimaryFramebuffer();
	}

	void Vulkan::WaitForAllCommands() {
		VkFence fences[COMMAND_BUFFER_COUNT];
		for (int i = 0; i < COMMAND_BUFFER_COUNT; i++) {
			fences[i] = primaryCommandBuffers[i].cmdFence;
		}

		vkWaitForFences(device, COMMAND_BUFFER_COUNT, fences, VK_TRUE, UINT64_MAX);
	}

	// Returns true if suitable physical device found, false otherwise
	void Vulkan::GetSuitablePhysicalDevice() {
		u32 physicalDeviceCount;
		vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, nullptr);
		if (physicalDeviceCount == 0) {
			DEBUG_ERROR("No physical devices found!");
		}
		std::vector<VkPhysicalDevice> availableDevices(physicalDeviceCount);
		vkEnumeratePhysicalDevices(vkInstance, &physicalDeviceCount, availableDevices.data());

		for (const auto& device : availableDevices) {
			u32 queueFamilyIndex;
			if (IsPhysicalDeviceSuitable(device, queueFamilyIndex)) {
				physicalDevice = device;
				primaryQueueFamilyIndex = queueFamilyIndex;
				return;
			}
		}

		DEBUG_ERROR("No suitable physical device found!");
	}

	bool Vulkan::IsPhysicalDeviceSuitable(VkPhysicalDevice physicalDevice, u32& outQueueFamilyIndex) {
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

	void Vulkan::CreateLogicalDevice() {
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
			DEBUG_ERROR("Failed to create logical device!");
		}
	}

	void Vulkan::CreateForwardRenderPass() {
		VkRenderPassCreateInfo2 createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
		createInfo.pNext = nullptr;
		createInfo.attachmentCount = 4;

		VkAttachmentDescription2 attachmentDescription{};
		attachmentDescription.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
		attachmentDescription.pNext = nullptr;
		attachmentDescription.flags = 0;
		attachmentDescription.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		attachmentDescription.samples = VK_SAMPLE_COUNT_8_BIT;
		attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; //temporary: clear before drawing. Change later to VK_ATTACHMENT_LOAD_OP_LOAD
		attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		//attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		//attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; //this is also temporary for testing purposes
		attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription2 depthDescription{};
		depthDescription.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
		depthDescription.pNext = nullptr;
		depthDescription.flags = 0;
		depthDescription.format = VK_FORMAT_D32_SFLOAT;
		depthDescription.samples = VK_SAMPLE_COUNT_8_BIT;
		depthDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthDescription.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription2 colorResolveDescription{};
		colorResolveDescription.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
		colorResolveDescription.pNext = nullptr;
		colorResolveDescription.flags = 0;
		colorResolveDescription.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		colorResolveDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		colorResolveDescription.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorResolveDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorResolveDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorResolveDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorResolveDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorResolveDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentDescription2 depthResolveDescription{};
		depthResolveDescription.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
		depthResolveDescription.pNext = nullptr;
		depthResolveDescription.flags = 0;
		depthResolveDescription.format = VK_FORMAT_D32_SFLOAT;
		depthResolveDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		depthResolveDescription.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthResolveDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthResolveDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthResolveDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthResolveDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthResolveDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentDescription2 attachments[4] = { attachmentDescription, depthDescription, colorResolveDescription, depthResolveDescription };

		createInfo.pAttachments = attachments;
		createInfo.subpassCount = 1;

		VkAttachmentReference2 colorAttachmentReference{};
		colorAttachmentReference.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
		colorAttachmentReference.pNext = nullptr;
		colorAttachmentReference.attachment = 0;
		colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachmentReference.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		VkAttachmentReference2 depthAttachmentReference{};
		depthAttachmentReference.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
		depthAttachmentReference.pNext = nullptr;
		depthAttachmentReference.attachment = 1;
		depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachmentReference.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		VkAttachmentReference2 colorResolveDescriptionRef{};
		colorResolveDescriptionRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
		colorResolveDescriptionRef.pNext = nullptr;
		colorResolveDescriptionRef.attachment = 2;
		colorResolveDescriptionRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorResolveDescriptionRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		VkAttachmentReference2 depthResolveDescriptionRef{};
		depthResolveDescriptionRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
		depthResolveDescriptionRef.pNext = nullptr;
		depthResolveDescriptionRef.attachment = 3;
		depthResolveDescriptionRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthResolveDescriptionRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		VkSubpassDescriptionDepthStencilResolve depthResolve{};
		depthResolve.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE;
		depthResolve.pNext = nullptr;
		depthResolve.depthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
		depthResolve.stencilResolveMode = VK_RESOLVE_MODE_NONE;
		depthResolve.pDepthStencilResolveAttachment = &depthResolveDescriptionRef;

		VkSubpassDescription2 subpassDescription{};
		subpassDescription.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
		subpassDescription.pNext = &depthResolve;
		subpassDescription.flags = 0;
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.viewMask = 0;
		subpassDescription.inputAttachmentCount = 0;
		subpassDescription.pInputAttachments = nullptr;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentReference;
		subpassDescription.pResolveAttachments = &colorResolveDescriptionRef;
		subpassDescription.pDepthStencilAttachment = &depthAttachmentReference;
		subpassDescription.preserveAttachmentCount = 0;
		subpassDescription.pPreserveAttachments = nullptr;

		createInfo.pSubpasses = &subpassDescription;
		createInfo.dependencyCount = 0;
		createInfo.pDependencies = nullptr;
		createInfo.correlatedViewMaskCount = 0;
		createInfo.pCorrelatedViewMasks = nullptr;

		VkResult err = vkCreateRenderPass2(device, &createInfo, nullptr, &forwardRenderPass);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("failed to create render pass!");
		}
	}

	void Vulkan::CreateFinalBlitRenderPass() {
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = VK_FORMAT_B8G8R8A8_SRGB;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo renderImagePassInfo{};
		renderImagePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderImagePassInfo.attachmentCount = 1;
		renderImagePassInfo.pAttachments = &colorAttachment;
		renderImagePassInfo.subpassCount = 1;
		renderImagePassInfo.pSubpasses = &subpass;
		renderImagePassInfo.dependencyCount = 1;
		renderImagePassInfo.pDependencies = &dependency;

		VkResult err = vkCreateRenderPass(device, &renderImagePassInfo, nullptr, &finalBlitRenderPass);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("failed to create render pass!");
		}
	}

	void Vulkan::CreateRenderPasses() {
		CreateForwardRenderPass();
		CreateFinalBlitRenderPass();
	}

	void Vulkan::FreeRenderPasses()
	{
		vkDestroyRenderPass(device, finalBlitRenderPass, nullptr);
		vkDestroyRenderPass(device, forwardRenderPass, nullptr);
	}

	void Vulkan::CreateSwapchain(VkRenderPass renderPass) {
		if (SWAPCHAIN_MIN_IMAGE_COUNT > surfaceCapabilities.maxImageCount) {
			DEBUG_ERROR("Image count not supported (%d or bigger than %d, the maximum image count)!", SWAPCHAIN_MIN_IMAGE_COUNT, surfaceCapabilities.maxImageCount);
		}

		if (renderPass == VK_NULL_HANDLE) {
			DEBUG_ERROR("Invalid render pass!");
		}

		VkResult err;

		VkSwapchainCreateInfoKHR swapchainCreateInfo{};
		swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapchainCreateInfo.surface = surface;
		swapchainCreateInfo.minImageCount = SWAPCHAIN_MIN_IMAGE_COUNT;
		swapchainCreateInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
		swapchainCreateInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		swapchainCreateInfo.imageExtent = surfaceCapabilities.currentExtent;
		swapchainCreateInfo.imageArrayLayers = 1;
		swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // As long as we're using a single queue
		swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
		swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapchainCreateInfo.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		swapchainCreateInfo.clipped = VK_TRUE;
		swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

		err = vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("Failed to create swapchain!");
		}

		u32 imageCount = 0;
		vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
		if (imageCount < SWAPCHAIN_MIN_IMAGE_COUNT) {
			DEBUG_ERROR("Swapchain image count is less than required");
		}
		std::vector<VkImage> images(imageCount);
		vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());

		swapchainImages = std::vector<SwapchainImage>(imageCount);
		// Create image views and framebuffers
		for (u32 i = 0; i < imageCount; i++) {
			SwapchainImage& swap = swapchainImages[i];

			swap.image = images[i];

			VkImageViewCreateInfo imageViewCreateInfo{};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.image = swap.image;
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCreateInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
			imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imageViewCreateInfo.subresourceRange.levelCount = 1;
			imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
			imageViewCreateInfo.subresourceRange.layerCount = 1;

			err = vkCreateImageView(device, &imageViewCreateInfo, nullptr, &swap.view);
			if (err != VK_SUCCESS) {
				DEBUG_ERROR("Failed to create image view!");
			}

			VkImageView attachments[] = {
				swap.view
			};

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = surfaceCapabilities.currentExtent.width;
			framebufferInfo.height = surfaceCapabilities.currentExtent.height;
			framebufferInfo.layers = 1;

			err = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swap.framebuffer);
			if (err != VK_SUCCESS) {
				DEBUG_ERROR("Failed to create framebuffer!");
			}
		}
	}

	void Vulkan::FreeSwapchain() {
		for (auto& swap : swapchainImages) {
			vkDestroyFramebuffer(device, swap.framebuffer, nullptr);
			vkDestroyImageView(device, swap.view, nullptr);
		}

		vkDestroySwapchainKHR(device, swapchain, nullptr);
	}

	void Vulkan::CreatePrimaryCommandPoolAndBuffers() {
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = primaryQueueFamilyIndex;

		if (vkCreateCommandPool(device, &poolInfo, nullptr, &primaryCommandPool) != VK_SUCCESS) {
			DEBUG_ERROR("failed to create command pool!");
		}

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = primaryCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		currentCbIndex = 0;

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (u32 i = 0; i < COMMAND_BUFFER_COUNT; i++) {
			CommandBuffer& cmd = primaryCommandBuffers[i];

			if (vkAllocateCommandBuffers(device, &allocInfo, &cmd.cmdBuffer) != VK_SUCCESS) {
				DEBUG_ERROR("failed to allocate command buffers!");
			}

			vkCreateSemaphore(device, &semaphoreInfo, nullptr, &cmd.imageAcquiredSemaphore);
			vkCreateSemaphore(device, &semaphoreInfo, nullptr, &cmd.drawCompleteSemaphore);
			vkCreateFence(device, &fenceInfo, nullptr, &cmd.cmdFence);
		}
	}
	void Vulkan::FreePrimaryCommandPoolAndBuffers() {
		for (u32 i = 0; i < COMMAND_BUFFER_COUNT; i++) {
			CommandBuffer& cmd = primaryCommandBuffers[i];

			vkDestroySemaphore(device, cmd.imageAcquiredSemaphore, nullptr);
			vkDestroySemaphore(device, cmd.drawCompleteSemaphore, nullptr);
			vkDestroyFence(device, cmd.cmdFence, nullptr);
			vkFreeCommandBuffers(device, primaryCommandPool, 1, &cmd.cmdBuffer);
		}
		vkDestroyCommandPool(device, primaryCommandPool, nullptr);
	}

	void Vulkan::CreateBlitPipeline()
	{
		u32 vertShaderLength;
		char* vertShader = AllocFileBytes("shaders/blit_vert.spv", vertShaderLength);
		u32 fragShaderLength;
		char* fragShader = AllocFileBytes("shaders/blit_frag.spv", fragShaderLength);

		blitVert = CreateShaderModule(vertShader, vertShaderLength);
		blitFrag = CreateShaderModule(fragShader, fragShaderLength);
		free(vertShader);
		free(fragShader);

		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = blitVert;
		vertShaderStageInfo.pName = "main";
		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = blitFrag;
		fragShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo stages[] = { vertShaderStageInfo, fragShaderStageInfo };

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = nullptr; // Will be set at render time
		viewportState.scissorCount = 1;
		viewportState.pScissors = nullptr; // Will be set at render time

		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		VkPipelineLayoutCreateInfo blitPipelineLayoutInfo{};
		blitPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

		if (vkCreatePipelineLayout(device, &blitPipelineLayoutInfo, nullptr, &blitPipelineLayout) != VK_SUCCESS) {
			DEBUG_ERROR("failed to create pipeline layout!");
		}

		// Dynamic viewport and scissor, as the window size might change
		// (Although it shouldn't change very often)
		VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
		dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateInfo.dynamicStateCount = 2;
		dynamicStateInfo.pDynamicStates = dynamicStates;

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = nullptr; // Optional
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicStateInfo;
		pipelineInfo.layout = blitPipelineLayout;
		pipelineInfo.renderPass = finalBlitRenderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
		pipelineInfo.basePipelineIndex = -1; // Optional

		VkResult err = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &blitPipeline);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("failed to create graphics pipelines!");
		}
	}

	void Vulkan::FreeBlitPipeline()
	{
		vkDestroyPipeline(device, blitPipeline, nullptr);
		vkDestroyPipelineLayout(device, blitPipelineLayout, nullptr);
		vkDestroyShaderModule(device, blitVert, nullptr);
		vkDestroyShaderModule(device, blitFrag, nullptr);
	}

	void Vulkan::CreateFramebufferAttachments() {
		// Color attachment (multisampled)
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.flags = 0;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { surfaceCapabilities.currentExtent.width, surfaceCapabilities.currentExtent.height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_8_BIT;

		vkCreateImage(device, &imageInfo, nullptr, &colorAttachment.image);

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, colorAttachment.image, &memRequirements);
		AllocateMemory(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorAttachment.memory);
		vkBindImageMemory(device, colorAttachment.image, colorAttachment.memory, 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = colorAttachment.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		vkCreateImageView(device, &viewInfo, nullptr, &colorAttachment.view);

		// Color attachment resolve (not multisampled)
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		vkCreateImage(device, &imageInfo, nullptr, &colorAttachmentResolve.image);
		vkGetImageMemoryRequirements(device, colorAttachmentResolve.image, &memRequirements);
		AllocateMemory(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorAttachmentResolve.memory);
		vkBindImageMemory(device, colorAttachmentResolve.image, colorAttachmentResolve.memory, 0);
		viewInfo.image = colorAttachmentResolve.image;
		vkCreateImageView(device, &viewInfo, nullptr, &colorAttachmentResolve.view);

		// Depth attachment (multisampled)
		imageInfo.format = VK_FORMAT_D32_SFLOAT;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_8_BIT;
		vkCreateImage(device, &imageInfo, nullptr, &depthAttachment.image);
		vkGetImageMemoryRequirements(device, depthAttachment.image, &memRequirements);
		AllocateMemory(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthAttachment.memory);
		vkBindImageMemory(device, depthAttachment.image, depthAttachment.memory, 0);
		viewInfo.format = VK_FORMAT_D32_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.image = depthAttachment.image;
		vkCreateImageView(device, &viewInfo, nullptr, &depthAttachment.view);

		// Depth attachment resolve
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		vkCreateImage(device, &imageInfo, nullptr, &depthAttachmentResolve.image);
		vkGetImageMemoryRequirements(device, depthAttachmentResolve.image, &memRequirements);
		AllocateMemory(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthAttachmentResolve.memory);
		vkBindImageMemory(device, depthAttachmentResolve.image, depthAttachmentResolve.memory, 0);
		viewInfo.image = depthAttachmentResolve.image;
		vkCreateImageView(device, &viewInfo, nullptr, &depthAttachmentResolve.view);
	}
	void Vulkan::FreeFramebufferAttachments() {
		vkDestroyImageView(device, colorAttachment.view, nullptr);
		vkDestroyImage(device, colorAttachment.image, nullptr);
		vkFreeMemory(device, colorAttachment.memory, nullptr);

		vkDestroyImageView(device, colorAttachmentResolve.view, nullptr);
		vkDestroyImage(device, colorAttachmentResolve.image, nullptr);
		vkFreeMemory(device, colorAttachmentResolve.memory, nullptr);

		vkDestroyImageView(device, depthAttachment.view, nullptr);
		vkDestroyImage(device, depthAttachment.image, nullptr);
		vkFreeMemory(device, depthAttachment.memory, nullptr);

		vkDestroyImageView(device, depthAttachmentResolve.view, nullptr);
		vkDestroyImage(device, depthAttachmentResolve.image, nullptr);
		vkFreeMemory(device, depthAttachmentResolve.memory, nullptr);
	}
	void Vulkan::CreatePrimaryFramebuffer() {
		VkImageView attachments[4] = { colorAttachment.view, depthAttachment.view, colorAttachmentResolve.view, depthAttachmentResolve.view };

		VkFramebufferCreateInfo framebufferInfo;
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.pNext = nullptr;
		framebufferInfo.flags = 0;
		framebufferInfo.renderPass = forwardRenderPass;
		framebufferInfo.attachmentCount = 4;
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = surfaceCapabilities.currentExtent.width;
		framebufferInfo.height = surfaceCapabilities.currentExtent.height;
		framebufferInfo.layers = 1;

		vkCreateFramebuffer(device, &framebufferInfo, nullptr, &primaryFramebuffer);
	}
	void Vulkan::FreePrimaryFramebuffer() {
		vkDestroyFramebuffer(device, primaryFramebuffer, nullptr);
	}

	u32 Vulkan::GetDeviceMemoryTypeIndex(u32 typeFilter, VkMemoryPropertyFlags propertyFlags) {
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

		for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
			if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags) {
				return i;
			}
		}
	}

	VkCommandBuffer Vulkan::GetTemporaryCommandBuffer() {
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = primaryCommandPool;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

		return commandBuffer;
	}

	void Vulkan::AllocateMemory(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties, VkDeviceMemory& outMemory) {
		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = requirements.size;
		memAllocInfo.memoryTypeIndex = GetDeviceMemoryTypeIndex(requirements.memoryTypeBits, properties);

		VkResult err = vkAllocateMemory(device, &memAllocInfo, nullptr, &outMemory);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("Failed to allocate memory!\n");
		}
	}

	void Vulkan::AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, Buffer& outBuffer) {
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.pNext = nullptr;
		bufferInfo.flags = 0;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult err = vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer.buffer);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("Failed to create buffer!\n");
		}

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, outBuffer.buffer, &memRequirements);
		DEBUG_LOG("Buffer memory required: %d\n", memRequirements.size);

		AllocateMemory(memRequirements, memProps, outBuffer.memory);

		vkBindBufferMemory(device, outBuffer.buffer, outBuffer.memory, 0);
	}

	void Vulkan::CopyBuffer(const VkBuffer& src, const VkBuffer& dst, VkDeviceSize size) {
		VkCommandBuffer temp = GetTemporaryCommandBuffer();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(temp, &beginInfo);

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0; // Optional
		copyRegion.dstOffset = 0; // Optional
		copyRegion.size = size;
		vkCmdCopyBuffer(temp, src, dst, 1, &copyRegion);

		vkEndCommandBuffer(temp);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &temp;

		vkQueueSubmit(primaryQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(primaryQueue);

		vkFreeCommandBuffers(device, primaryCommandPool, 1, &temp);
	}

	void Vulkan::FreeBuffer(Buffer& buffer) {
		vkDestroyBuffer(device, buffer.buffer, nullptr);
		vkFreeMemory(device, buffer.memory, nullptr);
	}

	VkShaderModule Vulkan::CreateShaderModule(const char* code, const u32 size) {
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = size;
		createInfo.pCode = (const u32*)code;

		VkShaderModule module;
		VkResult err = vkCreateShaderModule(device, &createInfo, nullptr, &module);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("Failed to create shader module!");
		}
		return module;
	}

	TextureHandle Vulkan::CreateTexture(void* pixels, u32 width, u32 height, TextureType type, ColorSpace space, TextureFilter filter, bool generateMips) {
		VkFormat format = VK_FORMAT_UNDEFINED;
		u32 layerCount = type == TEXTURE_CUBEMAP ? 6 : 1;

		switch (space) {
		case COLORSPACE_SRGB:
			format = VK_FORMAT_R8G8B8A8_SRGB;
			break;
		case COLORSPACE_LINEAR:
			format = VK_FORMAT_R8G8B8A8_UNORM;
			break;
		default:
			break;
		}

		u32 mipCount = 1;
		if (generateMips) {
			mipCount += std::floor(std::log2(MAX(width, height)));
		}

		TextureImpl texture{};

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.flags = type == TEXTURE_CUBEMAP ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = mipCount;
		imageInfo.arrayLayers = layerCount;
		imageInfo.format = format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		vkCreateImage(device, &imageInfo, nullptr, &texture.image);

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, texture.image, &memRequirements);

		AllocateMemory(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.memory);
		vkBindImageMemory(device, texture.image, texture.memory, 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = texture.image;
		viewInfo.viewType = type == TEXTURE_CUBEMAP ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipCount;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = layerCount;

		vkCreateImageView(device, &viewInfo, nullptr, &texture.view);

		VkSamplerCreateInfo samplerInfo;
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.pNext = nullptr;
		samplerInfo.flags = 0;
		samplerInfo.magFilter = (VkFilter)filter;
		samplerInfo.minFilter = (VkFilter)filter;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 0;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = mipCount;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler);

		// Copy data
		Buffer stagingBuffer;
		VkDeviceSize imageBytes = width * height * 4;
		if (type == TEXTURE_CUBEMAP) {
			imageBytes *= 6;
		}
		AllocateBuffer(imageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer);

		void* data;
		vkMapMemory(device, stagingBuffer.memory, 0, imageBytes, 0, &data);
		memcpy(data, pixels, imageBytes);
		vkUnmapMemory(device, stagingBuffer.memory);

		VkCommandBuffer temp = GetTemporaryCommandBuffer();

		VkCommandBufferBeginInfo beginInfo;
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.pNext = nullptr;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		beginInfo.pInheritanceInfo = nullptr;

		vkBeginCommandBuffer(temp, &beginInfo);

		//cmds
		VkImageMemoryBarrier barrier;
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture.image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipCount;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = layerCount;

		vkCmdPipelineBarrier(temp, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkBufferImageCopy region;
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = layerCount;
		region.imageOffset = { 0,0,0 };
		region.imageExtent = { width,height,1 };

		vkCmdCopyBufferToImage(temp, stagingBuffer.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		// Generate mipmaps
		// This needs to be done even if there are no mipmaps, to convert the texture into the correct format
		VkImageMemoryBarrier mipBarrier{};
		mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		mipBarrier.pNext = nullptr;
		mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		mipBarrier.image = texture.image;
		mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		mipBarrier.subresourceRange.levelCount = 1;
		mipBarrier.subresourceRange.baseArrayLayer = 0;
		mipBarrier.subresourceRange.layerCount = layerCount;

		s32 mipWidth = width;
		s32 mipHeight = height;

		for (int i = 1; i < mipCount; i++)
		{
			mipBarrier.subresourceRange.baseMipLevel = i - 1;
			mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			mipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			mipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

			vkCmdPipelineBarrier(temp, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

			VkImageBlit blit{};
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = layerCount;
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = layerCount;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };

			vkCmdBlitImage(temp, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(temp, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

			if (mipWidth > 1)
				mipWidth /= 2;
			if (mipHeight > 1)
				mipHeight /= 2;
		}

		mipBarrier.subresourceRange.baseMipLevel = mipCount - 1;
		mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		vkCmdPipelineBarrier(temp, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

		vkEndCommandBuffer(temp);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &temp;

		vkQueueSubmit(primaryQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(primaryQueue);

		vkFreeCommandBuffers(device, primaryCommandPool, 1, &temp);

		FreeBuffer(stagingBuffer);
		
		return textures.Add(texture);
	}
	void Vulkan::FreeTexture(TextureHandle handle) {
		const TextureImpl& texture = textures[handle];

		vkDestroySampler(device, texture.sampler, nullptr);
		vkDestroyImageView(device, texture.view, nullptr);
		vkDestroyImage(device, texture.image, nullptr);
		vkFreeMemory(device, texture.memory, nullptr);

		textures.Remove(handle);
	}

	void Vulkan::BeginRenderCommands() {
		CommandBuffer& cmd = primaryCommandBuffers[currentCbIndex];
		// Wait for drawing to finish if it hasn't
		vkWaitForFences(device, 1, &cmd.cmdFence, VK_TRUE, UINT64_MAX);

		// Get next swapchain image index
		VkResult err = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, cmd.imageAcquiredSemaphore, VK_NULL_HANDLE, &currentSwapchainImageIndex);
		if (err != VK_SUCCESS) {
		}

		vkResetFences(device, 1, &cmd.cmdFence);
		vkResetCommandBuffer(cmd.cmdBuffer, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0; // Optional
		beginInfo.pInheritanceInfo = nullptr; // Optional

		if (vkBeginCommandBuffer(cmd.cmdBuffer, &beginInfo) != VK_SUCCESS) {
			DEBUG_ERROR("failed to begin recording command buffer!");
		}

		// Should be ready to draw now!
	}
	void Vulkan::DoFinalBlit() {
		CommandBuffer& cmd = primaryCommandBuffers[currentCbIndex];
		SwapchainImage& swap = swapchainImages[currentSwapchainImageIndex];
		VkExtent2D extent = surfaceCapabilities.currentExtent;

		VkRenderPassBeginInfo renderPassInfo;
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.pNext = nullptr;
		renderPassInfo.renderPass = finalBlitRenderPass;
		renderPassInfo.framebuffer = swap.framebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extent;

		VkClearValue clearColor = { 0,0,0,1 };
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(cmd.cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)(extent.width);
		viewport.height = (float)(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd.cmdBuffer, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(cmd.cmdBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(cmd.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline);
		vkCmdDraw(cmd.cmdBuffer, 4, 1, 0, 0);

		vkCmdEndRenderPass(cmd.cmdBuffer);
	}
	void Vulkan::EndRenderCommands() {
		CommandBuffer& cmd = primaryCommandBuffers[currentCbIndex];

		if (vkEndCommandBuffer(cmd.cmdBuffer) != VK_SUCCESS) {
			DEBUG_ERROR("failed to record command buffer!");
		}

		// Submit the above commands
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		VkSemaphore waitSemaphores[] = { cmd.imageAcquiredSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd.cmdBuffer;
		VkSemaphore signalSemaphores[] = { cmd.drawCompleteSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		VkResult err = vkQueueSubmit(primaryQueue, 1, &submitInfo, cmd.cmdFence);

		// Present to swapchain
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		VkSwapchainKHR swapchains[] = { swapchain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapchains;
		presentInfo.pImageIndices = &currentSwapchainImageIndex;
		presentInfo.pResults = nullptr; // Optional

		vkQueuePresentKHR(primaryQueue, &presentInfo);

		// Advance cb index
		currentCbIndex = (currentCbIndex + 1) % COMMAND_BUFFER_COUNT;
	}
	
}