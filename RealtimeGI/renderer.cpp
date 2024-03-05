#include "renderer.h"
#include "system.h"
#include <algorithm>

namespace Rendering {
	Drawcall::Drawcall(): id(0) {}
	Drawcall::Drawcall(const Drawcall& other) : id(other.id) {}
	Drawcall::Drawcall(u16 dataIndex, MeshHandle mesh, MaterialHandle mat, RenderLayer layer) {
		id = 0;
		id += (u64)(dataIndex % 0x1000);
		id += (u64)mesh << 12;
		id += (u64)mat << 20;
		id += (u64)layer << 56;
	}

	std::strong_ordering Drawcall::operator<=>(const Drawcall& other) const {
		if (id < other.id) return std::strong_ordering::less;
		if (id > other.id) return std::strong_ordering::greater;
		else return std::strong_ordering::equal;
	}

	RenderLayer Drawcall::Layer() const {
		return (RenderLayer)(id >> 56);
	}
	MaterialHandle Drawcall::Material() const {
		return (MaterialHandle)((id >> 20) % 0x1000);
	}
	MeshHandle Drawcall::Mesh() const {
		return (MeshHandle)((id >> 12) % 0x100);
	}
	u16 Drawcall::DataIndex() const {
		return (u16)(id % 0x1000);
	}


	Renderer::Renderer(HINSTANCE hInst, HWND hWindow): vulkan(hInst, hWindow) {
		drawcallData = (DrawcallData*)calloc(maxDrawcallCount, sizeof(DrawcallData));
		instanceData = (PerInstanceData*)calloc(maxInstanceCount, sizeof(PerInstanceData));

		renderQueue = (Drawcall*)calloc(maxDrawcallCount, sizeof(Drawcall));
		drawcallCount = 0;
		instanceCount = 0;

		mainCamera = Camera{};
		lightingData = LightingData{};
	}

	Renderer::~Renderer() {
		free(drawcallData);
		free(instanceData);
		free(renderQueue);
	}

	MeshHandle Renderer::CreateMesh(std::string name, const MeshCreateInfo& info) {
		if (meshNameMap.contains(name)) {
			DEBUG_LOG("Mesh with name %s already exists", name.c_str());
			return -1;
		}

		if (info.triangles == nullptr) {
			DEBUG_ERROR("Triangles cannot be null");
		}

		auto handle = vulkan.CreateMesh(info);
		meshNameMap[name] = handle;
		return handle;
	}

	TextureHandle Renderer::CreateTexture(std::string name, const TextureCreateInfo& info) {
		if (textureNameMap.contains(name)) {
			DEBUG_LOG("Texture with name %s already exists", name.c_str());
			return -1;
		}

		auto handle = vulkan.CreateTexture(info);
		textureNameMap[name] = handle;
		return handle;
	}

	ShaderHandle Renderer::CreateShader(std::string name, const ShaderCreateInfo& info) {
		if (shaderNameMap.contains(name)) {
			DEBUG_LOG("Shader with name %s already exists", name.c_str());
			return -1;
		}

		if (info.metadata.dataLayout.dataSize > maxShaderDataBlockSize) {
			DEBUG_ERROR("Data size too large");
		}

		auto handle = vulkan.CreateShader(info);
		meshNameMap[name] = handle;
		shaderMetadataMap[handle] = info.metadata;
		return handle;
	}

	MaterialHandle Renderer::CreateMaterial(std::string name, const MaterialCreateInfo& info) {
		if (materialNameMap.contains(name)) {
			DEBUG_LOG("Material with name %s already exists", name.c_str());
			return -1;
		}

		auto handle = vulkan.CreateMaterial(info);
		materialNameMap[name] = handle;
		materialMetadataMap[handle] = info.metadata;
		return handle;
	}

	void Renderer::UpdateCamera(const Transform& transform, r32 fov, r32 nearClip, r32 farClip) {
		mainCamera.transform = transform;
		mainCamera.fov = fov;
		mainCamera.nearClip = nearClip;
		mainCamera.farClip = farClip;

		RecalculateCameraMatrices();
	}

