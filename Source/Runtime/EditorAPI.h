#pragma once

/**
 * @file EditorAPI.h
 * @brief App ↔ EditorModule 통신용 함수 테이블 (IEditor 구현 세부사항 은닉)
 */

#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Runtime/RuntimeHandles.h"

namespace sw
{
	using EditorHandle	= void*;
	using TextureHandle = uint64;

	struct EditorUIContext;
	struct NativeWindowEvent;

	/** @brief EditorModule C ABI 함수 테이블 */
	struct EditorAPI
	{
		EditorHandle ( *create )()																	= nullptr; ///< 에디터 인스턴스 생성
		void ( *destroy )( EditorHandle editor )													= nullptr; ///< 에디터 인스턴스 파괴
		bool ( *initialize )( EditorHandle editor, WindowHandle window, RHIDeviceHandle rhiDevice ) = nullptr; ///< 윈도우·RHI로 초기화
		void ( *shutdown )( EditorHandle editor )													= nullptr; ///< 종료
		void ( *preRender )( EditorHandle editor, RHIDeviceHandle rhiDevice )						= nullptr; ///< 렌더 직전
		void ( *render )( EditorHandle editor, const EditorUIContext* context )						= nullptr; ///< UI 렌더
		void ( *postPresent )( EditorHandle editor, RHIDeviceHandle rhiDevice )						= nullptr; ///< Present 이후(멀티 뷰포트)
		bool ( *processEvent )( EditorHandle editor, const NativeWindowEvent* event )				= nullptr; ///< 네이티브 이벤트 전달
		void* ( *registerTexture )( EditorHandle editor, TextureHandle texture )					= nullptr; ///< ImGui 텍스처 등록
	};

	/** @brief EditorModule이 export하는 API 테이블 채우기 심볼 이름: fillEditorAPI */
	using PFN_FillEditorAPI = bool ( * )( EditorAPI* outApi );
} // namespace sw

extern "C"
{
	SW_MODULE_API bool fillEditorAPI( sw::EditorAPI* outApi );
}
