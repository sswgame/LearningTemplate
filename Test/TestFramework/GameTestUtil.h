/**
 * @file GameTestUtil.h
 * @brief GameFramework 단위 테스트 격리를 위한 테스트 유틸리티.
 */
#pragma once
#include "GameFramework/Base/GameService.h"

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
