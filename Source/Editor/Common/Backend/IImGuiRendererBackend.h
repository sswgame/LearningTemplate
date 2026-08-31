/**
 * @file IImGuiRendererBackend.h
 * @brief ImGui GPU 렌더러 백엔드 추상 인터페이스 (RHI별 구현)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	class IRHIDevice;
	enum class RHIBackend : uint32;
} // namespace sw

struct ImDrawData;
struct ImTextureData;

namespace sw::editor
{
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
		/** @brief 지정 DrawData를 RHI로 그립니다. nullptr이면 그리지 않습니다. */
		virtual void render( IRHIDevice* pRhiDevice, ImDrawData* pDrawData ) = 0;
		/**
		 * @brief 대기 중인 ImGui 텍스처 생성/갱신(폰트 아틀라스 재빌드 등)을 즉시 처리합니다.
		 * @details ImGui 1.92 동적 아틀라스에서 draw-data 스냅샷을 다른 스레드로 넘길 때, 텍스처 갱신은
		 *          렌더 스레드가 아니라 UI 스레드에서 이 호출로 끝내야 합니다(공유 리스트 레이스 방지).
		 */
		virtual void processTextureUpdates() {}

		/**
		 * @brief 프레임 GPU 작업(newFrame·텍스처 갱신·보조 뷰포트 렌더)을 렌더 스레드에서 해야 하면 true.
		 * @details OpenGL 처럼 GPU 컨텍스트가 스레드 전용(wglMakeCurrent)인 백엔드는 UI 스레드에서
		 *          컨텍스트를 잡을 수 없다. 이 값이 true 면 ImGuiEditor 가 해당 호출들을 present 훅
		 *          (렌더 스레드)으로 옮겨 실행한다. DX12/DX11/Vulkan 은 false.
		 */
		virtual bool requiresRenderThreadContext() const { return false; }

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

	protected:
		/**
		 * @brief 갱신 대기 중인 ImGui 텍스처를 백엔드 UpdateTexture 로 모두 처리합니다.
		 * @param pUpdateTexture ImGui_Impl*_UpdateTexture 함수 포인터.
		 * @details 백엔드 4종이 이 루프를 똑같이 반복하므로 여기 모읍니다.
		 *          ImGui_Impl*_RenderDrawData 가 draw_data->Textures 를 순회하며 하던 일인데,
		 *          draw-data 스냅샷은 그 리스트를 공유하지 않으므로 그리기 전에 끝내야 합니다.
		 *          호출 스레드는 requiresRenderThreadContext() 에 따라 UI/렌더 스레드로 갈립니다.
		 */
		static void updatePendingTextures( void ( *pUpdateTexture )( ImTextureData* ) );
	};
} // namespace sw::editor
