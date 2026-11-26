/**
 * @file IGame.h
 * @brief 게임 로직 모듈의 런타임 추상 인터페이스와, 게임 인스턴스에 붙는 클라이언트 전용 · 개발 전용 부분(`IGameClientPart` · `IGameDevPart`)입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GenericWindow;

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
        [[nodiscard]] virtual bool initialize( GenericWindow* pWindow ) = 0;
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

    // ------------------------------------------------------------------------------
    // 2) IGameClientPart — 게임 하나의 클라이언트 전용 부분
    //    게임 하나의 클라이언트 전용 부분(그리는 벤치 · 렌더 확장 시연 같은 것)을 게임 인스턴스에 붙이는 자리입니다 — 공유 게임 코드는 그 부분의 형식을 모릅니다.
    //    게임의 `Client/` 소스가 공장(`IGameClientPartFactory`)을 모듈 서브시스템으로 공개하고, 게임 인스턴스가 기동에 하나 만들어 듭니다(인스턴스와 수명이 같다 —
    //    모듈을 다시 올리면 새 인스턴스가 새로 만든다). 전용 서버는 그 소스를 짓지 않아 공장이 없고, 게임은 그 부분 없이 선다.
    //    예전에는 게임 파일 안의 `#if SW_WITH_CLIENT_CODE` 구역이었다. 언리얼 게임 모듈의 클라이언트 전용 서브모듈(`Type = ClientOnly`)과 같은 갈림이다(기억).
    // ------------------------------------------------------------------------------
    /** @class IGameClientPart @brief 게임 인스턴스가 드는 클라이언트 전용 부분입니다. 게임 스레드에서 부릅니다. */
    class IGameClientPart
    {
    public:
        virtual ~IGameClientPart() = default;

        /** @brief 게임 초기화 끝(`GameInstance::startFirstScene`)에서 한 번 부릅니다. 이 부분이 첫 씬 대신 자기 장면을 띄웠으면 true 입니다(게임은 첫 씬을 요청하지 않는다). */
        [[nodiscard]] virtual bool initialize() = 0;
        /** @brief 게임 갱신(`onUpdate`)마다 부릅니다. */
        virtual void update( float32 deltaTime ) = 0;
        /** @brief 상태 스냅샷을 만들기 직전입니다(절차로 만든 오브젝트를 걷는다). */
        virtual void onBeforeStateSerialize() = 0;
        /** @brief 상태 복원이 씬을 바꾼 뒤입니다(걷은 것을 다시 만든다). */
        virtual void onAfterStateDeserialize() = 0;
    };

    /** @class IGameClientPartFactory @brief 게임의 클라이언트 전용 부분을 만드는 쪽입니다(게임 `Client/` 의 모듈 서브시스템). */
    class IGameClientPartFactory
    {
    public:
        virtual ~IGameClientPartFactory() = default;

        [[nodiscard]] virtual unique_ptr<IGameClientPart> createClientPart() = 0;
    };

    // ------------------------------------------------------------------------------
    // 3) IGameDevPart — 게임 하나의 개발 전용 부분
    //    게임 하나의 개발 전용 부분(움직임 기록 · 자동 플레이 조종 같은 진단 · 테스트 장치)을 게임 인스턴스에 붙이는 자리입니다 — 게임 코드는 그 부분의 형식을 모릅니다.
    //    게임의 `Dev/` 소스가 공장(`IGameDevPartFactory`)을 모듈 서브시스템으로 공개하고(`SW_MODULE_PART`), 바탕 `GameInstance` 가 파생의 `onInitialize` 뒤에 하나 만들어
    //    `update` 마다 파생의 `onUpdate` 뒤에 부르며 `onShutdown` 앞에 놓는다(모듈을 다시 올리면 새 인스턴스가 새로 만든다). 배포 구성은 `Dev/` 를 짓지 않아 공장이 없고,
    //    게임은 그 부분 없이 선다 — 런타임 파일이 테스트용 전역 변수를 직접 읽지 않는다. 클라이언트 전용 부분(`IGameClientPart`)과 같은 모양이고,
    //    언리얼 게임 모듈의 개발 전용 서브모듈(`Type = DeveloperTool` — 배포 타깃에 싣지 않는다)과 같은 갈림이다.
    // ------------------------------------------------------------------------------
    /** @class IGameDevPart @brief 게임 인스턴스가 드는 개발 전용 부분입니다. 게임 스레드에서 부릅니다. 소멸자가 남길 것(기록 파일)을 마무리합니다. */
    class IGameDevPart
    {
    public:
        virtual ~IGameDevPart() = default;

        /** @brief 게임 갱신(`GameInstance::update`)마다 파생의 `onUpdate` 뒤에 부릅니다. */
        virtual void update( float32 deltaTime ) = 0;
    };

    /** @class IGameDevPartFactory @brief 게임의 개발 전용 부분을 만드는 쪽입니다(게임 `Dev/` 의 모듈 서브시스템). */
    class IGameDevPartFactory
    {
    public:
        virtual ~IGameDevPartFactory() = default;

        [[nodiscard]] virtual unique_ptr<IGameDevPart> createDevPart() = 0;
    };
} // namespace sw
