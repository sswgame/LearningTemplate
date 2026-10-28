/**
 * @file IGame.h
 * @brief 게임 로직 모듈의 런타임 추상 인터페이스입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IRHIDevice;
    class IWindow;

    // ------------------------------------------------------------------------------
    // 1) IGame — SWGame MODULE 이 구현하는 수명주기
    //    App(GameModuleManager)이 GameAPI 로 만들고 initialize / update / shutdown 을 부른다
    // ------------------------------------------------------------------------------
    /** @brief SWGame 모듈이 구현하는 게임 로직 인터페이스입니다. */
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

        /** @brief 윈도우 · RHI 로 게임 상태를 초기화합니다. */
        [[nodiscard]] virtual bool initialize( IWindow* pWindow, IRHIDevice* pRHIDevice ) = 0;
        /** @brief 게임 리소스를 해제합니다. */
        virtual void shutdown() = 0;
        /** @brief 한 프레임 게임 로직을 갱신합니다. */
        virtual void update( float32 deltaTime ) = 0;
        /** @brief 물리 등 고정 주기 게임 로직을 갱신합니다. */
        virtual void fixedUpdate( float32 /*fixedDeltaTime*/ ) {}

        /**
         * @brief 게임의 상태를 한 번 직렬화해 @p outBytes 에 담습니다. C-ABI 는 이 결과를 호스트 싱크(`StateBufferSink`)로 옮긴다.
         * @param outBytes 상태 바이트. 비어 있으면 저장할 상태가 없다는 뜻이다.
         * @return 지원하고 성공하면 true
         */
        [[nodiscard]] virtual bool serializeState( vector<uint8>& outBytes )
        {
            (void)outBytes;
            return false;
        }

        /**
         * @brief 게임의 상태를 버퍼로부터 복원(역직렬화)합니다.
         */
        [[nodiscard]] virtual bool deserializeState( const void* pInBuffer, uint32 size )
        {
            (void)pInBuffer;
            (void)size;
            return false;
        }
    };
} // namespace sw
