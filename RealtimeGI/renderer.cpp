#include "renderer.h"
#include "system.h"
#include <algorithm>

namespace Rendering {
	Renderer::Renderer(HINSTANCE hInst, HWND hWindow): vulkan(hInst, hWindow) {
		drawcallData = (DrawcallData*)calloc(maxDrawcallCount, sizeof(DrawcallData));
		instanceData = (PerInstanceData*)calloc(maxTransformCount, sizeof(PerInstanceData));

		renderQueue = (Drawcall*)calloc(maxDrawcallCount, sizeof(Drawcall));
		queueLength = 0;

		nextDataIndex = 0;
		nextTransformIndex = 0;
	}

	Renderer::~Renderer() {
		free(drawcallData);
		free(instanceData);
		free(renderQueue);
	}

	MeshHandle Renderer::CreateMesh(const MeshData& data) {
		return vulkan.CreateMesh(data);
	}

	void Renderer::UpdateCamera(const Transform& transform, r32 fov, r32 nearClip, r32 farClip) {
		glm::mat4 transMat = GetTransformMatrix(transform);
		cameraData.view = glm::inverse(transMat);
		r32 aspect = vulkan.GetSurfaceAspect();
		cameraData.proj = glm::perspective(glm::radians(fov), aspect, nearClip, farClip);
		cameraData.pos = transform.position;
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

	s16 Renderer::DrawMesh(MeshHandle mesh, MaterialHandle material, const Transform& transform) {
		u16 transformIndex = nextTransformIndex++;
		u16 dataIndex = nextDataIndex++;

		DrawcallData data = { 1, transformIndex };

		drawcallData[dataIndex] = data;
		
		instanceData[transformIndex] = { GetTransformMatrix(transform) };

		Drawcall call = 0;
		call += (dataIndex % 0x1000);
		call += (u64)mesh << 12;
		call += (u64)material << 20;
		// call += (u64)shaders[materials[material].shader].layer << 56;

		renderQueue[queueLength++] = call;

		return dataIndex;
	}

	void Renderer::Render() {
		// Sort drawcalls
		auto comp = [](const Drawcall& a, const Drawcall& b)
		{
			return a < b;
		};
		std::sort(&renderQueue[0], &renderQueue[queueLength], comp);

		vulkan.SetInstanceData(instanceData, queueLength);
		vulkan.SetCameraData(cameraData);
		vulkan.SetLightingData(lightingData);

		vulkan.BeginRenderCommands();
		vulkan.DoFinalBlit();
		vulkan.EndRenderCommands();

		// Clear render queue
		queueLength = 0;
		nextDataIndex = 0;
		nextTransformIndex = 0;
	}

	void Renderer::ResizeSurface() {
		DEBUG_LOG("Resize");
		vulkan.RecreateSwapchain();
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
}