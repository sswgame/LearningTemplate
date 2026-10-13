/**
 * @file GameService.h
 * @brief GameFramework 가 제공하고 게임 모듈 · 키트가 쓰는 서비스 창구입니다 — 게임 범위(`SubsystemScope`)에서 찾고, 없으면 호스트(엔진) 서비스 표를 봅니다.
 * @details 게임 범위는 게임 인스턴스(`GameInstanceBase`)가 소유하고 기동 때 "지금 범위" 로 건다(언리얼 `UGameInstanceSubsystem` 을 게임 인스턴스가 드는 자리).
 *          게임이 쓰는 카탈로그 · 설정은 그 범위에 걸린다(`GameInstanceBase::bindScopedService`). 범위는 인스턴스와 함께 사라지므로 게임 모듈이 내려갈 때
 *          남는 칸이 없다. 테스트는 자기 범위를 만들어 `ScopedGameScope` 로 잠시 "지금 범위" 로 건다.
 */
#pragma once
#include "Core/Subsystem/SubsystemScope.h"

#include "RuntimeAPI/Service/GameServiceBinding.h"

namespace sw
{
    namespace game
    {
        namespace internal
        {
            /** @brief App 이 넘긴 호스트 서비스 범위입니다(`bindGameService`). 없으면 nullptr 입니다. */
            SW_GAMESERVICE_API const ISubsystemLocator* findHostServices();
        } // namespace internal

        // bindGameService · unbindGameService 는 RuntimeAPI/Service/GameServiceBinding.h 가 선언한다(진입점 매크로가 RuntimeAPI 안에서 부른다).

        /** @brief 지금 게임 범위입니다. 게임 인스턴스가 서기 전이나 내려간 뒤에는 nullptr 입니다. */
        SW_GAMESERVICE_API SubsystemScope* findGameScope();
        /**
         * @brief 지금 게임 범위를 겁니다(nullptr 이면 풉니다). 게임 인스턴스가 기동 · 종료에서 부른다.
         * @details 핫 리로드에서는 새 인스턴스가 먼저 걸 수 있으므로, 푸는 쪽은 자기 범위가 걸려 있을 때만 푼다.
         */
        SW_GAMESERVICE_API void setGameScope( SubsystemScope* pScope );

        /**
         * @brief 게임 서비스를 찾습니다. **없으면 nullptr 입니다.** 받는 쪽이 확인해야 합니다.
         * @details 지금 게임 범위(그 상위 범위까지)에서 먼저 찾고, 없으면 호스트 서비스 표를 본다. 서비스가 없어도 `SW_ASSERT` 로 멈추지 않는다 —
         *          `SW_ASSERT` 는 Debug 의 디버거 브레이크라, 여기 두면 "없으면 nullptr" 계약이 Debug 에서만 프로세스를 멈추는 계약이 된다.
         *          `CheckNullableServiceUse` 린트가 받는 쪽의 널 검사를 강제한다. 짝인 `editor::getService<T>()` 와 같은 계약이다.
         */
        template <typename T>
        T* getService()
        {
            T* pScoped = locateSubsystem<T>( findGameScope() );
            if ( pScoped != nullptr )
                return pScoped;
            return locateSubsystem<T>( internal::findHostServices() );
        }

        /** @brief 서비스 @p T 가 지금 묶여 있는지 묻습니다. `getService<T>()` 와 같은 순서(게임 범위 → 호스트)로 본다. */
        template <typename T>
        bool isServiceBound()
        {
            return getService<T>() != nullptr;
        }

        /**
         * @class ScopedGameScope
         * @brief 범위 @p scope 를 이 객체가 사는 동안 "지금 게임 범위" 로 겁니다. 앞 범위를 상위로 이어, 그 범위에 없는 것은 앞 범위에서 찾는다.
         * @details 게임 인스턴스 없이 도는 테스트가 서비스를 잠시 걸 때 쓴다. 끝나면 앞 범위를 되돌린다(중첩해도 된다).
         */
        class ScopedGameScope
        {
        public:
            explicit ScopedGameScope( SubsystemScope& scope )
                : _pPrevious{ findGameScope() }
            {
                scope.setParentScope( _pPrevious );
                setGameScope( &scope );
            }
            ~ScopedGameScope() { setGameScope( _pPrevious ); }

            ScopedGameScope( const ScopedGameScope& )            = delete;
            ScopedGameScope& operator=( const ScopedGameScope& ) = delete;

        private:
            SubsystemScope* _pPrevious; ///< 걸기 전의 지금 범위
        };
    } // namespace game
} // namespace sw
