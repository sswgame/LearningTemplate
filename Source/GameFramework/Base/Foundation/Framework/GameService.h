/**
 * @file GameService.h
 * @brief GameFramework 가 제공하고 게임 모듈과 키트가 쓰는 서비스 API 입니다. 게임 범위(`SubsystemScope`)에서 찾고, 없으면 호스트(엔진) 서비스 테이블을 봅니다.
 * @details 게임 범위는 게임 인스턴스(`GameInstance`)가 소유하고 기동 때 "지금 범위" 로 설정한다. 언리얼에서 게임 인스턴스가 `UGameInstanceSubsystem` 을 소유하는 것에 해당한다.
 *          게임이 쓰는 카탈로그와 설정은 그 범위에 등록한다(`GameInstance::bindScopedService`). 범위는 인스턴스와 함께 사라지므로 게임 모듈을 언로드할 때
 *          남는 항목이 없다. 테스트는 자기 범위를 만들어 `ScopedGameScope` 로 잠시 "지금 범위" 로 설정한다.
 */
#pragma once
#include "Core/Subsystem/SubsystemScope.h"

#include "ModuleAPI/Service/GameServiceBinding.h"

namespace sw
{
    namespace game
    {
        namespace internal
        {
            /** @brief App 이 넘긴 호스트 서비스 범위입니다(`bindGameService`). 없으면 nullptr 입니다. */
            SW_GAMESERVICE_API const ISubsystemLocator* findHostServices();
        } // namespace internal

        // bindGameService · unbindGameService 는 ModuleAPI/Service/GameServiceBinding.h 가 선언한다(진입점 매크로가 ModuleAPI 안에서 부른다).

        /** @brief 지금 게임 범위입니다. 게임 인스턴스가 서기 전이나 내려간 뒤에는 nullptr 입니다. */
        SW_GAMESERVICE_API SubsystemScope* findGameScope();
        /**
         * @brief 지금 게임 범위를 겁니다(nullptr 이면 풉니다). 게임 인스턴스가 기동 · 종료에서 부른다.
         * @details 핫 리로드에서는 새 인스턴스가 먼저 설정할 수 있으므로, 해제하는 쪽은 자기 범위가 설정돼 있을 때만 해제한다.
         */
        SW_GAMESERVICE_API void setGameScope( SubsystemScope* pScope );

        /**
         * @brief 게임 서비스를 찾습니다. **없으면 nullptr 입니다.** 받는 쪽이 확인해야 합니다.
         * @details 지금 게임 범위(그 상위 범위까지)에서 먼저 찾고, 없으면 호스트 서비스 표를 본다. 서비스가 없어도 `SW_ASSERT` 로 멈추지 않는다 —
         *          `SW_ASSERT` 는 Debug 의 디버거 브레이크라, 여기 두면 "없으면 nullptr" 계약이 Debug 에서만 프로세스를 멈추는 계약이 된다.
         *          받는 쪽은 널을 검사해야 한다. 짝인 `editor::findSubsystem<T>()` · `engine::findSubsystem<T>()` 와 같은 계약이다.
         */
        template <typename T>
        T* findSubsystem()
        {
            T* pScoped = locateSubsystem<T>( findGameScope() );
            if ( pScoped != nullptr )
                return pScoped;
            return locateSubsystem<T>( internal::findHostServices() );
        }

        /**
         * @brief 반드시 있어야 하는 게임 서비스 @p T 입니다(`findSubsystem<T>()` 와 같은 순서로 찾는다). 없으면 부른 쪽의 잘못이라 모든 구성에서 오류를 남기고 멈춥니다(`sw::internal::reportMissingRequiredService`).
         * @details 서비스가 바인딩된 뒤에만 도는 경로(카탈로그 읽기, 타입 조회)가 쓴다. 없을 수 있는 곳에서는 `findSubsystem<T>()` 를 쓴다.
         */
        template <typename T>
        T& getSubsystem()
        {
            T* pFound = findSubsystem<T>();
            if ( pFound == nullptr )
                ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
            return *pFound;
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
