/**
 * @file EditorModuleExports.h
 * @brief 에디터 모듈의 진입점(C-ABI) 구현을 위한 매크로 모음
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "RuntimeAPI/ABI/EditorAPI.h"
#include "RuntimeAPI/Service/EditorService.h"

/**
 * @brief 에디터 모듈의 C-ABI 함수 테이블을 1줄로 구현 및 export하는 매크로
 * @param EditorClass sw::IEditor를 구현하는 에디터 클래스 (보통 sw::editor::ImGuiEditor)
 */

#define SW_IMPLEMENT_EDITOR_MODULE( EditorClass )                                                                                                         \
	extern "C" SW_MODULE_API bool exportEditorAPI( sw::EditorAPI* pOutApi )                                                                               \
	{                                                                                                                                                     \
		if ( pOutApi == nullptr )                                                                                                                         \
			return false;                                                                                                                                 \
		pOutApi->create			   = []() -> sw::EditorHandle { return sw_new EditorClass(); };                                                           \
		pOutApi->destroy		   = []( sw::EditorHandle editorHandle ) { sw_delete( static_cast<EditorClass*>( editorHandle ) ); };                     \
		pOutApi->initialize		   = []( sw::EditorHandle editorHandle, sw::WindowHandle windowHandle, sw::RHIDeviceHandle rhiDeviceHandle ) -> bool {           \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			return pInstance != nullptr ? pInstance->initialize( static_cast<sw::IWindow*>( windowHandle ), static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ) : false; }; \
		pOutApi->shutdown		   = []( sw::EditorHandle editorHandle ) {                                                                                      \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->shutdown(); };                                                                             \
		pOutApi->updateUI		   = []( sw::EditorHandle editorHandle ) {                                                                                      \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->updateUI(); };                                                                             \
		pOutApi->preRender		   = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle ) {                                                 \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->preRender( static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                        \
		pOutApi->render			   = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle ) {                                                 \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->render( static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                        \
		pOutApi->postPresent	   = []( sw::EditorHandle editorHandle, sw::RHIDeviceHandle rhiDeviceHandle ) {                                                 \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->postPresent( static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); };                                        \
		pOutApi->processEvent	   = []( sw::EditorHandle editorHandle, const sw::NativeWindowEvent* pEvent ) -> bool {                                         \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			return ( pInstance != nullptr && pEvent != nullptr ) ? pInstance->processEvent( *pEvent ) : false; };                                \
		pOutApi->registerTexture   = []( sw::EditorHandle editorHandle, sw::TextureHandle textureHandle ) -> void* {                                           \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			return pInstance != nullptr ? pInstance->registerTexture( static_cast<sw::RHITextureHandle>( textureHandle ) ) : nullptr; };                                   \
		pOutApi->unregisterTexture = []( sw::EditorHandle editorHandle, void* pTextureId ) {                                                                   \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->unregisterTexture( pTextureId ); };                                                           \
		pOutApi->getGameViewport   = []( sw::EditorHandle editorHandle, uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ) {                             \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                                       \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->getGameViewport( pRenderTarget, pWidth, pHeight ); };                     \
		pOutApi->getViewportCamera = []( sw::EditorHandle editorHandle ) -> void* {                                                                            \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                               \
			return pInstance != nullptr ? static_cast<void*>( pInstance->getViewportCamera() ) : nullptr; };                                                                    \
		pOutApi->bindService	   = []( const sw::ModuleService* pService ) {                                                                                  \
			if ( pService != nullptr )                                                                                                                         \
				sw::editor::bindEditorService( *pService );                                                                                                    \
			else                                                                                                                                               \
				sw::editor::unbindEditorService(); };                                                                         \
		pOutApi->isPlaying		   = []( sw::EditorHandle editorHandle ) -> bool {                                                                              \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                               \
			return pInstance != nullptr ? pInstance->isPlaying() : false; };                                                                     \
		pOutApi->isPaused		   = []( sw::EditorHandle editorHandle ) -> bool {                                                                              \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                               \
			return pInstance != nullptr ? pInstance->isPaused() : false; };                                                                     \
		pOutApi->stopSimulation	   = []( sw::EditorHandle editorHandle ) {                                                                                      \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                               \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->stopSimulation(); };                                                                             \
		pOutApi->endFrame		   = []( sw::EditorHandle editorHandle ) {                                                                                      \
			EditorClass* pInstance = static_cast<EditorClass*>( editorHandle );                                                                               \
			if ( pInstance != nullptr )                                                                                                                        \
				pInstance->onHostFrameEnd(); };                                                                             \
		return true;                                                                                                                                      \
	}
