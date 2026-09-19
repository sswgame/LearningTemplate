/**
 * @file GameService.h
 * @brief GameFramework가 제공하고 게임 모듈이 사용하는 서비스 로케이터.
 */
#pragma once
#include "Core/String/StringUtil.h"

#include "RuntimeAPI/Service/ModuleService.h"

#include <type_traits>

#if defined( SW_PLATFORM_WINDOWS )
    #if defined( SW_GF_EXPORTS )
        #define SW_GAMESERVICE_API __declspec( dllexport )
    #elif defined( SW_GF_IMPORTS )
        #define SW_GAMESERVICE_API __declspec( dllimport )
    #else
        #define SW_GAMESERVICE_API
    #endif
#else
    #define SW_GAMESERVICE_API __attribute__( ( visibility( "default" ) ) )
#endif

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

            SW_GAMESERVICE_API void* getRawService( sw::internal::ModuleServiceId id );
            SW_GAMESERVICE_API void  bindRawLocalService( uint64 typeHash, void* pService );
            SW_GAMESERVICE_API void* getRawLocalService( uint64 typeHash );
        } // namespace internal

        SW_GAMESERVICE_API void bindGameService( const ModuleService& service );
        SW_GAMESERVICE_API void unbindGameService();
        /**
         * @brief 게임 서비스가 쓸 수 있는 상태인지 — **SceneManager 슬롯 하나를 봅니다.**
         * @details 이름은 "서비스들이 붙었는가" 로 읽히지만 실제로 검사하는 것은
         *          `ModuleServiceId::SceneManager` **하나**다. 그래서 이 함수가 true 면
         *          `getService<SceneManager>()` 는 널일 수 없고, 반대로 다른 서비스에 대해서는
         *          **아무것도 보장하지 않는다.** 2026-09-18 에 이것을 "필수 서비스 전체를 본다" 로
         *          잘못 읽어 도달할 수 없는 가드를 넣은 적이 있다 — 이름만 보고 판단하지 말 것.
         */
        SW_GAMESERVICE_API bool areGameServicesBound();

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
         * @brief 게임 서비스를 찾습니다. **없으면 nullptr 입니다** — 받는 쪽이 확인해야 합니다.
         * @details 여기에 `SW_ASSERT( false )` 가 있었다. 그런데 `SW_ASSERT` 는 Debug 에서
         *          **디버거 브레이크**이고 Debug 밖에서는 통째로 사라진다. 그래서
         *          "없으면 nullptr" 이라는 이 함수의 계약은 **Debug 에서만 프로세스를 죽이는**
         *          계약이었다. 호출하는 서른한 자리가 전부 `pX == nullptr` 을 확인하고 있었고
         *          `CheckNullableServiceUse` 린트도 그 모양을 강제하는데, 그 가드는 Debug 에서
         *          **한 번도 도달할 수 없었다** — 브레이크가 먼저 걸린다.
         *
         *          실제로 이것에 부딪힌 곳: `TurnBattleSaveGame::loadFromFile` 의 텍스트 경로는
         *          `GameData` 가 없으면 파티 상한으로 6 을 쓰도록 **이미 적혀 있는데**, 게임이
         *          붙지 않은 프로세스(도구·테스트)에서 그 폴백에 닿기 전에 죽었다.
         *
         *          짝인 `editor::getService<T>()` 는 처음부터 조용히 nullptr 을 돌려준다.
         *          같은 함수가 두 벌 있는데 한쪽만 죽는 것이었다 — 살아 있는 쪽에 맞춘다.
         *
         *          "붙였어야 하는데 안 붙었다" 를 묻고 싶으면 `areGameServicesBound()` 가 그
         *          질문의 답이다(다만 그것은 `SceneManager` 슬롯 하나만 본다).
         * @return 찾은 서비스. 로컬에도 호스트에도 없으면 nullptr.
         */
        template <typename T>
        T* getService()
        {
            // 1) 게임 로컬 서비스 우선 조회 (RTTI-Free 컴파일타임 TypeId 해시 레지스트리)
            void* pLocal = internal::getRawLocalService( internal::getServiceTypeHash<T>() );
            if ( pLocal != nullptr )
                return static_cast<T*>( pLocal );

            // 2) 호스트(엔진) 서비스 조회
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
