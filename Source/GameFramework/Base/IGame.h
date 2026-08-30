/**
 * @file IGame.h
 * @brief 게임 로직 모듈의 런타임 추상 인터페이스
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	class IRHIDevice;
	class IWindow;

	// ------------------------------------------------------------------------------
	// 1) IGame — SWGame MODULE이 구현하는 수명주기
	//    App이 GameAPI로 생성하고 initialize / update / shutdown을 호출
	// ------------------------------------------------------------------------------
	/** @brief SWGame 모듈이 구현하는 게임 로직 인터페이스 */
	class SW_GF_API IGame
	{
	public:
		/** @brief 상태는 파생 클래스가 가집니다. */
		IGame() = default;
		/** @brief 파생 모듈이 리소스를 해제할 수 있게 합니다. */
		virtual ~IGame() = default;

		/** @brief 복사를 금지합니다. */
		IGame( const IGame& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		IGame& operator=( const IGame& ) = delete;

		/** @brief 윈도우·RHI로 게임 상태를 초기화합니다. */
		virtual bool initialize( IWindow* pWindow, IRHIDevice* pRhiDevice ) = 0;
		/** @brief 게임 리소스를 해제합니다. */
		virtual void shutdown() = 0;
		/** @brief 한 프레임 게임 로직을 갱신합니다. */
		virtual void update( float32 deltaTime ) = 0;
		/** @brief 물리 등 고정 주기 게임 로직을 갱신합니다. */
		virtual void fixedUpdate( float32 /*fixedDeltaTime*/ ) {}

		/**
		 * @brief 게임의 상태를 버퍼에 직렬화하거나 필요한 버퍼 크기를 계산합니다.
		 * @param pOutBuffer 상태를 저장할 버퍼 (nullptr이면 inOutSize에 크기만 반환)
		 * @param pInOutSize 버퍼의 크기 (입력/출력)
		 * @return 지원하면 true
		 */
		virtual bool serializeState( void* pOutBuffer, uint32* pInOutSize )
		{
			(void)pOutBuffer;
			(void)pInOutSize;
			return false;
		}

		/**
		 * @brief 게임의 상태를 버퍼로부터 복원(역직렬화)합니다.
		 */
		virtual bool deserializeState( const void* pInBuffer, uint32 size )
		{
			(void)pInBuffer;
			(void)size;
			return false;
		}
	};
} // namespace sw
