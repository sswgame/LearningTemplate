/**
 * @file GameTestUtil.h
 * @brief GameFramework 단위 테스트 격리를 위한 테스트 유틸리티.
 */
#pragma once
#include "GameFramework/Framework/GameService.h"

namespace sw::test
{
    /** @brief 단위 테스트 격리를 위한 GameService RAII 바인딩 가드 */
    class ScopedGameServiceBinding
    {
    public:
        explicit ScopedGameServiceBinding( const ModuleService& service )
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
    /** @brief 게임 로컬 서비스 하나(`game::bindLocalService<T>`)를 스코프 동안 묶는 RAII 가드입니다 — 어서션이 빠져나가도 풀립니다. */
    template <typename T>
    class ScopedLocalServiceBinding
    {
    public:
        explicit ScopedLocalServiceBinding( T& service ) { game::bindLocalService<T>( &service ); }
        ~ScopedLocalServiceBinding() { game::unbindLocalService<T>(); }

        ScopedLocalServiceBinding( const ScopedLocalServiceBinding& )            = delete;
        ScopedLocalServiceBinding& operator=( const ScopedLocalServiceBinding& ) = delete;
        ScopedLocalServiceBinding( ScopedLocalServiceBinding&& )                 = delete;
        ScopedLocalServiceBinding& operator=( ScopedLocalServiceBinding&& )      = delete;
    };
} // namespace sw::test
