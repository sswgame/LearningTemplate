/**
 * @file IEditor.h
 * @brief App↔Editor Runtime API에 대응하는 에디터 코어 인터페이스
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
	class IRHIDevice;
	class IWindow;
	struct EditorUIContext;
	struct NativeWindowEvent;

	using RHITextureHandle = uint64;

	/**
	 * @class IEditor
	 * @brief EditorAPI 함수 테이블이 위임하는 최소 표면 (위젯/도킹은 ImGuiEditor 내부)
	 */
	class IEditor
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 생명주기 — 생성은 App, 해제는 파생 구현
		// ------------------------------------------------------------------------------
		/** @brief 파생 에디터가 리소스를 해제할 수 있게 합니다. */
		virtual ~IEditor() = default;

		/** @brief 플랫폼 백엔드·렌더러·폰트를 초기화합니다. */
		virtual bool initialize( IWindow* pWindow, IRHIDevice* pRhiDevice ) = 0;
		/** @brief 에디터 리소스를 해제합니다. */
		virtual void shutdown() = 0;

		// ------------------------------------------------------------------------------
		// 2) 프레임 — preRender → render → postPresent
		// ------------------------------------------------------------------------------
		/** @brief UI 그리기 전 패널 GPU 작업을 수행합니다. */
		virtual void preRender( IRHIDevice* pRhiDevice ) = 0;
		/** @brief ImGui 프레임과 패널을 그립니다. */
		virtual void render( const EditorUIContext& context ) = 0;
		/** @brief 메인 스왑체인 Present 이후 호출 (멀티 뷰포트 보조 윈도우 렌더) */
		virtual void postPresent( IRHIDevice* pRhiDevice ) = 0;

		// ------------------------------------------------------------------------------
		// 3) 입력 · ImGui 텍스처
		// ------------------------------------------------------------------------------
		/** @brief 네이티브 이벤트를 ImGui 플랫폼 레이어로 전달합니다. */
		virtual bool processEvent( const NativeWindowEvent& event, const EditorUIContext* pContext ) = 0;
		/** @brief RHI 텍스처를 ImGui 텍스처 ID로 등록합니다. */
		virtual void* registerTexture( RHITextureHandle texture ) = 0;
		/** @brief 등록된 ImGui 텍스처 ID를 해제합니다. */
		virtual void unregisterTexture( void* pTextureID ) = 0;
	};
} // namespace sw
