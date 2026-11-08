/**
 * @file GameFramework/Runtime/Foundation/Framework/Flow/IGameDevPart.h
 * @brief 게임 하나의 개발 전용 부분(움직임 기록 · 자동 플레이 조종 같은 진단 · 테스트 장치)을 게임 인스턴스에 붙이는 자리입니다 — 게임 코드는 그 부분의 형식을 모릅니다.
 * @details 게임의 `Dev/` 소스가 공장(`IGameDevPartFactory`)을 모듈 서브시스템으로 공개하고(`SW_MODULE_PART`), 바탕 `GameInstance` 가 파생의 `onInitialize` 뒤에 하나 만들어
 *          `update` 마다 파생의 `onUpdate` 뒤에 부르며 `onShutdown` 앞에 놓는다(모듈을 다시 올리면 새 인스턴스가 새로 만든다). 배포 구성은 `Dev/` 를 짓지 않아 공장이 없고,
 *          게임은 그 부분 없이 선다 — 런타임 파일이 테스트용 전역 변수를 직접 읽지 않는다. 클라이언트 전용 부분(`IGameClientPart`)과 같은 모양이고,
 *          언리얼 게임 모듈의 개발 전용 서브모듈(`Type = DeveloperTool` — 배포 타깃에 싣지 않는다)과 같은 갈림이다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
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
