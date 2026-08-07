#pragma once

/**
 * @file IGame.h
 * @brief 게임 로직 모듈의 런타임 추상 인터페이스
 */

#include "Core/Common/Types.h"

namespace sw
{
	class IRHIDevice;
	class IWindow;

	/** @brief SWGame 모듈이 구현하는 게임 로직 인터페이스 */
	class IGame
	{
	public:
		IGame()			 = default;
		virtual ~IGame() = default;

		IGame( const IGame& )			 = delete;
		IGame& operator=( const IGame& ) = delete;

		/** @brief 윈도우·RHI로 게임 상태를 초기화합니다. */
		virtual bool initialize( IWindow* window, IRHIDevice* rhiDevice ) = 0;
		/** @brief 게임 리소스를 해제합니다. */
		virtual void shutdown()											  = 0;
		/** @brief 프레임 업데이트를 수행합니다. */
		virtual void update( float32 deltaTime )						  = 0;
	};
}
