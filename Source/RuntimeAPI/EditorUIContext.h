/**
 * @file EditorUIContext.h
 * @brief App ↔ EditorModule 간 UI 렌더링에 필요한 런타임 컨텍스트
 *
 * @note Dev 전용 · 타입이 있는 동일 프로세스 컨텍스트 — C ABI / POD 동결 대상이 아님.
 *       Material*, IRHIDevice*, ShaderReflectionData* 등은 App이 소유한 Engine 그래픽/RHI
 *       객체이며, EditorModule은 같은 주소 공간의 MODULE으로서 이 타입을 통해 호출할 수 있다.
 *       프로세스 간 직렬화하거나 안정 ABI로 취급하지 말 것.
 *       create/destroy용 opaque 핸들은 ABI/RuntimeHandles.h에 둔다.
 *
 * @note 데모/게임플레이 트윅(playerSpeed 등)은 이 컨텍스트에 두지 않는다.
 *       GlobalVariableManager / 게임 채널을 사용한다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
	class IRHIDevice;
	class Material;
	struct ShaderReflectionData;

	// ------------------------------------------------------------------------------
	// 1) EditorUIContext — App 소유 객체의 동일 프로세스 포인터
	//    C ABI가 아님. 프로세스 간 직렬화 금지
	// ------------------------------------------------------------------------------
	/** @brief App ↔ EditorModule 간 UI 렌더링에 필요한 런타임 컨텍스트 */
	struct EditorUIContext
	{
		void*			_pMaterial{ nullptr };		 /**< App 소유 Material* */
		void*			_pReflectionData{ nullptr }; /**< App 소유 ShaderReflectionData* */
		void*			_pRHIDevice{ nullptr };		 /**< App 소유 IRHIDevice* */
		uint64			_gameRenderTarget{ 0 };
		void*			_pGameTextureID{ nullptr };						/**< registerTexture가 돌려준 ImGui 텍스처 ID */
		mutable float32 _arrClearColor[4]{ 0.12f, 0.15f, 0.18f, 1.0f }; /**< App이 소유한 클리어 컬러 (에디터 표시 전용) */

		// ------------------------------------------------------------------------------
		// 2) Game View RT — 현재 크기(App→패널) · 리사이즈 요청(패널→App)
		// ------------------------------------------------------------------------------
		uint32 _gameViewportWidth{ 0 };	 /**< 현재 Game View RT 너비 */
		uint32 _gameViewportHeight{ 0 }; /**< 현재 Game View RT 높이 */

		mutable uint32 _requestGameViewportWidth{ 0 };	/**< 패널이 원하는 콘텐츠 영역 너비. App이 디바운스 후 RT 재생성 */
		mutable uint32 _requestGameViewportHeight{ 0 }; /**< 패널이 원하는 콘텐츠 영역 높이. 대기 요청 해제는 0 */

		// ------------------------------------------------------------------------------
		// 3) 입력 라우팅 상태 (ImGui Input Leak 방지)
		// ------------------------------------------------------------------------------
		mutable bool _bIsGameViewHovered{ false }; /**< GameView 창 위 마우스 호버 여부 */
		mutable bool _bIsGameViewFocused{ false }; /**< GameView 창 포커스 여부 */
	};
} // namespace sw
