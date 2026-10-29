/**
 * @file EngineStartupExtension.h
 * @brief 호스트가 엔진 기동 단계 범위에 서브시스템을 더하는 확장점입니다 — 엔진은 무엇이 더해지는지 모릅니다(개발 도구 `DevTools` 가 쓴다).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class SubsystemScope;

    namespace engine
    {
        struct StartupParts;
    } // namespace engine

    /**
     * @class IEngineStartupExtension
     * @brief 호스트 범위에 걸어 두면(`SubsystemScope::bind<IEngineStartupExtension>`) 엔진이 기동 단계 범위를 채운 뒤 · 초기화하기 전에 부릅니다.
     * @details 더한 서브시스템은 다른 기동 단계 서브시스템과 같이 선언한 의존 순서로 서고 엔진 종료 때 역순으로 내려간다.
     *          배포 빌드에 없는 기능(셰이더 · 설정 리로드)을 엔진 목록(`EngineSubsystemSet`)에 `#if` 로 두지 않고 그 기능의 단위가 여기로 붙는다
     *          (언리얼의 모듈이 엔진 서브시스템을 기여하는 자리와 같다). 하나만 건다 — 여럿이 필요해지면 목록으로 바꾼다.
     */
    class IEngineStartupExtension
    {
    public:
        /** @brief @p startup 에 서브시스템을 더합니다. @p parts 는 엔진이 방금 넣은 서브시스템(렌더 쪽은 렌더러가 없으면 nullptr)입니다. */
        virtual void addStartupSubsystems( SubsystemScope& startup, const engine::StartupParts& parts ) = 0;

    protected:
        ~IEngineStartupExtension() = default;
    };
} // namespace sw
