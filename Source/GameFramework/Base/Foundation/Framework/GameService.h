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
            /**
             * @brief 로컬 서비스 칸을 걸거나(@p pService) 풉니다(nullptr).
             * @details 소유 이미지(언로드 훑기가 이 칸을 회수할 이미지)는 @p pOwnerAddress 를 품은 이미지다. nullptr 이면 이 함수의 반환 주소를 쓴다.
             *          반환 주소가 맞는 것은 부른 곳이 헤더의 인라인 템플릿(`bindLocalService`)이라 **서비스를 든 모듈에서 인스턴스화될 때뿐**이다.
             *          GameFramework.dll 의 비템플릿 코드(예: `GameInstanceBase`)가 게임 객체의 칸을 대신 걸면 반환 주소는 GameFramework.dll 이라
             *          게임 이미지 언로드 때 회수되지 않는다 — 그런 곳은 소유자 주소를 명시한다(`getPolymorphicOwnerAddress`).
             */
            SW_GAMESERVICE_API void  bindRawLocalService( uint64 typeHash, void* pService, const void* pOwnerAddress = nullptr );
            SW_GAMESERVICE_API void* getRawLocalService( uint64 typeHash );
        } // namespace internal

        /**
         * @brief 다형 객체 @p object 의 vtable 주소를 돌려줍니다. 가장 파생된 클래스의 vtable 이라 그 클래스를 정의한 이미지 안에 있다.
         * @details 프레임워크 기반 클래스가 파생(게임 모듈) 객체가 든 서비스를 걸 때 소유자 주소로 넘긴다. 생성자 · 소멸자 안에서는 기반 vtable 이라 쓰지 않는다.
         */
        template <typename T>
        const void* getPolymorphicOwnerAddress( const T& object )
        {
            static_assert( std::is_polymorphic_v<T>, "getPolymorphicOwnerAddress requires a polymorphic type" );
            return *reinterpret_cast<const void* const*>( &object );
        }

        // bindGameService · unbindGameService 는 RuntimeAPI/Service/GameServiceBinding.h 가 선언한다(진입점 매크로가 RuntimeAPI 안에서 부른다).
        /**
         * @brief 게임 로컬 서비스(게임 인스턴스가 든 카탈로그 등)를 겁니다.
         * @details 같은 타입이 이미 다른 인스턴스에 걸려 있으면 알리고 걸지 않는다. 먼저 `unbindLocalService` 로 풀어야 한다.
         *          핫 리로드에서는 옛 인스턴스가 `onShutdown` 에서 푼다.
         *          소유 이미지는 이 템플릿을 인스턴스화한 모듈(부른 모듈)이다. 서비스를 든 모듈과 부른 모듈이 다르면 아래 오버로드로 소유자를 명시한다.
         */
        template <typename T>
        void bindLocalService( T* pService )
        {
            internal::bindRawLocalService( internal::getServiceTypeHash<T>(), static_cast<void*>( pService ) );
        }

        /**
         * @brief 소유 이미지를 @p pOwnerAddress 를 품은 이미지로 명시해 겁니다. 그 이미지가 내려갈 때 이 칸이 회수된다.
         * @details 프레임워크 코드가 게임 객체의 서비스를 대신 거는 곳에서 쓴다(예: `getPolymorphicOwnerAddress( *this )`).
         */
        template <typename T>
        void bindLocalService( T* pService, const void* pOwnerAddress )
        {
            internal::bindRawLocalService( internal::getServiceTypeHash<T>(), static_cast<void*>( pService ), pOwnerAddress );
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
         *          "붙였어야 하는데 안 붙었다"를 묻고 싶으면 `isServiceBound<T>()` 가 답한다.
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

        /**
         * @brief 서비스 @p T 가 지금 묶여 있는지 묻습니다. `getService<T>()` 와 같은 순서(게임 로컬 → 호스트) · 같은 trait 로 본다.
         * @details 호스트 서비스는 `ModuleServiceTraits<T>` 가 있는 타입만 본다(서비스 표의 그 칸). 로컬 서비스는 `bindLocalService<T>` 로 건 것이다.
         */
        template <typename T>
        bool isServiceBound()
        {
            if ( internal::getRawLocalService( internal::getServiceTypeHash<T>() ) != nullptr )
                return true;
            if constexpr ( internal::HasModuleServiceTraits<T>::value )
                return internal::getRawService( sw::internal::ModuleServiceTraits<T>::id ) != nullptr;
            else
                return false;
        }
    } // namespace game
} // namespace sw