	void Renderer::UpdateMainLight(const Transform& transform, const Color& color) {
		lightingData.mainLightMat = GetTransformMatrix(transform);
		lightingData.mainLightColor = color;
		static const r32 shadowmapArea = 25.0f;
		lightingData.mainLightProjMat = glm::ortho(-shadowmapArea / 2.0f, shadowmapArea / 2.0f, shadowmapArea / 2.0f, -shadowmapArea / 2.0f, -1024.0f, 1024.0f);
		lightingData.mainLightDirection = -glm::vec4(transform.rotation*glm::vec3(0.0f, 0.0f, 1.0f), 0.0);
	}

	void Renderer::UpdateAmbientLight(const Color& color) {
		lightingData.ambientColor = color;
	}

	void Renderer::DrawMesh(MeshHandle mesh, MaterialHandle material, const Transform& transform) {
		u16 instanceOffset = instanceCount++;
		u16 callIndex = drawcallCount++;

		DrawcallData data = { 1, instanceOffset };

		drawcallData[callIndex] = data;
		
		instanceData[instanceOffset] = { GetTransformMatrix(transform) };

		RenderLayer layer = shaderMetadataMap[materialMetadataMap[material].shader].layer;
		Drawcall call(callIndex, mesh, material, layer);

		renderQueue[callIndex] = call;
	}

	void Renderer::Render() {
		// Sort drawcalls
		std::sort(&renderQueue[0], &renderQueue[drawcallCount]);

		vulkan.SetInstanceData(instanceData, instanceCount);
		vulkan.SetCameraData(mainCamera.data);
		vulkan.SetLightingData(lightingData);

		vulkan.BeginRenderCommands();
		vulkan.BeginForwardRenderPass();
		for (u32 i = 0; i < drawcallCount; i++) {
			const Drawcall& call = renderQueue[i];
			MeshHandle meshHandle = call.Mesh();
			MaterialHandle matHandle = call.Material();
			MaterialMetadata matData = materialMetadataMap[matHandle];
			u16 dataIndex = call.DataIndex();
			DrawcallData data = drawcallData[dataIndex];
			vulkan.DrawMesh(meshHandle, matData.shader, matHandle, data.instanceOffset, data.instanceCount);
		}
		vulkan.EndRenderPass();
		vulkan.DoFinalBlit();
		vulkan.EndRenderCommands();

		// Clear render queue
		drawcallCount = 0;
		instanceCount = 0;
	}

	void Renderer::ResizeSurface() {
		DEBUG_LOG("Resize");
		vulkan.RecreateSwapchain();
		RecalculateCameraMatrices();
	}

	glm::mat4x4 Renderer::GetTransformMatrix(const Transform& transform) const {
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.position);
		const Quaternion& rot = transform.rotation;
		glm::mat4 rotation = {
			1 - 2 * rot.y * rot.y - 2 * rot.z * rot.z, 2 * rot.x * rot.y + 2 * rot.z * rot.w, 2 * rot.x * rot.z - 2 * rot.y * rot.w, 0,
			2 * rot.x * rot.y - 2 * rot.z * rot.w, 1 - 2 * rot.x * rot.x - 2 * rot.z * rot.z, 2 * rot.y * rot.z + 2 * rot.x * rot.w, 0,
			2 * rot.x * rot.z + 2 * rot.y * rot.w, 2 * rot.y * rot.z - 2 * rot.x * rot.w, 1 - 2 * rot.x * rot.x - 2 * rot.y * rot.y, 0,
			0, 0, 0, 1
		};
		glm::mat4 scale = glm::scale(glm::mat4(1.0f), transform.scale);
		return translation * rotation * scale;
	}

	void Renderer::RecalculateCameraMatrices() {
		glm::mat4 transMat = GetTransformMatrix(mainCamera.transform);
		mainCamera.data.view = glm::inverse(transMat);
		r32 aspect = vulkan.GetSurfaceAspect();
		mainCamera.data.proj = glm::perspective(glm::radians(mainCamera.fov), aspect, mainCamera.nearClip, mainCamera.farClip);
		mainCamera.data.pos = mainCamera.transform.position;
	}
}