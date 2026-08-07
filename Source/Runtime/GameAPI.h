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

	struct GameAPI
	{
		GameHandle ( *create )()																= nullptr;
		void ( *destroy )( GameHandle game )													= nullptr;
		bool ( *initialize )( GameHandle game, WindowHandle window, RHIDeviceHandle rhiDevice ) = nullptr;
		void ( *shutdown )( GameHandle game )													= nullptr;
		void ( *update )( GameHandle game, float32 deltaTime )									= nullptr;
	};

	/** @brief SWGame export 심볼: fillGameAPI */
	using PFN_FillGameAPI = bool ( * )( GameAPI* outApi );
}

extern "C"
{
	SW_MODULE_API bool fillGameAPI( sw::GameAPI* outApi );
}
