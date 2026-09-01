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
			struct HasModuleServiceTraits<T, std::void_t<decltype( ModuleServiceTraits<T>::id )>> : std::true_type
			{
			};

			SW_GAMESERVICE_API void* getRawService( ModuleServiceId id );
			SW_GAMESERVICE_API void	 bindRawLocalService( uint64 typeHash, void* pService );
			SW_GAMESERVICE_API void* getRawLocalService( uint64 typeHash );
		} // namespace internal

		SW_GAMESERVICE_API void bindGameService( const ModuleService& service );
		SW_GAMESERVICE_API void unbindGameService();
		/** @brief 서비스 콜백이 바인딩되었는지 확인합니다. */
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
				T* pHost = static_cast<T*>( internal::getRawService( ModuleServiceTraits<T>::id ) );
				if ( pHost != nullptr )
					return pHost;
			}

			SW_ASSERT( false && "Requested game service was not found in local or host registry!" );
			return nullptr;
		}
	} // namespace game
} // namespace sw
