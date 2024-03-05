#pragma once
#include "vulkan.h"
#include <string>
#include <unordered_map>
#include <compare>

namespace Rendering {
	class Drawcall {
		u64 id;
	public:
		Drawcall();
		Drawcall(const Drawcall& other);
		Drawcall(u16 dataIndex, MeshHandle mesh, MaterialHandle mat, RenderLayer layer);

		std::strong_ordering operator<=>(const Drawcall& other) const;

		RenderLayer Layer() const;
		MaterialHandle Material() const;
		MeshHandle Mesh() const;
		u16 DataIndex() const;
	};

	class Renderer {
	public:
		Renderer(HINSTANCE hInst, HWND hWindow);
		~Renderer();

		MeshHandle CreateMesh(std::string name, const MeshCreateInfo& data);
		TextureHandle CreateTexture(std::string name, const TextureCreateInfo& info);
		ShaderHandle CreateShader(std::string name, const ShaderCreateInfo& info);
		MaterialHandle CreateMaterial(std::string name, const MaterialCreateInfo& info);

		void UpdateCamera(const Transform& transform, r32 fov = 35.0f, r32 nearClip = 0.01f, r32 farClip = 100.0f);
		void UpdateMainLight(const Transform& transform, const Color& color);
		void UpdateAmbientLight(const Color& color);
		void DrawMesh(MeshHandle mesh, MaterialHandle material, const Transform& transform);

		void Render();
		void ResizeSurface();
	private:

		glm::mat4x4 GetTransformMatrix(const Transform& transform) const;
		void RecalculateCameraMatrices();

		Camera mainCamera;
		LightingData lightingData;
		PerInstanceData *instanceData;

		struct DrawcallData {
			u16 instanceCount;
			u16 instanceOffset;
		} *drawcallData;

		Drawcall* renderQueue;
		u16 drawcallCount;
		u16 instanceCount;

		Vulkan vulkan;

		std::unordered_map<std::string, MeshHandle> meshNameMap;
		std::unordered_map<std::string, TextureHandle> textureNameMap;
		std::unordered_map<std::string, ShaderHandle> shaderNameMap;
		std::unordered_map<std::string, MaterialHandle> materialNameMap;

		std::unordered_map<ShaderHandle, ShaderMetadata> shaderMetadataMap;
		std::unordered_map<MaterialHandle, MaterialMetadata> materialMetadataMap;
	};
}