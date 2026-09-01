#include "pch.h"

#include "Editor/Panels/GameViewPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
	GameViewPanel::GameViewPanel()
		: _viewportClient{}
		, _bConfirmUnsavedPlay{ false }
	{
	}

	void GameViewPanel::onPanelCollapsed()
	{
		EditorContext* pClosedContext = EditorContext::get();
		if ( pClosedContext != nullptr )
		{
			pClosedContext->setGameViewFocused( false );
			pClosedContext->setGameViewHovered( false );
		}
	}

	void GameViewPanel::drawContent()
	{
		EditorContext* pEditorContext = EditorContext::get();
		if ( pEditorContext == nullptr )
			return;

		const bool	  bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
		const bool	  bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
		const float32 dt	   = ImGui::GetIO().DeltaTime;

		pEditorContext->setGameViewFocused( bFocused );
		pEditorContext->setGameViewHovered( bHovered );

		_viewportClient.update( dt, bFocused, bHovered );

		if ( EditorChrome::beginToolbar( "##GameViewToolbar" ) )
		{
			drawTransportControls();
			EditorWidgets::drawToolbarSeparator();
			_viewportClient.drawViewportToolbar( ImGui::GetContentRegionAvail().x );
		}
		EditorChrome::endToolbar();

		if ( _bConfirmUnsavedPlay )
		{
			ImGui::OpenPopup( "##UnsavedScenePlay" );
			_bConfirmUnsavedPlay = false;
		}
		if ( ImGui::BeginPopupModal( "##UnsavedScenePlay", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Scene has unsaved changes. Play anyway?" );
			if ( ImGui::Button( "Play" ) )
			{
				EditorPlaySession::play();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		const ImVec2 size = ImGui::GetContentRegionAvail();
		if ( size.x > 1.0f && size.y > 1.0f )
		{
			const uint32		  wantW		  = static_cast<uint32>( MathUtil::round( size.x ) );
			const uint32		  wantH		  = static_cast<uint32>( MathUtil::round( size.y ) );
			const EditorGameView& view		  = pEditorContext->getGameView();
			const int32			  dW		  = static_cast<int32>( wantW ) - static_cast<int32>( view._width );
			const int32			  dH		  = static_cast<int32>( wantH ) - static_cast<int32>( view._height );
			const bool			  bNeedResize = ( dW > 1 || dW < -1 || dH > 1 || dH < -1 ) && wantW > 0 && wantH > 0;
			if ( bNeedResize )
				pEditorContext->ensureGameViewSize( wantW, wantH );
		}

		const ImVec2 imagePos = ImGui::GetCursorScreenPos();
		_viewportClient.draw( pEditorContext->getGameView()._pTextureId, float2{ size.x, size.y } );

		if ( size.x > 1.0f && size.y > 1.0f )
		{
			const float2 barAnchor{ imagePos.x + size.x * 0.5f, imagePos.y + 8.0f };
			_viewportClient.drawTransformBar( barAnchor );
		}
	}

	void GameViewPanel::drawTransportControls()
	{
		const PlaySessionState currentState = EditorPlaySession::getState();
		EditorContext*		   pContext		= EditorContext::get();
		const bool			   bSceneDirty	= ( pContext != nullptr && pContext->getWorkspace().isSceneDirty() );

		if ( currentState == PlaySessionState::Playing )
		{
			EditorWidgets::drawChip( "Playing", editor::style::kOk );
			ImGui::SameLine();
		}
		else if ( ImGui::Button( "Play" ) )
		{
			if ( bSceneDirty && EditorPlaySession::isStopped() )
				_bConfirmUnsavedPlay = true;
			else
				EditorPlaySession::play();
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Simulate" ) )
		{
			if ( bSceneDirty && EditorPlaySession::isStopped() )
				_bConfirmUnsavedPlay = true;
			else
				EditorPlaySession::play();
		}

		ImGui::SameLine();
		if ( currentState == PlaySessionState::Paused )
		{
			EditorWidgets::drawChip( "Paused", editor::style::kWarn );
			ImGui::SameLine();
		}
		else if ( ImGui::Button( "Pause" ) )
			EditorPlaySession::pause();

		ImGui::SameLine();
		if ( ImGui::Button( "Step" ) )
			EditorPlaySession::stepOnce();

		ImGui::SameLine();
		if ( ImGui::Button( "Stop" ) )
		{
			EditorPlaySession::stop();
			SceneManager* pSceneManager = editor::getService<SceneManager>();
			if ( pSceneManager != nullptr )
			{
				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene != nullptr )
					EditorContext::get()->getWorkspace().remapSelectionByObjectName( pScene->getObjectManager() );
			}
		}
	}
} // namespace sw::editor
