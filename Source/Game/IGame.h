#pragma once

/**
 * @file IGame.h
 * @brief 게임 로직 모듈의 런타임 추상 인터페이스
 */

#include "Core/Common/Common.h"

namespace sw
{
	class IRHIDevice;
	class IWindow;

	class IGame
	{
	public:
		IGame()			 = default;
		virtual ~IGame() = default;

		IGame( const IGame& )			 = delete;
		IGame& operator=( const IGame& ) = delete;

		virtual bool initialize( IWindow* window, IRHIDevice* rhiDevice ) = 0;
		virtual void shutdown()											  = 0;
		virtual void update( float32 deltaTime )						  = 0;
	};
}
