/**
 * @file AppWindow.cpp
 * @brief Window resize / native message handlers
 */
#include "App.h"
#include "AppInternal.h"

#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Module/LiveReloadManager.h"
#include "Core/Graphics/RHI/RHI.h"
#include "Core/Window/NativeWindowEvent.h"

namespace sw
{
	using namespace app_internal;

	void App::onResize( uint32 w, uint32 h )
	{
		if ( _rhi )
			_rhi->getDevice().resize( w, h );

		if ( _bEnableEditor == 0 || _rhi == nullptr || w == 0 || h == 0 )
			return;

		if ( _gameRenderTarget != 0 )
		{
			_rhi->getDevice().destroyTexture( _gameRenderTarget );
			_gameRenderTarget = 0;
			_gameTextureID	  = nullptr;
		}

		if ( createGameViewportTexture( w, h ) == false )
		{
			SW_LOG_WARNING( "[App] Game View RT recreate on resize failed (%# x %#)", w, h );
			_editorCtx.gameTextureID = nullptr;
			return;
		}

		if ( _editor && _editorApi.registerTexture )
			_gameTextureID = _editorApi.registerTexture( _editor, static_cast<TextureHandle>( _gameRenderTarget ) );

		_editorCtx.gameTextureID = _gameTextureID;
	}

	bool App::onWindowMessage( const NativeWindowEvent& event )
	{
#if defined( SW_PLATFORM_WINDOWS ) && !defined( SW_SHIPPING )
		if ( event.message == WM_KEYDOWN && _rhi )
		{
			switch ( event.wParam )
			{
				case VK_F5:
					_rhi->getLiveShaderManager().triggerReloadAll();
					SW_LOG_INFO( "[App] F5: force shader reload" );
					break;
				case VK_F6:
					if ( _liveReloadManager && _bEnableEditor )
					{
						_liveReloadManager->triggerReload( kEditorModuleName );
						SW_LOG_INFO( "[App] F6: force EditorModule reload" );
					}
					break;
				case VK_F7:
					if ( _liveReloadManager )
					{
						_liveReloadManager->triggerReload( kGameModuleName );
						SW_LOG_INFO( "[App] F7: force SWGame reload" );
					}
					break;
				default:
					break;
			}
		}
#endif

		if ( _editor && _editorApi.processEvent )
			return _editorApi.processEvent( _editor, &event );
		return false;
	}
} // namespace sw
