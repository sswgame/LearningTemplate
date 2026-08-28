/**
 * @file EditorAPI.h
 * @brief App ↔ EditorModule 통신용 함수 테이블 (IEditor 구현 세부사항 은닉)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "RuntimeAPI/ABI/RuntimeHandles.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 핸들
	// ------------------------------------------------------------------------------
	/** @brief 에디터 인스턴스를 가리키는 불투명(opaque) 핸들 */
	using EditorHandle = void*;
	/** @brief 텍스처를 가리키는 핸들 */
	using TextureHandle = uint64;

	struct NativeWindowEvent;
	struct ModuleService;

	// ------------------------------------------------------------------------------
	// 2) EditorAPI — C ABI 함수 테이블
	//    IEditor 구현은 EditorModule 안에 두고, App은 이 포인터만 호출
	// ------------------------------------------------------------------------------
	/** @brief App이 채우고 EditorModule이 구현하는 함수 포인터 테이블 */
	struct EditorAPI
	{
		EditorHandle ( *create )(){ nullptr };																				 /**< @brief 에디터 인스턴스를 생성합니다. */
		void ( *destroy )( EditorHandle editor ){ nullptr };																 /**< @brief 에디터 인스턴스를 파괴합니다. */
		bool ( *initialize )( EditorHandle editor, WindowHandle window, RHIDeviceHandle rhiDevice ){ nullptr };				 /**< @brief 윈도우 및 RHI 디바이스로 에디터를 초기화합니다. */
		void ( *shutdown )( EditorHandle editor ){ nullptr };																 /**< @brief 에디터를 종료합니다. */
		void ( *updateUI )( EditorHandle editor ){ nullptr };																 /**< @brief 메인 스레드에서 에디터 UI 및 플랫폼 윈도우를 갱신합니다. */
		void ( *preRender )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };									 /**< @brief 렌더링 직전에 호출됩니다. */
		void ( *render )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };										 /**< @brief GPU 상에 에디터 UI DrawData를 렌더링합니다. */
		void ( *postPresent )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };									 /**< @brief 렌더링 결과가 출력된 후 호출됩니다 (멀티 뷰포트 처리용). */
		bool ( *processEvent )( EditorHandle editor, const NativeWindowEvent* pEvent ){ nullptr };							 /**< @brief 네이티브 이벤트를 에디터로 전달합니다. */
		void* ( *registerTexture )(EditorHandle editor, TextureHandle texture){ nullptr };									 /**< @brief 텍스처를 ImGui에 등록합니다. */
		void ( *unregisterTexture )( EditorHandle editor, void* pTextureId ){ nullptr };									 /**< @brief 텍스처를 ImGui에서 해제합니다. */
		void ( *getGameViewport )( EditorHandle editor, uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ){ nullptr }; /**< @brief 이번 프레임 Game View RT 핸들과 크기를 조회합니다. */
		void ( *bindService )( const ModuleService* pService ){ nullptr };													 /**< @brief ModuleService를 에디터 모듈에 주입하거나 nullptr로 해제합니다. */
		bool ( *isPlaying )( EditorHandle editor ){ nullptr };																 /**< @brief 에디터 시뮬레이션(PIE)이 실행 중인지 반환합니다. */
		void ( *stopSimulation )( EditorHandle editor ){ nullptr };															 /**< @brief 에디터 시뮬레이션(PIE)을 정지합니다 (핫리로드 등). */
	};

	/** @brief EditorModule이 export하는 API 테이블 심볼 이름: exportEditorAPI */
	using PFN_ExportEditorAPI = bool ( * )( EditorAPI* pOutApi );

	class IWindow;
	class IRHIDevice;
} // namespace sw

extern "C"
{
	// ------------------------------------------------------------------------------
	// 3) export — EditorModule이 채우는 진입점
	// ------------------------------------------------------------------------------
	/** @brief EditorModule API 테이블을 내보냅니다. */
	SW_MODULE_API bool exportEditorAPI( sw::EditorAPI* pOutApi );
}
