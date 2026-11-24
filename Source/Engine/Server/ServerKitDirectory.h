/**
 * @file ServerKitDirectory.h
 * @brief 호스트(Server)가 소유하는 서버 키트 창구입니다. 서버 서비스가 세운 키트 범위를 공개하고 거둡니다.
 * @details - 수명은 호스트 것이라 서버 서비스가 모듈 리로드 · `stopAll` 로 내려가도 게임 범위에 건 창구가 매달리지 않는다 — 내려간 동안은 범위가 비어(nullptr) 찾기가 없음을 돌려준다.
 *          - 거두기 전에 알린다(`subscribeBeforeWithdraw`) — 게임 서버 코드가 범위가 아직 있을 때 마지막 저장을 맡긴다. 저장소는 그 뒤 맡은 일을 끝내고 멈춘다.
 *          - 게임 모듈은 이 형식을 모른다(`Server/` 폴더는 클라이언트 구성이 짓지 않는다). 호스트가 `IServerKitLocator`(ModuleAPI)로 감싸 게임 모듈 범위에 걸고, 게임 모듈은 형식 목록 묶음(`ServerKitDependencies`)으로만 받는다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/SubsystemScope.h"

namespace sw
{
    /**
     * @class ServerKitDirectory
     * @brief 키트 범위 하나를 빌려 공개합니다. 메인 스레드(서버 루프)에서만 씁니다.
     */
    class ServerKitDirectory
    {
    public:
        /** @brief 서버 서비스가 선 뒤 키트 범위를 공개합니다. 범위는 `withdraw` 까지 살아야 합니다. 이미 다른 범위가 있으면 조립 실수라 멈춥니다. */
        void publish( const ISubsystemLocator* pScope )
        {
            SW_ASSERT( _pScope == nullptr || _pScope == pScope ); // 온라인 서버 조립은 서버 하나에 하나다
            _pScope = pScope;
        }

        /** @brief 서버 서비스가 내려가기 전에 알리고(`subscribeBeforeWithdraw`) 거둡니다. 공개한 범위가 아니면 아무것도 하지 않습니다. */
        void withdraw( const ISubsystemLocator* pScope )
        {
            if ( _pScope != pScope || pScope == nullptr )
                return;
            _onBeforeWithdraw.broadcast();
            _pScope = nullptr;
        }
        /** @brief 공개한 범위를 거두기 직전의 알림입니다 — 그 안에서는 범위를 아직 찾을 수 있습니다. */
        [[nodiscard]] Subscription subscribeBeforeWithdraw( const Delegate<void()>& onBeforeWithdraw ) { return _onBeforeWithdraw.add( onBeforeWithdraw ); }

        /** @brief 지금 공개된 키트 범위입니다. 없으면 nullptr 입니다. */
        const ISubsystemLocator* findKitScope() const { return _pScope; }

    private:
        const ISubsystemLocator*  _pScope{ nullptr };
        MulticastDelegate<void()> _onBeforeWithdraw{};
    };
} // namespace sw
