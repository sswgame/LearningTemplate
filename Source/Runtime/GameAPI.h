#pragma once

/**
 * @file GameAPI.h
 * @brief App ↔ SWGame 통신용 함수 테이블
 */

#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Runtime/RuntimeHandles.h"

namespace sw
{
	using GameHandle = void*;

	/** @brief SWGame C ABI 함수 테이블 */
	struct GameAPI
	{
		GameHandle ( *create )()																= nullptr; ///< 게임 인스턴스 생성
		void ( *destroy )( GameHandle game )													= nullptr; ///< 게임 인스턴스 파괴
		bool ( *initialize )( GameHandle game, WindowHandle window, RHIDeviceHandle rhiDevice ) = nullptr; ///< 윈도우·RHI로 초기화
		void ( *shutdown )( GameHandle game )													= nullptr; ///< 종료
		void ( *update )( GameHandle game, float32 deltaTime )									= nullptr; ///< 프레임 업데이트
	};

	/** @brief SWGame export 심볼: fillGameAPI */
	using PFN_FillGameAPI = bool ( * )( GameAPI* outApi );
} // namespace sw

extern "C"
{
	SW_MODULE_API bool fillGameAPI( sw::GameAPI* outApi );
}
