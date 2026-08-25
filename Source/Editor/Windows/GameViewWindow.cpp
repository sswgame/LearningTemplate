#include "pch.h"

#include "Editor/Windows/GameViewWindow.h"

#include "Editor/Widgets/EditorWidgets.h"
#include "Editor/Workspace/EditorAssetDrop.h"
#include "Editor/Workspace/EditorTransaction.h"
#include "Editor/Workspace/EditorWorkspace.h"
#include "Editor/Workspace/SelectionManager.h"

#include "Engine/Game/GameState.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"

#include "RuntimeAPI/EditorService.h"
#include "RuntimeAPI/EditorUIContext.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw
{
	GameViewWindow::GameViewWindow()
	{
	}

	void GameViewWindow::draw( const EditorUIContext& ctx )
	{
		const bool	  bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
		const bool	  bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
		const float32 dt	   = ImGui::GetIO().DeltaTime;

		ctx._bIsGameViewFocused = bFocused;
		ctx._bIsGameViewHovered = bHovered;

		_viewportClient.update( dt, bFocused, bHovered );

		// 상단 기본 기즈모 모드 칩
		int32& op = EditorWorkspace::gizmoOperation();
		ImGui::RadioButton( "Translate", &op, 0 );
		ImGui::SameLine();
		ImGui::RadioButton( "Rotate", &op, 1 );
		ImGui::SameLine();
		ImGui::RadioButton( "Scale", &op, 2 );
		ImGui::SameLine();
		bool& bLocal = EditorWorkspace::gizmoLocalSpace();
		ImGui::Checkbox( "Local", &bLocal );

		const GameState gs = getGameState();
		if ( gs == GameState::Playing )
		{
			ImGui::SameLine();
			editor::drawChip( "Playing", editor::style::kOk );
		}
		else if ( gs == GameState::Paused )
		{
			ImGui::SameLine();
			editor::drawChip( "Paused", editor::style::kWarn );
		}

		const ImVec2 size = ImGui::GetContentRegionAvail();
		if ( size.x > 1.0f && size.y > 1.0f )
		{
			const uint32 wantW = static_cast<uint32>( MathUtil::round( size.x ) );
			const uint32 wantH = static_cast<uint32>( MathUtil::round( size.y ) );
			const int32	 dW	   = static_cast<int32>( wantW ) - static_cast<int32>( ctx._gameViewportWidth );
			const int32	 dH	   = static_cast<int32>( wantH ) - static_cast<int32>( ctx._gameViewportHeight );
			if ( ( dW > 1 || dW < -1 || dH > 1 || dH < -1 ) && wantW > 0 && wantH > 0 )
			{
				ctx._requestGameViewportWidth  = wantW;
				ctx._requestGameViewportHeight = wantH;
			}
		}

		_viewportClient.draw( ctx, ctx._pGameTextureID, float2{ size.x, size.y } );

		// 애셋 드롭 타깃 (Content Browser -> Viewport)
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
	}
} // namespace sw
