#pragma once
#include "vulkan.h"

namespace Rendering {
	class Renderer {
	public:
		Renderer(HINSTANCE hInst, HWND hWindow);
		~Renderer();

		MeshHandle CreateMesh(const MeshData& data);

		void UpdateCamera(const Transform& transform, r32 fov = 35.0f, r32 nearClip = 0.01f, r32 farClip = 100.0f);
		void UpdateMainLight(const Transform& transform, const Color& color);
		void UpdateAmbientLight(const Color& color);
		s16 DrawMesh(MeshHandle mesh, MaterialHandle material, const Transform& transform);

		void Render();
		void ResizeSurface();
	private:

		glm::mat4x4 GetTransformMatrix(const Transform& transform) const;

		CameraData cameraData;
		LightingData lightingData;
		PerInstanceData *instanceData;

		struct DrawcallData {
			u16 instanceCount;
			u16 transformIndex;
		} *drawcallData;

		typedef u64 Drawcall;
		Drawcall* renderQueue;
		u16 queueLength;

		u16 nextDataIndex;
		u16 nextTransformIndex;

		Vulkan vulkan;
	};
}