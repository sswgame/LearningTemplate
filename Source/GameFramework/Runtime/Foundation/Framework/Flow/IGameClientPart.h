/**
 * @file GameFramework/Runtime/Foundation/Framework/Flow/IGameClientPart.h
 * @brief 게임 하나의 클라이언트 전용 부분(그리는 벤치 · 렌더 확장 시연 같은 것)을 게임 인스턴스에 붙이는 자리입니다 — 공유 게임 코드는 그 부분의 형식을 모릅니다.
 * @details 게임의 `Client/` 소스가 공장(`IGameClientPartFactory`)을 모듈 서브시스템으로 공개하고, 게임 인스턴스가 기동에 하나 만들어 듭니다(인스턴스와 수명이 같다 —
 *          모듈을 다시 올리면 새 인스턴스가 새로 만든다). 전용 서버는 그 소스를 짓지 않아 공장이 없고, 게임은 그 부분 없이 선다.
 *          예전에는 게임 파일 안의 `#if SW_WITH_CLIENT_CODE` 구역이었다. 언리얼 게임 모듈의 클라이언트 전용 서브모듈(`Type = ClientOnly`)과 같은 갈림이다(기억).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @class IGameClientPart @brief 게임 인스턴스가 드는 클라이언트 전용 부분입니다. 게임 스레드에서 부릅니다. */
    class IGameClientPart
    {
    public:
        virtual ~IGameClientPart() = default;

        /** @brief 게임 초기화(`onInitialize`)에서 한 번 부릅니다. 이 부분이 첫 씬 대신 자기 장면을 띄웠으면 true 입니다(게임은 첫 씬을 요청하지 않는다). */
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
} // namespace sw
