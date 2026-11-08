/**
 * @file Engine/Module/ModuleLoadStageSubsystem.h
 * @brief 기동 순서의 ModuleLoadStage 자리입니다 — 호스트가 타입 공급자(GameFramework · 킷 · 게임 모듈)를 올려 등록을 끝냅니다. 씬을 읽는 일은 이 뒤다.
 * @details 호스트 범위의 모듈 라이브러리 창구(`IModuleLoader`)가 로더다. 창구가 없으면 올릴 것이 없다(배포 구성은 정적 링크라 Reflection 이 이미 다 모았다). 어느 쪽이든 끝나면 모든 타입이 등록된 것으로 적고
 *          (`TypeRegistry::markAllModuleTypesRegistered`), 그때부터 씬을 읽을 수 있다 — 헤드리스 쿠킹이 이것에 의존한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/SubsystemScope.h"

namespace sw
{
    /**
     * @class ModuleLoadStageSubsystem
     * @brief 타입 공급자 모듈을 올려 타입 등록을 끝내는 서브시스템입니다.
     */
    class SW_API ModuleLoadStageSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ModuleLoadStage";

        /** @brief @p pScope 는 이 서브시스템이 속한 범위입니다(찾는 시작점 — 부모로 올라가 호스트 범위의 `IModuleLoader` 를 찾는다). nullptr 이면 올릴 것이 없습니다. */
        explicit ModuleLoadStageSubsystem( const ISubsystemLocator* pScope = nullptr );

        SubsystemInitResult        initializeSubsystem() override;
        void                       shutdownSubsystem() override {}
        const utf8*                getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>    getSubsystemDependencies() const override;
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Reflection;

    private:
        const ISubsystemLocator* _pScope;
    };
} // namespace sw

/*
 * 기동 순서의 ModuleUnloadStage 자리입니다 — 종료 쪽에서 모듈 DLL 을 내리는 호스트 창구(`IModuleLoader::onScenesReleased`)를 부릅니다.
 * 씬은 방금 사라졌고 서비스는 아직 살아 있는 구간이 모듈 DLL 을 내리기에 알맞은 유일한 자리다(타입 · 코덱 · 캐시를 떼고 태스크를 기다린다).
 *          엔진은 거기서 무슨 일이 일어나는지 모른다 — 창구는 호스트가 부모인 호스트 범위에 건다. 호스트 범위가 없으면(테스트 하네스) 자리만 둔다.
 *          모듈 코드를 드는 낮은 층(비동기 IO 의 완료 콜백 · 오디오 · 입력 · 씬)은 이 이름을 모르므로, 이것이 그들을 역방향 의존으로 적어 그들이 먼저 내려가게 한다.
 */
namespace sw
{
    /**
     * @class ModuleUnloadStageSubsystem
     * @brief 종료에서 모듈 DLL 내리기 훅을 부르는 서브시스템입니다.
     */
    class SW_API ModuleUnloadStageSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ModuleUnloadStage";

        /** @brief @p pScope 는 이 서브시스템이 속한 범위입니다(찾는 시작점 — 부모로 올라가 호스트 범위의 `IModuleLoader` 를 찾는다). nullptr 이면 부를 것이 없습니다. */
        explicit ModuleUnloadStageSubsystem( const ISubsystemLocator* pScope = nullptr );

        SubsystemInitResult     initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        span<const utf8* const> getSubsystemDependents() const override;

    private:
        const ISubsystemLocator* _pScope;
    };
} // namespace sw
