#pragma once
#include <windows.h>
#include <vector>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "rendering.h"
#include "memory_pool.h"

#define COMMAND_BUFFER_COUNT 2
#define SWAPCHAIN_MIN_IMAGE_COUNT 3

namespace Rendering {
	class Vulkan {
	public:
		Vulkan(HINSTANCE hInst, HWND hWindow);
		~Vulkan();

		void RecreateSwapchain();
		void WaitForAllCommands();
		TextureHandle CreateTexture(void *data, u32 width, u32 height, TextureType type, ColorSpace space = COLORSPACE_SRGB, TextureFilter filter = (TextureFilter)VK_FILTER_LINEAR, bool generateMips = true);
		void FreeTexture(TextureHandle handle);

		void BeginRenderCommands();
		void DoFinalBlit();
		void EndRenderCommands();
	private:
		struct TextureImpl {
			VkImage image;
			VkImageView view;
			VkDeviceMemory memory;
			VkSampler sampler;
		};

		struct Buffer {
			VkBuffer buffer;
			VkDeviceMemory memory;
		};

		struct CommandBuffer {
			VkCommandBuffer cmdBuffer;
			VkFence cmdFence;
			VkSemaphore imageAcquiredSemaphore;
			VkSemaphore drawCompleteSemaphore;
		};

		struct SwapchainImage {
			VkImage image;
			VkImageView view;
			VkFramebuffer framebuffer;
		};

		struct FramebufferAttachemnt {
			VkImage image;
			VkImageView view;
			VkDeviceMemory memory;
		};

		void GetSuitablePhysicalDevice();
		bool IsPhysicalDeviceSuitable(VkPhysicalDevice physicalDevice, u32& outQueueFamilyIndex);
		void CreateLogicalDevice();
		void CreateForwardRenderPass();
		void CreateFinalBlitRenderPass();
		void CreateRenderPasses();
		void FreeRenderPasses();
		void CreateSwapchain(VkRenderPass renderPass);
		void FreeSwapchain();
		void CreatePrimaryCommandPoolAndBuffers();
		void FreePrimaryCommandPoolAndBuffers();
		void CreateFramebufferAttachments();
		void FreeFramebufferAttachments();
		void CreatePrimaryFramebuffer();
		void FreePrimaryFramebuffer();
		void CreateBlitPipeline();
		void FreeBlitPipeline();

		u32 GetDeviceMemoryTypeIndex(u32 typeFilter, VkMemoryPropertyFlags propertyFlags);
		VkCommandBuffer GetTemporaryCommandBuffer();
		void AllocateMemory(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties, VkDeviceMemory& outMemory);
		void AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, Buffer& outBuffer);
		void CopyBuffer(const VkBuffer& src, const VkBuffer& dst, VkDeviceSize size);
		void FreeBuffer(Buffer& buffer);
		VkShaderModule CreateShaderModule(const char* code, const u32 size);

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

		VkCommandPool primaryCommandPool;
		u32 currentCbIndex;
		CommandBuffer primaryCommandBuffers[COMMAND_BUFFER_COUNT];

		VkSwapchainKHR swapchain;
		u32 currentSwapchainImageIndex;
		std::vector<SwapchainImage> swapchainImages;

		VkDescriptorPool descriptorPool;

		// Render targets
		FramebufferAttachemnt colorAttachment;
		FramebufferAttachemnt colorAttachmentResolve;
		FramebufferAttachemnt depthAttachment;
		FramebufferAttachemnt depthAttachmentResolve;

		VkFramebuffer primaryFramebuffer;
		VkSampler primaryFramebufferSampler;

		// Render passes
		VkRenderPass forwardRenderPass;
		VkRenderPass finalBlitRenderPass;

		// Pipelines
		VkShaderModule blitVert;
		VkShaderModule blitFrag;
		VkPipelineLayout blitPipelineLayout;
		VkPipeline blitPipeline;

		MemoryPool<TextureImpl> textures = MemoryPool<TextureImpl>(MAX_TEXTURE_COUNT);
	};
}