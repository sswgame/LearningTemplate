/**
 * @file Engine/Bootstrap/StaticStorageSubsystem.h
 * @brief 프로세스 정적 저장소가 기동 뒤 자란 몫을 돌려주고 엔진 수명 범위(`LifetimeScope`)를 닫습니다 — 부트스트랩 범위에서 가장 먼저 내려간다.
 * @details 종료에서 한다. 부트스트랩 범위의 종료는 그 위 범위(서비스 · 엔진)가 모두 소멸한 뒤라 컴포넌트 · 메시 · 지역화 서비스 · 모듈이 모두 내려간
 *          뒤다. 남기면 종료 보고(기준선 대비 태그 증가)에 남는다. 컴포넌트 칸이 남았으면 그 컴포넌트가 새는 것이라 놓지 않고 알린다(페이지를 놓으면
 *          그 컴포넌트가 내려간 메모리를 든다). 이름 풀 · 로거보다 먼저 내려가도록 그 뒤에 선다(리소스 루트 단계가 연 경로 캐시를 비운다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /** @class StaticStorageSubsystem @brief 정적 저장소 정리 서브시스템입니다. */
    class SW_API StaticStorageSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "StaticStorage";

        /** @brief 엔진 수명 범위를 엽니다 — 같은 프로세스에서 엔진을 다시 세우면 앞 엔진이 닫은 범위를 빈 채로 다시 연다. */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 정적 저장소를 돌려주고 엔진 수명 범위를 닫습니다. */
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
    };
} // namespace sw
