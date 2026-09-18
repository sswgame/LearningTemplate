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

            SW_ASSERT( false && "Requested game service was not found in local or host registry!" );
            return nullptr;
        }
    } // namespace game
} // namespace sw
