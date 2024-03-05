#pragma once
#include <windows.h>
#include <vector>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "rendering.h"
#include "material.h"
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
		TextureHandle CreateTexture(const TextureCreateInfo& info);
		void FreeTexture(TextureHandle handle);
		MeshHandle CreateMesh(const MeshCreateInfo& info);
		void FreeMesh(MeshHandle handle);
		ShaderHandle CreateShader(const ShaderCreateInfo& info);
		void FreeShader(ShaderHandle handle);
		MaterialHandle CreateMaterial(const MaterialCreateInfo& info);
		void UpdateMaterialData(MaterialHandle handle, void* data, u32 offset, u32 size);
		void UpdateMaterialTexture(MaterialHandle handle, u32 index, TextureHandle texture);
		void FreeMaterial(MaterialHandle handle);

		r32 GetSurfaceAspect() const;

		void SetInstanceData(PerInstanceData* instances, u32 length);
		void SetCameraData(CameraData cameraData);
		void SetLightingData(LightingData lightingData);
		void BeginRenderCommands();
		void BeginForwardRenderPass();
		void DrawMesh(MeshHandle mesh, ShaderHandle shader, MaterialHandle mat, u16 instanceOffset, u16 instanceCount);
		void EndRenderPass();
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

		enum DescriptorSetLayoutFlags
		{
			DSF_NONE = 0,
			DSF_CAMERADATA = 1,
			DSF_LIGHTINGDATA = 1 << 1,
			DSF_INSTANCEDATA = 1 << 2,
			DSF_SHADERDATA = 1 << 3,
			DSF_SHADOWMAP = 1 << 4,
			DSF_CUBEMAP = 1 << 5,
			DSF_COLOR_TEX = 1 << 6,
			DSF_DEPTH_TEX = 1 << 7
		};

		struct DescriptorSetLayoutInfo
		{
			DescriptorSetLayoutFlags flags;
			u32 samplerCount;
			u32 bindingCount;
		};

		struct ShaderImpl {
			VkPipelineLayout pipelineLayout;
			VkPipeline pipeline;
			VkDescriptorSetLayout descriptorSetLayout;
			DescriptorSetLayoutInfo layoutInfo;
			VertexAttribFlags vertexInputs;
		};

		struct MaterialImpl {
			VkDescriptorSet descriptorSet;
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

		s32 GetDeviceMemoryTypeIndex(u32 typeFilter, VkMemoryPropertyFlags propertyFlags);
		VkCommandBuffer GetTemporaryCommandBuffer();
		void AllocateMemory(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties, VkDeviceMemory& outMemory);
		void AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, Buffer& outBuffer);
		void CopyBuffer(const VkBuffer& src, const VkBuffer& dst, VkDeviceSize size);
		void CopyRawDataToBuffer(void* src, const VkBuffer& dst, VkDeviceSize size);
		void FreeBuffer(const Buffer& buffer);
		VkShaderModule CreateShaderModule(const char* code, const u32 size);
		void CreateDescriptorSetLayout(VkDescriptorSetLayout& layout, const DescriptorSetLayoutInfo& info);
		void CreateShaderRenderPipeline(VkPipelineLayout& outLayout, VkPipeline &outPipeline, const VkDescriptorSetLayout &descSetLayout, VertexAttribFlags vertexInputs, const char* vert, const char* frag);
		void InitializeDescriptorSet(VkDescriptorSet descriptorSet, const DescriptorSetLayoutInfo& info, const MaterialHandle matHandle, const TextureHandle* textures);
		void UpdateDescriptorSetSampler(VkDescriptorSet descriptorSet, u32 binding, VkDescriptorImageInfo info);
		void UpdateDescriptorSetBuffer(VkDescriptorSet descriptorSet, u32 binding, VkDescriptorBufferInfo info, bool dynamic = false);

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
		VkDescriptorSet blitDescriptorSet;
		VkDescriptorSetLayout blitDescriptorSetLayout;

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

		static constexpr u32 samplerBinding = 4; // 4 - 11 reserved for generic samplers
		MemoryPool<TextureImpl> textures = MemoryPool<TextureImpl>(maxTextureCount);

		MemoryPool<MeshImpl> meshes = MemoryPool<MeshImpl>(maxVertexBufferCount);
		MemoryPool<ShaderImpl> shaders = MemoryPool<ShaderImpl>(maxShaderCount);
		MemoryPool<MaterialImpl> materials = MemoryPool<MaterialImpl>(maxMaterialCount);

		static constexpr u32 shadowMapBinding = 12;
		static constexpr u32 envMapBinding = 13;

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