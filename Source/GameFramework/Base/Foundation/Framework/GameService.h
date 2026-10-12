/**
 * @file GameService.h
 * @brief GameFramework 가 제공하고 게임 모듈이 쓰는 서비스 로케이터입니다.
 */
#pragma once
#include "Core/Container/StringUtil.h"

#include "RuntimeAPI/Service/GameServiceBinding.h"
#include "RuntimeAPI/Service/ModuleService.h"

#include <type_traits>

namespace sw
{
    namespace game
    {
        namespace internal
        {
            /** @brief RTTI 없이 컴파일타임에 고유한 64비트 타입 해시를 생성합니다. */
            template <typename T>
            constexpr uint64 getServiceTypeHash() noexcept
            {
                return StringUtil::computeHash64( SW_FUNCTION_SIGNATURE, sizeof( SW_FUNCTION_SIGNATURE ) - 1, false );
            }

            template <typename T, typename = void>
            struct HasModuleServiceTraits : std::false_type
            {
            };

            template <typename T>
            struct HasModuleServiceTraits<T, std::void_t<decltype( sw::internal::ModuleServiceTraits<T>::id )>> : std::true_type
            {
            };

            SW_GAMESERVICE_API void* getRawService( sw::internal::ModuleServiceID id );
            SW_GAMESERVICE_API void  bindRawLocalService( uint64 typeHash, void* pService );
            SW_GAMESERVICE_API void* getRawLocalService( uint64 typeHash );
        } // namespace internal

        // bindGameService · unbindGameService 는 RuntimeAPI/Service/GameServiceBinding.h 가 선언한다(진입점 매크로가 RuntimeAPI 안에서 부른다).
        /**
         * @brief 게임 서비스 테이블의 SceneManager 칸이 묶였는지 묻습니다(`bindGameService` 뒤 · `unbindGameService` 전).
         * @details 검사하는 것은 `ModuleServiceID::SceneManager` 하나다. true 면 `getService<SceneManager>()` 는 널이 아니고,
         *          다른 서비스에 대해서는 아무것도 보장하지 않는다.
         */
        SW_GAMESERVICE_API bool isSceneManagerServiceBound();

        /**
         * @brief 게임 로컬 서비스(게임 인스턴스가 든 카탈로그 등)를 겁니다.
         * @details 같은 타입이 이미 다른 인스턴스에 걸려 있으면 알리고 걸지 않는다. 먼저 `unbindLocalService` 로 풀어야 한다.
         *          핫 리로드에서는 옛 인스턴스가 `onShutdown` 에서 푼다.
         */
        template <typename T>
        void bindLocalService( T* pService )
        {
            internal::bindRawLocalService( internal::getServiceTypeHash<T>(), static_cast<void*>( pService ) );
        }

        template <typename T>
        void unbindLocalService()
        {
            internal::bindRawLocalService( internal::getServiceTypeHash<T>(), nullptr );
        }

        /**
         * @brief 게임 서비스를 찾습니다. **없으면 nullptr 입니다.** 받는 쪽이 확인해야 합니다.
         * @details 서비스가 없어도 `SW_ASSERT` 로 죽지 않는다. `SW_ASSERT` 는 Debug 에서 **디버거 브레이크**이고
         *          Debug 밖에서는 통째로 사라지므로, 여기에 두면 "없으면 nullptr"이라는 계약이 Debug 에서만 프로세스를 멈추는 계약이 된다.
         *          부르는 쪽이 모두 `pX == nullptr` 을 확인하고 `CheckNullableServiceUse` 린트도 그 모양을 강제하는데,
         *          브레이크가 먼저 걸리면 그 가드는 Debug 에서 한 번도 도달할 수 없다.
         *
         *          짝인 `editor::getService<T>()` 도 처음부터 조용히 nullptr 을 반환한다. 같은 함수가 두 벌 있는데 한쪽만 죽으면 안 되므로 이쪽을 맞춘다.
         *
         *          "붙였어야 하는데 안 붙었다"를 묻고 싶으면 `isSceneManagerServiceBound()` 가 답한다.
         *          다만 그 함수는 `SceneManager` 슬롯 하나만 본다.
         * @return 찾은 서비스이다. 로컬에도 호스트에도 없으면 nullptr 이다.
         */
        template <typename T>
        T* getService()
        {
            // 1) 게임 로컬 서비스를 먼저 조회한다. RTTI 없이 컴파일 타임 TypeID 해시 레지스트리를 쓴다.
            void* pLocal = internal::getRawLocalService( internal::getServiceTypeHash<T>() );
            if ( pLocal != nullptr )
                return static_cast<T*>( pLocal );

            // 2) 호스트(엔진) 서비스를 조회한다.
            if constexpr ( internal::HasModuleServiceTraits<T>::value )
            {
                T* pHost = static_cast<T*>( internal::getRawService( sw::internal::ModuleServiceTraits<T>::id ) );
                if ( pHost != nullptr )
                    return pHost;
            }
            return nullptr;
        }
    } // namespace game
} // namespace sw
