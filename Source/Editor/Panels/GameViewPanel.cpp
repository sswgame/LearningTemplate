#include "pch.h"

#include "Editor/Panels/GameViewPanel.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetDrop.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Game/GameState.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	GameViewPanel::GameViewPanel()
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

		editor::EditorSectionDesc toolbarDesc{};
		toolbarDesc._pId  = "##GameViewToolbar";
		toolbarDesc._kind = editor::EditorSectionKind::Toolbar;
		if ( editor::beginSection( toolbarDesc ) )
		{
			drawTransportControls();
			editor::drawToolbarSeparator();
			_viewportClient.drawViewportToolbar( ImGui::GetContentRegionAvail().x );
		}
		editor::endSection();

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

		if ( ImGui::BeginDragDropTarget() )
		{
			if ( const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( "SW_CONTENT_BROWSER_ASSET" ) )
			{
				const utf8* pPath = static_cast<const utf8*>( pPayload->Data );
				if ( pPath != nullptr )
				{
					SceneManager* pSceneManager = editor::getService<SceneManager>();
					if ( pSceneManager != nullptr && pSceneManager->getActiveScene() != nullptr )
					{
						GameObjectManager* pManager = pSceneManager->getActiveScene()->getObjectManager();
						if ( pManager != nullptr )
						{
							GameObject* pSpawned = editor::spawnPrefabFromAssetPath( pManager, pPath );
							if ( pSpawned != nullptr )
							{
								EditorTransaction::recordCreation( GameObjectPtr{ pSpawned }, "Spawn Prefab" );
								EditorWorkspace::selectGameObject( GameObjectPtr{ pSpawned }, SelectionMode::Replace );
							}
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if ( size.x > 1.0f && size.y > 1.0f )
		{
			const float2 barAnchor{ imagePos.x + size.x * 0.5f, imagePos.y + 8.0f };
			_viewportClient.drawTransformBar( barAnchor );
		}
	}

	void GameViewPanel::drawTransportControls()
	{
		const GameState currentState = getGameState();

		if ( currentState == GameState::Playing )
		{
			editor::drawChip( "Playing", editor::style::kOk );
			ImGui::SameLine();
		}
		else if ( ImGui::Button( "Play" ) )
			setGameState( GameState::Playing );

		ImGui::SameLine();
		if ( ImGui::Button( "Simulate" ) )
			setGameState( GameState::Playing );

		ImGui::SameLine();
		if ( currentState == GameState::Paused )
		{
			editor::drawChip( "Paused", editor::style::kWarn );
			ImGui::SameLine();
		}
		else if ( ImGui::Button( "Pause" ) )
			setGameState( GameState::Paused );

		ImGui::SameLine();
		if ( ImGui::Button( "Step" ) )
		{
			if ( currentState != GameState::Playing )
				setGameState( GameState::Playing );
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Stop" ) )
		{
			setGameState( GameState::Stopped );
			SceneManager* pSceneManager = editor::getService<SceneManager>();
			if ( pSceneManager != nullptr )
			{
				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene != nullptr )
					EditorWorkspace::remapSelectionByObjectName( pScene->getObjectManager() );
			}
		}
	}
} // namespace sw::editor
