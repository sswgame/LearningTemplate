/**
 * @file ModuleAPI/Service/IServerKitLocator.h
 * @brief 전용 서버의 게임 모듈이 서버 키트 인터페이스(백필 · 접속 받아들이기 …)를 찾는 호스트 칸입니다.
 * @details - 키트 인터페이스는 온라인 서버 조립(`OnlineServerRuntime`)의 지역 범위에 걸린다(`IOnlineServerKit::exposeInterface`). 조립은 서버 서비스라 모듈 리로드 · 종료마다
 *            멈췄다 다시 선다 — 게임 범위에 그 객체를 직접 걸면 매달린다. 그래서 호스트(Server)가 이 칸을 소유하고, 조립이 설 때 범위를 공개 · 내릴 때 거둔다.
 *          - 게임 모듈은 `findSubsystem<IServerKitLocator>()` 로 찾고 부를 때마다 `findServerKit<T>` 로 찾는다(들고 있지 않는다). App(클라이언트)에는 없다.
 *          `IModuleStateStore` 와 같은 자리다. 언리얼의 `IOnlineSubsystem::Get()` 을 서버 모듈에서 부르는 것과 같은 쓰임이다.
 */
#pragma once
#include "Core/Subsystem/SubsystemScope.h"

namespace sw
{
    /** @brief 서버 키트 범위의 창구입니다. 메인 스레드(서버 루프)에서만 씁니다. */
    class IServerKitLocator
    {
    public:
        virtual ~IServerKitLocator() = default;

        /** @brief 지금 서 있는 서버 키트 조립의 인터페이스 범위입니다. 조립이 서 있지 않으면(리로드 · 종료 중, 온라인 설정 없음) nullptr 입니다. */
        virtual const ISubsystemLocator* findKitScope() const = 0;
    };

    /** @brief @p pLocator 가 공개한 키트 범위에서 @p T 를 찾습니다. 창구나 범위가 없으면 nullptr 입니다. 받은 포인터는 이번 호출에서만 씁니다. */
    template <typename T>
    T* findServerKit( const IServerKitLocator* pLocator )
    {
        const ISubsystemLocator* const pScope = pLocator != nullptr ? pLocator->findKitScope() : nullptr;
        return pScope != nullptr ? findInLocator<T>( *pScope ) : nullptr;
    }
} // namespace sw
