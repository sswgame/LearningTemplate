/**
 * @file GameModuleExports.h
 * @brief 게임 모듈의 진입점(C-ABI) 구현을 위한 매크로 모음
 */
#pragma once
#include "Core/Memory/Memory.h"
#include "RuntimeAPI/GameAPI.h"
#include "RuntimeAPI/GameService.h"

/**
 * @brief 게임 모듈의 C-ABI 함수 테이블을 1줄로 구현 및 export하는 매크로
 * @param GameClass sw::IGame 인터페이스를 구현하는 게임 클래스
 */
#define SW_IMPLEMENT_GAME_MODULE( GameClass )                                                                                                       \
	extern "C" SW_MODULE_API bool fillGameAPI( sw::GameAPI* pOutApi )                                                                                \
	{                                                                                                                                               \
		if ( pOutApi == nullptr || pOutApi->_abiVersion != sw::kGameAPIAbiVersion || pOutApi->_structSize < sizeof( sw::GameAPI ) )                    \
			return false;                                                                                                                           \
		pOutApi->create			 = []() -> sw::GameHandle { return sw_new GameClass(); };                                                           \
		pOutApi->destroy			 = []( sw::GameHandle gameHandle ) { sw_delete( static_cast<GameClass*>( gameHandle ) ); };                         \
		pOutApi->initialize		 = []( sw::GameHandle gameHandle, sw::WindowHandle windowHandle, sw::RHIDeviceHandle rhiDeviceHandle ) -> bool {                  \
			GameClass* pInstance = static_cast<GameClass*>( gameHandle );                                                                                           \
			return pInstance != nullptr ? pInstance->initialize( static_cast<sw::IWindow*>( windowHandle ), static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ) : false; }; \
		pOutApi->shutdown		 = []( sw::GameHandle gameHandle ) {                                                                                           \
			GameClass* pInstance = static_cast<GameClass*>( gameHandle );                                                                                           \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->shutdown(); };                                                                             \
		pOutApi->update			 = []( sw::GameHandle gameHandle, float32 deltaTime ) {                                                                        \
			GameClass* pInstance = static_cast<GameClass*>( gameHandle );                                                                                           \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->update( deltaTime ); };                                                          \
		pOutApi->fixedUpdate		 = []( sw::GameHandle gameHandle, float32 fixedDeltaTime ) {                                                                   \
			GameClass* pInstance = static_cast<GameClass*>( gameHandle );                                                                                           \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->fixedUpdate( fixedDeltaTime ); };                                                     \
		pOutApi->bindService		 = []( const sw::GameService* pService ) {                                                                                     \
			if ( pService != nullptr )                                                                                                                         \
				sw::game::bindGameService( *pService );                                                                                                        \
			else                                                                                                                                               \
				sw::game::unbindGameService(); };                                                                       \
		pOutApi->serializeState	 = []( sw::GameHandle gameHandle, void* pOutBuffer, uint32* pInOutSize ) -> bool {                                             \
			GameClass* pInstance = static_cast<GameClass*>( gameHandle );                                                                                           \
			return pInstance != nullptr ? pInstance->serializeState( pOutBuffer, pInOutSize ) : false; };                               \
		pOutApi->deserializeState = []( sw::GameHandle gameHandle, const void* pInBuffer, uint32 bufferSize ) -> bool {                                        \
			GameClass* pInstance = static_cast<GameClass*>( gameHandle );                                                                                           \
			return pInstance != nullptr ? pInstance->deserializeState( pInBuffer, bufferSize ) : false; };                           \
		return true;                                                                                                                                \
	}
