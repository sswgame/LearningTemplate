/**
 * @file IImGuiRendererBackend.h
 * @brief ImGui GPU 렌더러 백엔드 추상 인터페이스 (RHI별 구현)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	class IRHIDevice;
	enum class RHIBackend : uint32;
	using RHITextureHandle = uint64;

	/** @brief ImGui GPU 렌더러 백엔드 (DX11 / DX12 / Vulkan / OpenGL) */
	class IImGuiRendererBackend
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 생명주기 · 프레임
		// ------------------------------------------------------------------------------
		/** @brief 파생 렌더러가 GPU 리소스를 해제할 수 있게 합니다. */
		virtual ~IImGuiRendererBackend() = default;

		/** @brief RHI 디바이스에 ImGui 렌더러를 바인딩하고 초기화합니다. */
		virtual bool initialize( IRHIDevice* pRhiDevice ) = 0;
		/** @brief ImGui 렌더러 리소스를 해제합니다. */
		virtual void shutdown() = 0;
		/** @brief 프레임 시작 시 ImGui 렌더러 상태를 갱신합니다. */
		virtual void newFrame() = 0;
		/** @brief 현재 ImGui draw data를 RHI로 그립니다. */
		virtual void render( IRHIDevice* pRhiDevice ) = 0;

		// ------------------------------------------------------------------------------
		// 2) ImGui 텍스처 — Game View RT 등
		// ------------------------------------------------------------------------------
		/** @brief RHI 텍스처를 ImGui 텍스처 ID로 등록하고 핸들을 반환합니다. */
		virtual void* registerTexture( RHITextureHandle texture ) = 0;
		/** @brief registerTexture로 발급한 ImGui 텍스처 ID를 해제합니다. */
		virtual void unregisterTexture( void* pTextureID ) = 0;

		// ------------------------------------------------------------------------------
		// 3) 팩토리 — RHI 백엔드별 구현
		// ------------------------------------------------------------------------------
		/** @brief 지정 RHI 백엔드에 맞는 렌더러 구현을 생성합니다. */
		static unique_ptr<IImGuiRendererBackend> createRendererBackend( RHIBackend backend );
	};
} // namespace sw
