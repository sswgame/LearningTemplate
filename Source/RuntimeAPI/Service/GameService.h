/**
 * @file GameService.h
 * @brief App이 채우고 게임 모듈이 쓰는 서비스 로케이터.
 * @note 에디터 전용 서비스 id는 이 헤더에 없습니다. 구현은 GameFramework.dll이 export합니다.
 */
#pragma once
#include "RuntimeAPI/Service/ModuleService.h"

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
		SW_GAMESERVICE_API void bindGameService( const ModuleService& service );
		SW_GAMESERVICE_API void unbindGameService();
		/** @brief 서비스 콜백이 바인딩되었는지 확인합니다. */
		SW_GAMESERVICE_API bool areGameServicesBound();

		SW_GAMESERVICE_API void* getRawService( ModuleServiceId id );
		SW_GAMESERVICE_API void	 bindLocalService( ModuleServiceId id, void* pService );

		template <typename T>
		void bindLocalService( T* pService )
		{
			bindLocalService( ModuleServiceTraits<T>::id, static_cast<void*>( pService ) );
		}

		template <typename T>
		void unbindLocalService()
		{
			bindLocalService( ModuleServiceTraits<T>::id, nullptr );
		}

		template <typename T>
		T* getService()
		{
			return static_cast<T*>( getRawService( ModuleServiceTraits<T>::id ) );
		}
	} // namespace game
} // namespace sw
