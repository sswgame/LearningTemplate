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
	// 1) 핸들 · ABI 버전
	//    호스트가 _abiVersion/structSize를 채운 뒤 fillEditorAPI가 포인터를 넣음
	// ------------------------------------------------------------------------------
	/** @brief 에디터 인스턴스를 가리키는 불투명(opaque) 핸들 */
	using EditorHandle = void*;
	/** @brief 텍스처를 가리키는 핸들 */
	using TextureHandle = uint64;

	struct EditorUIContext;
	struct NativeWindowEvent;
	struct ModuleService;

	/** @brief EditorAPI 테이블 ABI 버전 */
	// Bump this whenever the EditorAPI layout or a function signature changes.
	inline constexpr uint32 kEditorAPIAbiVersion = 1;

	// ------------------------------------------------------------------------------
	// 2) EditorAPI — C ABI 함수 테이블
	//    IEditor 구현은 EditorModule 안에 두고, App은 이 포인터만 호출
	// ------------------------------------------------------------------------------
	/** @brief App이 채우고 EditorModule이 구현하는 함수 포인터 테이블 */
	struct EditorAPI
	{
		uint32 _abiVersion{ 0 }; /**< @brief 호스트가 API를 채우기 전에 예상하는 버전을 설정합니다. */
		uint32 _structSize{ 0 }; /**< @brief 호스트가 API를 채우기 전에 EditorAPI 구조체의 크기를 설정합니다. */

		EditorHandle ( *create )(){ nullptr };																						/**< @brief 에디터 인스턴스를 생성합니다. */
		void ( *destroy )( EditorHandle editor ){ nullptr };																		/**< @brief 에디터 인스턴스를 파괴합니다. */
		bool ( *initialize )( EditorHandle editor, WindowHandle window, RHIDeviceHandle rhiDevice ){ nullptr };						/**< @brief 윈도우 및 RHI 디바이스로 에디터를 초기화합니다. */
		void ( *shutdown )( EditorHandle editor ){ nullptr };																		/**< @brief 에디터를 종료합니다. */
		void ( *updateUI )( EditorHandle editor, const EditorUIContext* pContext ){ nullptr };										/**< @brief 메인 스레드에서 에디터 UI 및 플랫폼 윈도우를 갱신합니다. */
		void ( *preRender )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };											/**< @brief 렌더링 직전에 호출됩니다. */
		void ( *render )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };												/**< @brief GPU 상에 에디터 UI DrawData를 렌더링합니다. */
		void ( *postPresent )( EditorHandle editor, RHIDeviceHandle rhiDevice ){ nullptr };											/**< @brief 렌더링 결과가 출력된 후 호출됩니다 (멀티 뷰포트 처리용). */
		bool ( *processEvent )( EditorHandle editor, const NativeWindowEvent* pEvent, const EditorUIContext* pContext ){ nullptr }; /**< @brief 네이티브 이벤트를 에디터로 전달합니다. */
		void* ( *registerTexture )(EditorHandle editor, TextureHandle texture){ nullptr };											/**< @brief 텍스처를 ImGui에 등록합니다. */
		void ( *unregisterTexture )( EditorHandle editor, void* pTextureId ){ nullptr };											/**< @brief 텍스처를 ImGui에서 해제합니다. */
		void ( *bindService )( const ModuleService* pService ){ nullptr };															/**< @brief ModuleService를 에디터 모듈에 주입하거나 nullptr로 해제합니다. */
	};

	/** @brief EditorModule이 export하는 API 테이블 채우기 심볼 이름: fillEditorAPI */
	using PFN_FillEditorAPI = bool ( * )( EditorAPI* pOutApi );

	class IWindow;
	class IRHIDevice;
} // namespace sw

extern "C"
{
	// ------------------------------------------------------------------------------
	// 3) export — EditorModule이 채우는 진입점
	// ------------------------------------------------------------------------------
	/** @brief EditorModule API 테이블을 채웁니다. */
	SW_MODULE_API bool fillEditorAPI( sw::EditorAPI* pOutApi );
}
