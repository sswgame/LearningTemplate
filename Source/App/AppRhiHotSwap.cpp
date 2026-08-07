/**
 * @file AppRhiHotSwap.cpp
 * @brief Soft RHI backend recreate / hot-swap
 */
#include "App.h"
#include "AppInternal.h"

#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Module/LiveReloadManager.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Graphics/RHI/RHI.h"
#include "Core/Graphics/RHI/RHICapabilities.h"
#include "Core/Graphics/Material/Material.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"

namespace sw
{
	using namespace app_internal;

	bool App::applyPendingBackendChange()
	{
		const RHIBackend requested = _pendingRHIBackend;
		const RHIBackend previous  = _committedRHIBackend;
		if ( requested == previous )
			return true;

		if ( _rhi == nullptr )
			return false;

		void* editorModule = nullptr;
		void* gameModule   = nullptr;
#if !defined( SW_SHIPPING )
		if ( _liveReloadManager )
		{
			editorModule = _liveReloadManager->getModuleHandle( kEditorModuleName );
			gameModule	 = _liveReloadManager->getModuleHandle( kGameModuleName );
		}
#endif

		SW_LOG_INFO( "[Hot-Swap] Soft-recreating RHI: %# → %#",
					 RHI::getBackendTypeName( previous ),
					 RHI::getBackendTypeName( requested ) );

		// Must release editor/game and GPU dependents before recreateDevice tears down the device.
		BLOCK( "기존 RHI / Scene / Editor 리소스 정리" )
		{
			_rhi->getDevice().waitIdle();

			onBeforeEditorReload();
			onBeforeGameReload();

			if ( _gameRenderTarget != 0 )
			{
				_rhi->getDevice().destroyTexture( _gameRenderTarget );
				_gameRenderTarget = 0;
				_gameTextureID	  = nullptr;
			}

			if ( Scene* scene = _sceneManager ? _sceneManager->getActiveScene() : nullptr )
			{
				if ( Material* material = scene->getMaterial() )
					material->shutdown( &_rhi->getDevice() );
			}

			_rhi->getDevice().waitIdle();
		}

		bool bHaveDevice = false;
		BLOCK( "RHI Device 재생성" )
		{
			gv_RHIBackend = requested;
			if ( _rhi->recreateDevice( requested ) )
			{
				_committedRHIBackend = requested;
				bHaveDevice			 = true;
			}
			else
			{
				SW_LOG_ERROR( "[Hot-Swap] recreateDevice(%#) failed — restoring %#",
							  RHI::getBackendTypeName( requested ),
							  RHI::getBackendTypeName( previous ) );
				gv_RHIBackend = previous;
				if ( _rhi->recreateDevice( previous ) )
				{
					bHaveDevice = true;
				}
				else
				{
					SW_LOG_ERROR( "[Hot-Swap] FATAL: restore of previous backend %# also failed. Nulling editor/game cleanly.",
								  RHI::getBackendTypeName( previous ) );
					_gameRenderTarget = 0;
					_gameTextureID	  = nullptr;
					_editor			  = nullptr;
					_editorApi		  = {};
					_game			  = nullptr;
					_gameApi		  = {};
					_editorCtx.rhiDevice	 = nullptr;
					_editorCtx.gameTextureID = nullptr;
					return false;
				}
			}
		}

		// Device is available (new or restored) — re-bind scene / editor / game.
		BLOCK( "Scene / Editor / Game 재초기화" )
		{
			(void)bHaveDevice;
			if ( Scene* scene = _sceneManager ? _sceneManager->getActiveScene() : nullptr )
			{
				if ( scene->initialize( &_rhi->getDevice() ) == false )
					SW_LOG_ERROR( "[Hot-Swap] Scene re-initialize failed after backend change." );
			}

			if ( _bEnableEditor )
			{
				if ( createGameViewportTexture( _gameViewportWidth, _gameViewportHeight ) == false )
					SW_LOG_WARNING( "[Hot-Swap] Game viewport texture recreate failed." );
				onAfterEditorReload( editorModule );
			}

#if defined( SW_SHIPPING )
			onAfterGameReload( nullptr );
#else
			onAfterGameReload( gameModule );
#endif

			_editorCtx.rhiDevice	 = &_rhi->getDevice();
			_editorCtx.gameTextureID = _gameTextureID;
		}

		SW_LOG_INFO( "[Hot-Swap] Active backend is now %#", RHI::getBackendTypeName( _committedRHIBackend ) );
		return true;
	}
} // namespace sw
