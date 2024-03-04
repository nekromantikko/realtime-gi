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
		MeshHandle CreateMesh(const MeshData& data);
		void FreeMesh(MeshHandle handle);

		r32 GetSurfaceAspect() const;

		void SetInstanceData(PerInstanceData* instances, u32 length);
		void SetCameraData(CameraData cameraData);
		void SetLightingData(LightingData lightingData);
		void BeginRenderCommands();
		void DoFinalBlit();
		void EndRenderCommands();
	private:
		struct Buffer {
			VkBuffer buffer;
			VkDeviceMemory memory;
		};

		struct TextureImpl {
			VkImage image;
			VkImageView view;
			VkDeviceMemory memory;
			VkSampler sampler;
		};

		struct MeshImpl {
			u32 vertexCount;
			Buffer vertexPositionBuffer;
			Buffer vertexTexcoord0Buffer;
			Buffer vertexNormalBuffer;
			Buffer vertexTangentBuffer;
			Buffer vertexColorBuffer;

			u32 indexCount;
			Buffer indexBuffer;
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
		void CreateUniformBuffers();
		void FreeUniformBuffers();

		u32 GetDeviceMemoryTypeIndex(u32 typeFilter, VkMemoryPropertyFlags propertyFlags);
		VkCommandBuffer GetTemporaryCommandBuffer();
		void AllocateMemory(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties, VkDeviceMemory& outMemory);
		void AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, Buffer& outBuffer);
		void CopyBuffer(const VkBuffer& src, const VkBuffer& dst, VkDeviceSize size);
		void CopyRawDataToBuffer(void* src, const VkBuffer& dst, VkDeviceSize size);
		void FreeBuffer(const Buffer& buffer);
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

		// Render passes
		VkRenderPass forwardRenderPass;
		VkRenderPass finalBlitRenderPass;

		// Pipelines
		VkShaderModule blitVert;
		VkShaderModule blitFrag;
		VkPipelineLayout blitPipelineLayout;
		VkPipeline blitPipeline;

		// Shader bindings
		static constexpr u32 cameraDataBinding = 0;
		Buffer cameraDataBuffer;

		static constexpr u32 lightingDataBinding = 1;
		Buffer lightingDataBuffer;

		static constexpr u32 perInstanceDataBinding = 2;
		Buffer perInstanceBuffer;
		u32 perInstanceDynamicOffset;

		static constexpr u32 shaderDataBinding = 3;
		Buffer shaderDataBuffer;

		static constexpr u32 samplerBinding = 4;
		static constexpr u32 maxSamplerCount = 8;
		MemoryPool<TextureImpl> textures = MemoryPool<TextureImpl>(maxTextureCount);

		MemoryPool<MeshImpl> meshes = MemoryPool<MeshImpl>(maxVertexBufferCount);

		// Render targets
		static constexpr u32 colorBinding = 14;
		FramebufferAttachemnt colorAttachment;
		FramebufferAttachemnt colorAttachmentResolve;
		static constexpr u32 depthBinding = 15;
		FramebufferAttachemnt depthAttachment;
		FramebufferAttachemnt depthAttachmentResolve;

		VkFramebuffer primaryFramebuffer;
		VkSampler primaryFramebufferSampler;
	};
}