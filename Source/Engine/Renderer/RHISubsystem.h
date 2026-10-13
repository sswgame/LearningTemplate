/**
 * @file Engine/Renderer/RHISubsystem.h
 * @brief 기동 순서의 RHI 자리입니다 — 백엔드를 고르고 창(없으면)과 디바이스를 만듭니다. RHI 객체를 소유합니다.
 * @details 화면 값의 순서는 엔진 설정 → 플레이어가 고른 사용자 설정(기본값이 아닌 것) → 명령줄이고, 명령줄이 백엔드를 적지 않았을 때만 설정의 기본 백엔드가 이긴다.
 *          RenderDoc 은 그래픽 API 를 만들 때 끼어들므로 디바이스보다 먼저 찾는다. 백엔드 교체의 기준점이다 — 엔진 루프가 이것에 기대는 서브시스템만
 *          내렸다가(`SubsystemCollection::shutdownDependentsOf`) 디바이스를 바꾸고 다시 세운다. 헤드리스 작업이면 서지 않는데, 그 순서는 높은 층인
 *          `HeadlessSubsystem` 이 역방향으로 적는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Graphics/RHI/RHIInitResult.h"

namespace sw
{
    class RHI;

    /**
     * @class RHISubsystem
     * @brief 창 · 디바이스를 세우는 서브시스템입니다. 실패의 이유(`getInitResult`)는 호스트가 종료 코드를 고를 때 읽는다.
     */
    class SW_API RHISubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "RHI";

        /** @brief 디바이스 없는 RHI 객체를 만듭니다(창 · 디바이스는 `initializeSubsystem`). */
        RHISubsystem();
        ~RHISubsystem() override;

        RHISubsystem( const RHISubsystem& )            = delete;
        RHISubsystem& operator=( const RHISubsystem& ) = delete;

        SubsystemInitResult initializeSubsystem() override;
        /** @brief 디바이스를 기다린 뒤 내립니다. GPU 자원을 든 객체는 손으로 훑지 않는다 — 디바이스가 레지스트리 전체에 releaseRHI 를 부른다. */
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

        /** @brief 이 서브시스템이 선 뒤의 RHI 입니다(디바이스가 있다). 서지 않았으면(전용 서버 · 헤드리스 · 실패) nullptr 입니다. */
        RHI* findRHI() const { return _bReady ? _rhi.get() : nullptr; }
        /** @brief 마지막 초기화의 결과입니다. 돌지 않았으면 `NotStarted` 입니다. */
        RHIInitResult getInitResult() const { return _initResult; }

    private:
        unique_ptr<RHI> _rhi;
        RHIInitResult   _initResult;
        bool            _bReady;
    };
} // namespace sw
