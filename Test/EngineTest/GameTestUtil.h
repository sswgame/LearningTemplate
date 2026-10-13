/**
 * @file GameTestUtil.h
 * @brief GameFramework 단위 테스트 격리를 위한 테스트 유틸리티.
 */
#pragma once
#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw::test
{
    /** @brief 단위 테스트 격리를 위한 GameService RAII 바인딩 가드 */
    class ScopedGameServiceBinding
    {
    public:
        explicit ScopedGameServiceBinding( const ModuleServiceTable& service )
        {
            game::bindGameService( service );
        }

        ~ScopedGameServiceBinding()
        {
            game::unbindGameService();
        }

        ScopedGameServiceBinding( const ScopedGameServiceBinding& )            = delete;
        ScopedGameServiceBinding& operator=( const ScopedGameServiceBinding& ) = delete;
        ScopedGameServiceBinding( ScopedGameServiceBinding&& )                 = delete;
        ScopedGameServiceBinding& operator=( ScopedGameServiceBinding&& )      = delete;
    };
} // namespace sw::test

namespace sw::test
{
    /**
     * @brief 서비스 하나를 스코프 동안 게임 범위에 거는 RAII 가드입니다 — 자기 범위를 만들어 지금 게임 범위로 걸고(앞 범위를 상위로 잇는다), 사라지면 되돌린다.
     * @details 어서션이 빠져나가도 풀린다. 여럿을 겹쳐 쓰면 범위가 이어져 모두 찾힌다.
     */
    template <typename T>
    class ScopedGameScopeService
    {
    public:
        explicit ScopedGameScopeService( T& service )
            : _scope{}
            , _activeScope{ _scope }
        {
            (void)_scope.bind( &service ); // 새 범위라 겹치지 않는다
        }

        ScopedGameScopeService( const ScopedGameScopeService& )            = delete;
        ScopedGameScopeService& operator=( const ScopedGameScopeService& ) = delete;
        ScopedGameScopeService( ScopedGameScopeService&& )                 = delete;
        ScopedGameScopeService& operator=( ScopedGameScopeService&& )      = delete;

    private:
        SubsystemScope        _scope;       ///< 이 서비스만 든 범위
        game::ScopedGameScope _activeScope; ///< 사는 동안 지금 게임 범위로 건다
    };
} // namespace sw::test
