#include "pch.h"

#include "Editor/Windows/GameViewWindow.h"

#include "Editor/Common/EditorContext.h"
#include "Editor/Overlay/EditorTransformBar.h"
#include "Editor/Overlay/EditorTransportBar.h"
#include "Editor/Workspace/EditorAssetDrop.h"
#include "Editor/Workspace/EditorTransaction.h"
#include "Editor/Workspace/EditorWorkspace.h"
#include "Editor/Workspace/SelectionManager.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw
{
	GameViewWindow::GameViewWindow()
	{
	}

	void GameViewWindow::draw()
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			EditorContext* pClosedContext = EditorContext::get();
			if ( pClosedContext != nullptr )
			{
				pClosedContext->setGameViewFocused( false );
				pClosedContext->setGameViewHovered( false );
			}
			ImGui::End();
			return;
		}

		EditorContext* pEditorContext = EditorContext::get();
		if ( pEditorContext == nullptr )
		{
			ImGui::End();
			return;
		}

		const bool	  bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
		const bool	  bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
		const float32 dt	   = ImGui::GetIO().DeltaTime;

		pEditorContext->setGameViewFocused( bFocused );
		pEditorContext->setGameViewHovered( bHovered );

		_viewportClient.update( dt, bFocused, bHovered );

		drawEditorTransportControls();
		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();
		_viewportClient.drawViewportToolbar( ImGui::GetContentRegionAvail().x );

		const ImVec2 size = ImGui::GetContentRegionAvail();
		if ( size.x > 1.0f && size.y > 1.0f )
		{
			const uint32			  wantW = static_cast<uint32>( MathUtil::round( size.x ) );
			const uint32			  wantH = static_cast<uint32>( MathUtil::round( size.y ) );
			const EditorGameView&	  view	= pEditorContext->getGameView();
			const int32				  dW	= static_cast<int32>( wantW ) - static_cast<int32>( view._width );
			const int32				  dH	= static_cast<int32>( wantH ) - static_cast<int32>( view._height );
			const bool bNeedResize = ( dW > 1 || dW < -1 || dH > 1 || dH < -1 ) && wantW > 0 && wantH > 0;
			if ( bNeedResize )
				pEditorContext->ensureGameViewSize( wantW, wantH );
		}

		const ImVec2 imagePos = ImGui::GetCursorScreenPos();
		_viewportClient.draw( pEditorContext->getGameView()._pTextureId, float2{ size.x, size.y } );

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

		ImGui::End();

		if ( size.x > 1.0f && size.y > 1.0f )
		{
			const float2 barAnchor{ imagePos.x + size.x * 0.5f, imagePos.y + 8.0f };
			drawEditorTransformBar( _viewportClient.getToolbarSettings(), barAnchor );
		}
	}
} // namespace sw
