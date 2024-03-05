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

		// Likely overkill pool sizes
		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo{};
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorPoolInfo.poolSizeCount = 11;
		descriptorPoolInfo.pPoolSizes = poolSizes;
		descriptorPoolInfo.maxSets = 1000;

		vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool);

		CreatePrimaryCommandPoolAndBuffers();

		CreateFramebufferAttachments();
		CreatePrimaryFramebuffer();

		CreateUniformBuffers();

		CreateBlitPipeline();
	}
	Vulkan::~Vulkan() {
		// Wait for all commands to execute first
		WaitForAllCommands();

		// Free all user-created resources
		for (u32 i = 0; i < textures.Count(); i++) {
			TextureHandle handle = textures.GetHandle(i);
			FreeTexture(handle);
		}
		for (u32 i = 0; i < meshes.Count(); i++) {
			MeshHandle handle = meshes.GetHandle(i);
			FreeMesh(handle);
		}
		for (u32 i = 0; i < shaders.Count(); i++) {
			ShaderHandle handle = shaders.GetHandle(i);
			FreeShader(handle);
		}
		for (u32 i = 0; i < materials.Count(); i++) {
			MaterialHandle handle = materials.GetHandle(i);
			FreeMaterial(handle);
		}

		FreeBlitPipeline();

		FreeUniformBuffers();

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

		// TODO: This has to be done to ALL descriptor sets that use color / depth!
		DescriptorSetLayoutInfo info;
		info.flags = (DescriptorSetLayoutFlags)(DSF_CAMERADATA | DSF_COLOR_TEX | DSF_DEPTH_TEX);
		info.samplerCount = 0;
		info.bindingCount = 3;

		InitializeDescriptorSet(blitDescriptorSet, info, -1, nullptr);
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

		for (u32 i = 0; i < queueFamilyCount; i++) {
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
		DescriptorSetLayoutInfo info;
		info.flags = (DescriptorSetLayoutFlags)(DSF_CAMERADATA | DSF_COLOR_TEX | DSF_DEPTH_TEX);
		info.samplerCount = 0;
		info.bindingCount = 3;

		// Descriptor set
		CreateDescriptorSetLayout(blitDescriptorSetLayout, info);

		VkDescriptorSetAllocateInfo allocInfo;
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &blitDescriptorSetLayout;

		vkAllocateDescriptorSets(device, &allocInfo, &blitDescriptorSet);
		InitializeDescriptorSet(blitDescriptorSet, info, -1, nullptr);

		// Pipeline
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
		blitPipelineLayoutInfo.setLayoutCount = 1;
		blitPipelineLayoutInfo.pSetLayouts = &blitDescriptorSetLayout;

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

		vkDestroyDescriptorSetLayout(device, blitDescriptorSetLayout, nullptr);
	}

	void Vulkan::CreateUniformBuffers() {
		AllocateBuffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cameraDataBuffer);
		AllocateBuffer(sizeof(LightingData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, lightingDataBuffer);
		AllocateBuffer(sizeof(PerInstanceData) * maxInstanceCount, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, perInstanceBuffer);
		AllocateBuffer(maxShaderDataBlockSize * maxMaterialCount, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, shaderDataBuffer);
	}
	void Vulkan::FreeUniformBuffers() {
		FreeBuffer(cameraDataBuffer);
		FreeBuffer(lightingDataBuffer);
		FreeBuffer(perInstanceBuffer);
		FreeBuffer(shaderDataBuffer);
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
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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

		VkSamplerCreateInfo samplerInfo;
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.pNext = nullptr;
		samplerInfo.flags = 0;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 0;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		vkCreateSampler(device, &samplerInfo, nullptr, &primaryFramebufferSampler);
	}
	void Vulkan::FreePrimaryFramebuffer() {
		vkDestroySampler(device, primaryFramebufferSampler, nullptr);
		vkDestroyFramebuffer(device, primaryFramebuffer, nullptr);
	}

	s32 Vulkan::GetDeviceMemoryTypeIndex(u32 typeFilter, VkMemoryPropertyFlags propertyFlags) {
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

		for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
			if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags) {
				return (s32)i;
			}
		}

		return -1;
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

	void Vulkan::CopyRawDataToBuffer(void* src, const VkBuffer& dst, VkDeviceSize size) {
		Buffer stagingBuffer{};
		AllocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer);

		void* data;
		vkMapMemory(device, stagingBuffer.memory, 0, size, 0, &data);
		memcpy(data, src, size);
		vkUnmapMemory(device, stagingBuffer.memory);

		CopyBuffer(stagingBuffer.buffer, dst, size);

		FreeBuffer(stagingBuffer);
	}

	void Vulkan::FreeBuffer(const Buffer& buffer) {
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

	void Vulkan::CreateDescriptorSetLayout(VkDescriptorSetLayout& layout, const DescriptorSetLayoutInfo& info) {
		if (info.samplerCount > maxSamplerCount) {
			DEBUG_ERROR("Max sampler count exceeded");
		}

		std::vector<VkDescriptorSetLayoutBinding> bindings(info.bindingCount);

		u32 bindingIndex = 0;

		// Camera data
		if ((info.flags & DSF_CAMERADATA) == DSF_CAMERADATA)
		{
			bindings[bindingIndex].binding = cameraDataBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Lighting data
		if ((info.flags & DSF_LIGHTINGDATA) == DSF_LIGHTINGDATA)
		{
			bindings[bindingIndex].binding = lightingDataBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Instance data
		if ((info.flags & DSF_INSTANCEDATA) == DSF_INSTANCEDATA)
		{
			bindings[bindingIndex].binding = perInstanceDataBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Material data
		if ((info.flags & DSF_SHADERDATA) == DSF_SHADERDATA)
		{
			bindings[bindingIndex].binding = shaderDataBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		for (u32 i = 0; i < info.samplerCount; i++)
		{
			bindings[bindingIndex].binding = samplerBinding + i;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Shadowmap
		if ((info.flags & DSF_SHADOWMAP) == DSF_SHADOWMAP)
		{
			bindings[bindingIndex].binding = shadowMapBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Env cubemap
		if ((info.flags & DSF_CUBEMAP) == DSF_CUBEMAP)
		{
			bindings[bindingIndex].binding = envMapBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Dolor texture
		if ((info.flags & DSF_COLOR_TEX) == DSF_COLOR_TEX)
		{
			bindings[bindingIndex].binding = colorBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Depth texture
		if ((info.flags & DSF_DEPTH_TEX) == DSF_DEPTH_TEX)
		{
			bindings[bindingIndex].binding = depthBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo;
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.pNext = nullptr;
		layoutInfo.flags = 0;
		layoutInfo.bindingCount = info.bindingCount;
		layoutInfo.pBindings = bindings.data();

		vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout);
	}

	void Vulkan::CreateShaderRenderPipeline(VkPipelineLayout& outLayout, VkPipeline& outPipeline, const VkDescriptorSetLayout &descSetLayout, VertexAttribFlags vertexInputs, const char* vert, const char* frag) {
		// Create shader modules
		u32 vertShaderLength;
		char* vertShaderBytes = AllocFileBytes(vert, vertShaderLength);
		u32 fragShaderLength;
		char* fragShaderBytes = AllocFileBytes(frag, fragShaderLength);

		VkShaderModule vertShader = CreateShaderModule(vertShaderBytes, vertShaderLength);

		VkPipelineShaderStageCreateInfo vertShaderStageInfo;
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.pNext = nullptr;
		vertShaderStageInfo.flags = 0;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShader;
		vertShaderStageInfo.pName = "main";
		vertShaderStageInfo.pSpecializationInfo = nullptr;

		VkShaderModule fragShader = CreateShaderModule(fragShaderBytes, fragShaderLength);

		VkPipelineShaderStageCreateInfo fragShaderStageInfo;
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.pNext = nullptr;
		fragShaderStageInfo.flags = 0;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShader;
		fragShaderStageInfo.pName = "main";
		fragShaderStageInfo.pSpecializationInfo = nullptr;

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		// Vertex input
		VkVertexInputBindingDescription vertDescriptions[10];
		VkVertexInputAttributeDescription attributeDescriptions[10];
		u32 vertexInputCount = 0;

		if (vertexInputs & VERTEX_POSITION_BIT) {
			vertDescriptions[vertexInputCount].binding = 0;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec3);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 0;
			attributeDescriptions[vertexInputCount].location = 0;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}
		if (vertexInputs & VERTEX_TEXCOORD_0_BIT) {
			vertDescriptions[vertexInputCount].binding = 1;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec2);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 1;
			attributeDescriptions[vertexInputCount].location = 1;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}
		if (vertexInputs & VERTEX_NORMAL_BIT) {
			vertDescriptions[vertexInputCount].binding = 2;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec3);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 2;
			attributeDescriptions[vertexInputCount].location = 2;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}
		if (vertexInputs & VERTEX_TANGENT_BIT) {
			vertDescriptions[vertexInputCount].binding = 3;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec4);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 3;
			attributeDescriptions[vertexInputCount].location = 3;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}
		if (vertexInputs & VERTEX_COLOR_BIT) {
			vertDescriptions[vertexInputCount].binding = 4;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec4);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 4;
			attributeDescriptions[vertexInputCount].location = 4;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}

		VkPipelineVertexInputStateCreateInfo vertexInputInfo;
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.pNext = nullptr;
		vertexInputInfo.flags = 0;
		vertexInputInfo.vertexBindingDescriptionCount = vertexInputCount;
		vertexInputInfo.pVertexBindingDescriptions = vertDescriptions;
		vertexInputInfo.vertexAttributeDescriptionCount = vertexInputCount;
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly;
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.pNext = nullptr;
		inputAssembly.flags = 0;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		////////////////////////////////////////////////////////

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = nullptr; // Will be set at render time
		viewportState.scissorCount = 1;
		viewportState.pScissors = nullptr; // Will be set at render time

		////////////////////////////////////////////////////////

		VkPipelineRasterizationStateCreateInfo rasterizer;
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.pNext = nullptr;
		rasterizer.flags = 0;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional
		rasterizer.lineWidth = 1.0f;

		////////////////////////////////////////////////////////

		VkPipelineMultisampleStateCreateInfo multisampling;
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.pNext = nullptr;
		multisampling.flags = 0;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		////////////////////////////////////////////////////////

		VkPipelineColorBlendAttachmentState colorBlendAttachment;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineDepthStencilStateCreateInfo depthStencil;
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.pNext = nullptr;
		depthStencil.flags = 0;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;
		depthStencil.front = VkStencilOpState{};
		depthStencil.back = VkStencilOpState{};
		depthStencil.minDepthBounds = 0.0f; // Optional
		depthStencil.maxDepthBounds = 1.0f; // Optional

		VkPipelineColorBlendStateCreateInfo colorBlending;
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.pNext = nullptr;
		colorBlending.flags = 0;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		////////////////////////////////////////////////////////

		VkPipelineLayoutCreateInfo pipelineLayoutInfo;
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.pNext = nullptr;
		pipelineLayoutInfo.flags = 0;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
		pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

		vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &outLayout);

		////////////////////////////////////////////////////////

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

		VkGraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.pNext = nullptr;
		pipelineInfo.flags = 0;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicStateInfo;
		pipelineInfo.layout = outLayout;
		pipelineInfo.renderPass = forwardRenderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
		pipelineInfo.basePipelineIndex = -1; // Optional

		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline);

		vkDestroyShaderModule(device, vertShader, nullptr);
		vkDestroyShaderModule(device, fragShader, nullptr);
	}

	void Vulkan::InitializeDescriptorSet(VkDescriptorSet descriptorSet, const DescriptorSetLayoutInfo& info, const MaterialHandle matHandle, const TextureHandle* texHandles) {
		if ((info.flags & DSF_CAMERADATA) == DSF_CAMERADATA)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = cameraDataBuffer.buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = VK_WHOLE_SIZE;

			UpdateDescriptorSetBuffer(descriptorSet, cameraDataBinding, bufferInfo);
		}

		if ((info.flags & DSF_LIGHTINGDATA) == DSF_LIGHTINGDATA)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = lightingDataBuffer.buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = VK_WHOLE_SIZE;

			UpdateDescriptorSetBuffer(descriptorSet, lightingDataBinding, bufferInfo);
		}

		if ((info.flags & DSF_INSTANCEDATA) == DSF_INSTANCEDATA)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = perInstanceBuffer.buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = VK_WHOLE_SIZE;

			UpdateDescriptorSetBuffer(descriptorSet, perInstanceDataBinding, bufferInfo, true);
		}

		if ((info.flags & DSF_SHADERDATA) == DSF_SHADERDATA)
		{
			if (matHandle < 0) {
				DEBUG_ERROR("Invalid material handle");
			}
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = shaderDataBuffer.buffer;
			bufferInfo.offset = maxShaderDataBlockSize * matHandle;
			bufferInfo.range = maxShaderDataBlockSize;

			UpdateDescriptorSetBuffer(descriptorSet, shaderDataBinding, bufferInfo, false);
		}

		if (info.samplerCount > 0 && texHandles == nullptr) {
			DEBUG_ERROR("Invalid texture input");
		}

		for (u32 i = 0; i < info.samplerCount; i++)
		{
			const TextureHandle texHandle = texHandles[i];
			if (texHandle < 0) {
				DEBUG_ERROR("invalid texture handle");
			}
			const TextureImpl& texture = textures[texHandle];

			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = texture.view;
			imageInfo.sampler = texture.sampler;

			UpdateDescriptorSetSampler(descriptorSet, samplerBinding + i, imageInfo);
		}

		// TODO
		/*if ((info.flags & DSF_SHADOWMAP) == DSF_SHADOWMAP)
		{
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			imageInfo.imageView = shadowMap.view;
			imageInfo.sampler = shadowMap.sampler;

			UpdateDescriptorSetSampler(descriptorSet, shadowMapBinding, imageInfo);
		}

		if ((info.flags & DSF_CUBEMAP) == DSF_CUBEMAP)
		{
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = envMap.view;
			imageInfo.sampler = envMap.sampler;

			UpdateDescriptorSetSampler(descriptorSet, envMapBinding, imageInfo);
		}*/

		if ((info.flags & DSF_COLOR_TEX) == DSF_COLOR_TEX)
		{
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = colorAttachmentResolve.view;
			imageInfo.sampler = primaryFramebufferSampler;

			UpdateDescriptorSetSampler(descriptorSet, colorBinding, imageInfo);
		}

		if ((info.flags & DSF_DEPTH_TEX) == DSF_DEPTH_TEX)
		{
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = depthAttachmentResolve.view;
			imageInfo.sampler = primaryFramebufferSampler;

			UpdateDescriptorSetSampler(descriptorSet, depthBinding, imageInfo);
		}
	}

	void Vulkan::UpdateDescriptorSetSampler(const VkDescriptorSet descriptorSet, u32 binding, VkDescriptorImageInfo info) {
		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.pNext = nullptr;
		descriptorWrite.dstSet = descriptorSet;
		descriptorWrite.dstBinding = binding;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.pBufferInfo = nullptr;
		descriptorWrite.pImageInfo = &info;
		descriptorWrite.pTexelBufferView = nullptr;

		vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
	}

	void Vulkan::UpdateDescriptorSetBuffer(VkDescriptorSet descriptorSet, u32 binding, VkDescriptorBufferInfo info, bool dynamic) {
		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.pNext = nullptr;
		descriptorWrite.dstSet = descriptorSet;
		descriptorWrite.dstBinding = binding;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.pBufferInfo = &info;
		descriptorWrite.pImageInfo = nullptr;
		descriptorWrite.pTexelBufferView = nullptr;

		vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
	}

	TextureHandle Vulkan::CreateTexture(const TextureCreateInfo& info) {
		VkFormat format = VK_FORMAT_UNDEFINED;
		u32 layerCount = info.type == TEXTURE_CUBEMAP ? 6 : 1;

		switch (info.space) {
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
		if (info.generateMips) {
			mipCount += (u32)std::floor(std::log2(MAX(info.width, info.height)));
		}

		TextureImpl texture{};

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.flags = info.type == TEXTURE_CUBEMAP ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = info.width;
		imageInfo.extent.height = info.height;
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
		viewInfo.viewType = info.type == TEXTURE_CUBEMAP ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
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
		samplerInfo.magFilter = (VkFilter)info.filter;
		samplerInfo.minFilter = (VkFilter)info.filter;
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
		samplerInfo.maxLod = (r32)mipCount;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler);

		// Copy data
		Buffer stagingBuffer;
		VkDeviceSize imageBytes = info.width * info.height * 4;
		if (info.type == TEXTURE_CUBEMAP) {
			imageBytes *= 6;
		}
		AllocateBuffer(imageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer);

		void* data;
		vkMapMemory(device, stagingBuffer.memory, 0, imageBytes, 0, &data);
		memcpy(data, info.pixels, imageBytes);
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
		region.imageExtent = { info.width,info.height,1 };

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

		s32 mipWidth = info.width;
		s32 mipHeight = info.height;

		for (u32 i = 1; i < mipCount; i++)
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

	MeshHandle Vulkan::CreateMesh(const MeshCreateInfo& data) {
		MeshImpl mesh{};

		if (data.position != nullptr) {
			const VkDeviceSize posBytes = sizeof(glm::vec3) * data.vertexCount;
			AllocateBuffer(posBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexPositionBuffer);
			CopyRawDataToBuffer(data.position, mesh.vertexPositionBuffer.buffer, posBytes);
		}

		if (data.texcoord0 != nullptr) {
			const VkDeviceSize uvBytes = sizeof(glm::vec2) * data.vertexCount;
			AllocateBuffer(uvBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexTexcoord0Buffer);
			CopyRawDataToBuffer(data.texcoord0, mesh.vertexTexcoord0Buffer.buffer, uvBytes);
		}

		if (data.normal != nullptr) {
			const VkDeviceSize normalBytes = sizeof(glm::vec3) * data.vertexCount;
			AllocateBuffer(normalBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexNormalBuffer);
			CopyRawDataToBuffer(data.normal, mesh.vertexNormalBuffer.buffer, normalBytes);
		}

		if (data.tangent != nullptr) {
			const VkDeviceSize tangentBytes = sizeof(glm::vec4) * data.vertexCount;
			AllocateBuffer(tangentBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexTangentBuffer);
			CopyRawDataToBuffer(data.tangent, mesh.vertexTangentBuffer.buffer, tangentBytes);
		}

		if (data.color != nullptr) {
			const VkDeviceSize colorBytes = sizeof(Color) * data.vertexCount;
			AllocateBuffer(colorBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexColorBuffer);
			CopyRawDataToBuffer(data.color, mesh.vertexColorBuffer.buffer, colorBytes);
		}

		const VkDeviceSize indexBytes = sizeof(Triangle) * data.triangleCount;
		AllocateBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer);
		CopyRawDataToBuffer(data.triangles, mesh.indexBuffer.buffer, indexBytes);

		mesh.vertexCount = data.vertexCount;
		mesh.indexCount = data.triangleCount * 3;

		return meshes.Add(mesh);
	}
	void Vulkan::FreeMesh(MeshHandle handle) {
		const MeshImpl& mesh = meshes[handle];

		FreeBuffer(mesh.vertexPositionBuffer);
		FreeBuffer(mesh.vertexTexcoord0Buffer);
		FreeBuffer(mesh.vertexNormalBuffer);
		FreeBuffer(mesh.vertexTangentBuffer);
		FreeBuffer(mesh.vertexColorBuffer);
		FreeBuffer(mesh.indexBuffer);

		meshes.Remove(handle);
	}

	ShaderHandle Vulkan::CreateShader(const ShaderCreateInfo& info) {
		ShaderImpl shader{};

		shader.layoutInfo.flags = (DescriptorSetLayoutFlags)(DSF_CAMERADATA | DSF_LIGHTINGDATA | DSF_INSTANCEDATA | DSF_SHADERDATA | DSF_SHADOWMAP | DSF_CUBEMAP);
		shader.layoutInfo.samplerCount = info.samplerCount;
		shader.layoutInfo.bindingCount = 6 + info.samplerCount;
		shader.vertexInputs = info.vertexInputs;

		CreateDescriptorSetLayout(shader.descriptorSetLayout, shader.layoutInfo);
		CreateShaderRenderPipeline(shader.pipelineLayout, shader.pipeline, shader.descriptorSetLayout, shader.vertexInputs, info.vert, info.frag);

		return shaders.Add(shader);
	}
	void Vulkan::FreeShader(ShaderHandle handle) {
		const ShaderImpl& shader = shaders[handle];

		vkDestroyPipelineLayout(device, shader.pipelineLayout, nullptr);
		vkDestroyPipeline(device, shader.pipeline, nullptr);
		vkDestroyDescriptorSetLayout(device, shader.descriptorSetLayout, nullptr);

		shaders.Remove(handle);
	}

	MaterialHandle Vulkan::CreateMaterial(const MaterialCreateInfo& info) {
		MaterialImpl material{};
		const ShaderImpl& shader = shaders[info.metadata.shader];

		VkDescriptorSetAllocateInfo allocInfo;
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &shader.descriptorSetLayout;

		VkResult res = vkAllocateDescriptorSets(device, &allocInfo, &material.descriptorSet);
		if (res != VK_SUCCESS) {
			DEBUG_ERROR("Oh noes... (%d)", res);
		}

		auto handle = materials.Add(material);

		InitializeDescriptorSet(material.descriptorSet, shader.layoutInfo, handle, info.data.textures);
		UpdateMaterialData(handle, (void*)info.data.data, 0, maxShaderDataBlockSize);

		return handle;
	}
	void Vulkan::UpdateMaterialData(MaterialHandle handle, void* data, u32 offset, u32 size) {
		if (size + offset > maxShaderDataBlockSize) {
			DEBUG_ERROR("Invalid data size (%d) or offset (%d)", size, offset);
		}

		if (size == 0 || data == nullptr) {
			return;
		}
		
		void* temp;
		vkMapMemory(device, shaderDataBuffer.memory, handle * maxShaderDataBlockSize + offset, size, 0, &temp);
		memcpy(temp, data, size);
		vkUnmapMemory(device, shaderDataBuffer.memory);
	}
	void Vulkan::UpdateMaterialTexture(MaterialHandle handle, u32 index, TextureHandle texHandle) {
		if (index >= maxSamplerCount) {
			DEBUG_ERROR("Invalid sampler index %d", index);
		}

		const MaterialImpl& material = materials[handle];
		const TextureImpl& texture = textures[texHandle];

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = texture.sampler;
		imageInfo.imageView = texture.view;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		UpdateDescriptorSetSampler(material.descriptorSet, samplerBinding + index, imageInfo);
	}
	void Vulkan::FreeMaterial(MaterialHandle handle) {
		const MaterialImpl& material = materials[handle];

		vkFreeDescriptorSets(device, descriptorPool, 1, &material.descriptorSet);
		materials.Remove(handle);
	}

	r32 Vulkan::GetSurfaceAspect() const {
		VkExtent2D extent = surfaceCapabilities.currentExtent;
		return extent.width / (r32)extent.height;
	}

	void Vulkan::SetInstanceData(PerInstanceData* instances, u32 length) {
		for (u32 i = 0; i < length; i++) {
			void* data;
			vkMapMemory(device, perInstanceBuffer.memory, i * 256, sizeof(PerInstanceData), 0, &data);
			memcpy(data, &instances[i], sizeof(PerInstanceData));
			vkUnmapMemory(device, perInstanceBuffer.memory);
		}
	}
	void Vulkan::SetCameraData(CameraData cameraData) {
		void* data;
		vkMapMemory(device, cameraDataBuffer.memory, 0, sizeof(CameraData), 0, &data);
		memcpy(data, &cameraData, sizeof(CameraData));
		vkUnmapMemory(device, cameraDataBuffer.memory);
	}
	void Vulkan::SetLightingData(LightingData lightingData) {
		void* data;
		vkMapMemory(device, lightingDataBuffer.memory, 0, sizeof(LightingData), 0, &data);
		memcpy(data, &lightingData, sizeof(LightingData));
		vkUnmapMemory(device, lightingDataBuffer.memory);
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
	// This could be just generic...
	void Vulkan::BeginForwardRenderPass() {
		CommandBuffer& cmd = primaryCommandBuffers[currentCbIndex];
		VkExtent2D extent = surfaceCapabilities.currentExtent;

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.pNext = nullptr;
		renderPassInfo.renderPass = forwardRenderPass;
		renderPassInfo.framebuffer = primaryFramebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extent;

		VkClearValue clearColors[2] = { {0,0,0,1}, {1.0f, 0} };
		renderPassInfo.clearValueCount = 2;
		renderPassInfo.pClearValues = clearColors;

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
	}
	void Vulkan::DrawMesh(MeshHandle meshHandle, ShaderHandle shaderHandle, MaterialHandle matHandle, u16 instanceOffset, u16 instanceCount) {
		CommandBuffer& cmd = primaryCommandBuffers[currentCbIndex];
		const ShaderImpl& shader = shaders[shaderHandle];
		const MaterialImpl& material = materials[matHandle];
		const MeshImpl& mesh = meshes[meshHandle];

		vkCmdBindPipeline(cmd.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader.pipeline);

		u32 dynamicOffset = perInstanceDynamicOffset * instanceOffset;
		vkCmdBindDescriptorSets(cmd.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader.pipelineLayout, 0, 1, &material.descriptorSet, 1, &dynamicOffset);

		VkDeviceSize offset = 0;
		if (shader.vertexInputs & VERTEX_POSITION_BIT)
			vkCmdBindVertexBuffers(cmd.cmdBuffer, 0, 1, &mesh.vertexPositionBuffer.buffer, &offset);
		if (shader.vertexInputs & VERTEX_TEXCOORD_0_BIT)
			vkCmdBindVertexBuffers(cmd.cmdBuffer, 1, 1, &mesh.vertexTexcoord0Buffer.buffer, &offset);
		if (shader.vertexInputs & VERTEX_NORMAL_BIT)
			vkCmdBindVertexBuffers(cmd.cmdBuffer, 2, 1, &mesh.vertexNormalBuffer.buffer, &offset);
		if (shader.vertexInputs & VERTEX_TANGENT_BIT)
			vkCmdBindVertexBuffers(cmd.cmdBuffer, 3, 1, &mesh.vertexTangentBuffer.buffer, &offset);
		if (shader.vertexInputs & VERTEX_COLOR_BIT)
			vkCmdBindVertexBuffers(cmd.cmdBuffer, 4, 1, &mesh.vertexColorBuffer.buffer, &offset);

		vkCmdBindIndexBuffer(cmd.cmdBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

		vkCmdDrawIndexed(cmd.cmdBuffer, mesh.indexCount, instanceCount, 0, 0, 0);
	}
	void Vulkan::EndRenderPass() {
		CommandBuffer& cmd = primaryCommandBuffers[currentCbIndex];
		vkCmdEndRenderPass(cmd.cmdBuffer);
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

		vkCmdBindPipeline(cmd.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline);
		vkCmdBindDescriptorSets(cmd.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipelineLayout, 0, 1, &blitDescriptorSet, 0, nullptr);
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