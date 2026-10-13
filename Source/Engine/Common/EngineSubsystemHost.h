/**
 * @file Engine/Common/EngineSubsystemHost.h
 * @brief 엔진의 호스트 종류 비트입니다 — 서브시스템이 선언하는 `SubsystemHostMask` 의 뜻을 엔진이 정한다(Core 는 비트만 안다).
 * @details 언리얼 모듈의 호스트 종류(`Runtime` · `ServerOnly` · `ClientOnly`)와 서브시스템의 `ShouldCreateSubsystem` 처럼, 서브시스템이 자기가 올라갈 호스트를
 *          말하고 목록(`SubsystemCollection::initializeAll`)이 호스트 종류로 거른다. 어디서나 도는 것은 `SubsystemHostMask::All` 이다.
 */
#pragma once
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /** @brief 엔진 호스트 종류의 비트입니다. 종류를 늘리면 비트 하나를 더하고, 그 호스트에서 돌 서브시스템 선언만 고친다. */
    struct EngineSubsystemHost
    {
        /** @brief 창 · 렌더러 · 플레이어 설정이 있는 호스트(App · 에디터 · 테스트 하네스)입니다. */
        static constexpr SubsystemHostMask kClient = static_cast<SubsystemHostMask>( 1 << 0 );
        /** @brief 전용 서버(Server 실행 파일)입니다. */
        static constexpr SubsystemHostMask kServer = static_cast<SubsystemHostMask>( 1 << 1 );
    };
} // namespace sw
