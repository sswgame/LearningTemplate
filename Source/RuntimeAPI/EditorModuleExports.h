/**
 * @file EditorModuleExports.h
 * @brief 에디터 모듈의 진입점(C-ABI) 구현을 위한 매크로 모음
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "RuntimeAPI/EditorAPI.h"
#include "RuntimeAPI/EditorService.h"

/**
 * @brief 에디터 모듈의 C-ABI 함수 테이블을 1줄로 구현 및 export하는 매크로
 * @param EditorClass sw::IEditor 인터페이스를 구현하는 에디터 클래스
 */

#define SW_IMPLEMENT_EDITOR_MODULE( EditorClass )                                                                                                               \
	extern "C" SW_MODULE_API bool fillEditorAPI( sw::EditorAPI* pOutApi )                                                                                       \
	{                                                                                                                                                           \
		if ( pOutApi == nullptr || pOutApi->_abiVersion != sw::kEditorAPIAbiVersion || pOutApi->_structSize < sizeof( sw::EditorAPI ) )                         \
			return false;                                                                                                                                       \
		pOutApi->create			   = []() -> sw::EditorHandle { return sw_new EditorClass(); };                                                                 \
		pOutApi->destroy		   = []( sw::EditorHandle editorHandle ) { sw_delete( static_cast<EditorClass*>( editorHandle ) ); };                           \
		pOutApi->initialize		   = []( sw::EditorHandle editorHandle, sw::WindowHandle windowHandle, sw::RHIDeviceHandle rhiDeviceHandle ) -> bool {           \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			return pInstance != nullptr ? pInstance->initialize( static_cast<sw::IWindow*>( windowHandle ), static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ) : false; };       \
		pOutApi->shutdown		   = []( sw::EditorHandle editorHandle ) {                                                                                      \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->shutdown(); };                                                                                   \
		pOutApi->preRender		   = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle ) {                                                 \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->preRender( static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                              \
		pOutApi->render			   = []( sw::EditorHandle editorHandle, const sw::EditorUIContext* pContext ) {                                                 \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr && pContext != nullptr )                                                                                                 \
				pInstance->render( *pContext ); };                                              \
		pOutApi->postPresent	   = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle ) {                                                 \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->postPresent( static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                              \
		pOutApi->processEvent	   = []( sw::EditorHandle editorHandle, const sw::NativeWindowEvent* pEvent, const sw::EditorUIContext* pContext ) -> bool {                                                 \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			return ( pInstance != nullptr && pEvent != nullptr ) ? pInstance->processEvent( *pEvent, pContext ) : false; }; \
		pOutApi->registerTexture   = []( sw::EditorHandle editorHandle, sw::TextureHandle textureHandle ) -> void* {                                           \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			return pInstance != nullptr ? pInstance->registerTexture( static_cast<sw::RHITextureHandle>( textureHandle ) ) : nullptr; };                                         \
		pOutApi->unregisterTexture = []( sw::EditorHandle editorHandle, void* pTextureId ) {                                                                   \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->unregisterTexture( pTextureId ); };                                                                 \
		pOutApi->bindService	   = []( const sw::EditorService* pService ) {                                                                                  \
			if ( pService != nullptr )                                                                                                                         \
				sw::editor::bindEditorService( *pService );                                                                                                    \
			else                                                                                                                                               \
				sw::editor::unbindEditorService(); };                                                                               \
		return true;                                                                                                                                            \
	}
