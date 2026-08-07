/**
 * @file AppModuleBinding.cpp
 * @brief Editor/Game MODULE API bind and hot-reload callbacks
 */
#include "App.h"
#include "AppInternal.h"

#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Module/LiveReloadManager.h"
#include "Core/Window/IWindow.h"
#include "Core/Graphics/RHI/RHI.h"

#if defined( SW_SHIPPING )
	#include "Runtime/GameAPI.h"
#endif

namespace sw
{
	using namespace app_internal;

	bool App::bindEditorAPI( void* hLibraryModule )
	{
		_editorApi = {};
		if ( hLibraryModule == nullptr )
			return false;

		auto pfnFill = reinterpret_cast<PFN_FillEditorAPI>( FileUtil::getDynamicSymbol( hLibraryModule, "fillEditorAPI" ) );
		if ( pfnFill == nullptr || pfnFill( &_editorApi ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to bind EditorAPI from module" );
			return false;
		}
		return _editorApi.create != nullptr && _editorApi.destroy != nullptr;
	}

	bool App::bindGameAPI( void* hLibraryModule )
	{
		_gameApi = {};
#if defined( SW_SHIPPING )
		(void)hLibraryModule;
		if ( fillGameAPI( &_gameApi ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to bind GameAPI (shipping)" );
			return false;
		}
#else
		if ( hLibraryModule == nullptr )
			return false;
		auto pfnFill = reinterpret_cast<PFN_FillGameAPI>( FileUtil::getDynamicSymbol( hLibraryModule, "fillGameAPI" ) );
		if ( pfnFill == nullptr || pfnFill( &_gameApi ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to bind GameAPI from module" );
			return false;
		}
#endif
		return _gameApi.create != nullptr && _gameApi.destroy != nullptr;
	}

	void App::onBeforeEditorReload()
	{
		if ( _editor && _editorApi.shutdown )
			_editorApi.shutdown( _editor );
		if ( _editor && _editorApi.destroy )
			_editorApi.destroy( _editor );
		_editor	   = nullptr;
		_editorApi = {};
	}

	void App::onAfterEditorReload( void* hLibraryModule )
	{
		if ( bindEditorAPI( hLibraryModule ) == false )
			return;

		_editor = _editorApi.create();
		if ( _editor == nullptr )
		{
			SW_LOG_ERROR( "[App] Failed to create Editor instance" );
			_editorApi = {};
			return;
		}

		if ( _editorApi.initialize( _editor, _window.get(), &_rhi->getDevice() ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to initialize Editor instance" );
			if ( _editorApi.destroy )
				_editorApi.destroy( _editor );
			_editor	   = nullptr;
			_editorApi = {};
			return;
		}

		if ( _gameRenderTarget && _editorApi.registerTexture )
			_gameTextureID = _editorApi.registerTexture( _editor, static_cast<TextureHandle>( _gameRenderTarget ) );

		SW_LOG_INFO( "[App] Editor initialized successfully via EditorAPI." );
	}

	void App::onBeforeGameReload()
	{
		if ( _game == nullptr )
			return;

		if ( _gameApi.shutdown )
			_gameApi.shutdown( _game );
		if ( _gameApi.destroy )
			_gameApi.destroy( _game );
		_game	 = nullptr;
		_gameApi = {};
	}

	void App::onAfterGameReload( void* hLibraryModule )
	{
#if defined( SW_SHIPPING )
		(void)hLibraryModule;
		if ( _gameApi.create == nullptr && bindGameAPI( nullptr ) == false )
			return;
#else
		void* moduleHandle = hLibraryModule;
		if ( moduleHandle == nullptr && _liveReloadManager )
			moduleHandle = _liveReloadManager->getModuleHandle( kGameModuleName );

		if ( bindGameAPI( moduleHandle ) == false )
			return;
#endif

		_game = _gameApi.create();
		if ( _game == nullptr )
		{
			SW_LOG_ERROR( "[App] Failed to create Game instance" );
			_gameApi = {};
			return;
		}

		if ( _gameApi.initialize( _game, _window.get(), &_rhi->getDevice() ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to initialize Game instance" );
			if ( _gameApi.destroy )
				_gameApi.destroy( _game );
			_game	 = nullptr;
			_gameApi = {};
			return;
		}

		SW_LOG_INFO( "[App] SWGame initialized successfully via GameAPI." );
	}
} // namespace sw
