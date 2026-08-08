/**
 * @file GameViewPanel.cpp
 */
#include "Panels/GameViewPanel.h"
#include "EditorAssetDrop.h"
#include "EditorSelection.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/SceneComponent.h"
#include "Core/Utility/Log/Logger.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <cmath>

namespace sw
{
	namespace
	{
		void setIdentity( float* m )
		{
			for ( int i = 0; i < 16; ++i )
				m[i] = 0.0f;
			m[0] = m[5] = m[10] = m[15] = 1.0f;
		}

		void lookAt( float* out, float eyeX, float eyeY, float eyeZ, float atX, float atY, float atZ )
		{
			float fx = atX - eyeX;
			float fy = atY - eyeY;
			float fz = atZ - eyeZ;
			float fl = std::sqrt( fx * fx + fy * fy + fz * fz );
			if ( fl > 1e-6f )
			{
				fx /= fl;
				fy /= fl;
				fz /= fl;
			}
			float sx = fy * 0.0f - fz * 1.0f;
			float sy = fz * 0.0f - fx * 0.0f;
			float sz = fx * 1.0f - fy * 0.0f;
			float sl = std::sqrt( sx * sx + sy * sy + sz * sz );
			if ( sl > 1e-6f )
			{
				sx /= sl;
				sy /= sl;
				sz /= sl;
			}
			const float ux = sy * fz - sz * fy;
			const float uy = sz * fx - sx * fz;
			const float uz = sx * fy - sy * fx;

			setIdentity( out );
			out[0]	= sx;
			out[4]	= sy;
			out[8]	= sz;
			out[1]	= ux;
			out[5]	= uy;
			out[9]	= uz;
			out[2]	= -fx;
			out[6]	= -fy;
			out[10] = -fz;
			out[12] = -( sx * eyeX + sy * eyeY + sz * eyeZ );
			out[13] = -( ux * eyeX + uy * eyeY + uz * eyeZ );
			out[14] = -( -fx * eyeX + -fy * eyeY + -fz * eyeZ );
		}

		void perspective( float* out, float fovYDeg, float aspect, float zNear, float zFar )
		{
			setIdentity( out );
			const float f = 1.0f / std::tan( fovYDeg * 0.5f * 3.14159265f / 180.0f );
			out[0]		  = f / aspect;
			out[5]		  = f;
			out[10]		  = ( zFar + zNear ) / ( zNear - zFar );
			out[11]		  = -1.0f;
			out[14]		  = ( 2.0f * zFar * zNear ) / ( zNear - zFar );
			out[15]		  = 0.0f;
		}
	} // namespace

	GameViewPanel::GameViewPanel()
	{
		lookAt( _view, 3.0f, 3.0f, 3.0f, 0.0f, 0.0f, 0.0f );
		perspective( _proj, 45.0f, 1.0f, 0.1f, 100.0f );
	}

	void GameViewPanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::RadioButton( "Translate", &_operation, 0 );
		ImGui::SameLine();
		ImGui::RadioButton( "Rotate", &_operation, 1 );
		ImGui::SameLine();
		ImGui::RadioButton( "Scale", &_operation, 2 );

		const ImVec2 canvasPos	= ImGui::GetCursorScreenPos();
		const ImVec2 size		= ImGui::GetContentRegionAvail();
		if ( size.x > 1.0f && size.y > 1.0f &&
			 ctx.requestGameViewportWidth != nullptr &&
			 ctx.requestGameViewportHeight != nullptr )
		{
			const uint32 wantW = static_cast<uint32>( std::lround( size.x ) );
			const uint32 wantH = static_cast<uint32>( std::lround( size.y ) );
			const int32	 dW	   = static_cast<int32>( wantW ) - static_cast<int32>( ctx.gameViewportWidth );
			const int32	 dH	   = static_cast<int32>( wantH ) - static_cast<int32>( ctx.gameViewportHeight );
			if ( ( dW > 1 || dW < -1 || dH > 1 || dH < -1 ) && wantW > 0 && wantH > 0 )
			{
				*ctx.requestGameViewportWidth  = wantW;
				*ctx.requestGameViewportHeight = wantH;
			}
		}

		if ( ctx.gameTextureID != nullptr )
		{
			if ( size.x > 0.0f && size.y > 0.0f )
				ImGui::Image( reinterpret_cast<ImTextureID>( ctx.gameTextureID ), size );
		}
		else
		{
			ImGui::InvisibleButton( "game_view_canvas", size );
			ImGui::GetWindowDrawList()->AddRectFilled(
				canvasPos,
				ImVec2( canvasPos.x + size.x, canvasPos.y + size.y ),
				IM_COL32( 28, 28, 32, 255 ) );
			ImGui::SetCursorScreenPos( canvasPos );
			ImGui::TextUnformatted( "Game RenderTarget is not available." );
		}

		const bool canvasHot = ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGuizmo::IsUsing();
		if ( size.x >= 64.0f && size.y >= 64.0f )
		{
			const float aspect = size.x / size.y;
			perspective( _proj, 45.0f, aspect, 0.1f, 100.0f );

			Scene*		scene = core::getSceneManager().getActiveScene();
			GameObject* obj	  = nullptr;
			if ( scene != nullptr && scene->getObjectManager() != nullptr && editor::selectedObjectId() != 0 )
				obj = scene->getObjectManager()->findGameObjectById( editor::selectedObjectId() );

			SceneComponent* root = obj != nullptr ? obj->getComponent<SceneComponent>() : nullptr;
			if ( root != nullptr )
			{
				float translation[3] = { root->getLocalPosition()._x, root->getLocalPosition()._y, root->getLocalPosition()._z };
				float rotation[3]	 = { root->getLocalRotation()._x, root->getLocalRotation()._y, root->getLocalRotation()._z };
				float scale[3]		 = { root->getLocalScale()._x, root->getLocalScale()._y, root->getLocalScale()._z };
				float matrix[16];
				ImGuizmo::RecomposeMatrixFromComponents( translation, rotation, scale, matrix );

				ImGuizmo::SetDrawlist( ImGui::GetWindowDrawList() );
				ImGuizmo::SetRect( canvasPos.x, canvasPos.y, size.x, size.y );
				ImGuizmo::SetOrthographic( false );
				ImGuizmo::Enable( canvasHot );

				ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
				if ( _operation == 1 )
					op = ImGuizmo::ROTATE;
				else if ( _operation == 2 )
					op = ImGuizmo::SCALE;

				if ( ImGuizmo::Manipulate( _view, _proj, op, ImGuizmo::LOCAL, matrix ) )
				{
					ImGuizmo::DecomposeMatrixToComponents( matrix, translation, rotation, scale );
					root->setLocalPosition( float3( translation[0], translation[1], translation[2] ) );
					root->setLocalRotation( float3( rotation[0], rotation[1], rotation[2] ) );
					root->setLocalScale( float3( scale[0], scale[1], scale[2] ) );
				}
			}
		}

		if ( ImGui::BeginDragDropTarget() )
		{
			if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "SW_ASSET_PATH" ) )
			{
				const char* path = static_cast<const char*>( payload->Data );
				if ( path != nullptr )
				{
					Scene* scene = core::getSceneManager().getActiveScene();
					if ( scene != nullptr && scene->getObjectManager() != nullptr )
					{
						GameObject* parent = scene->getObjectManager()->findGameObjectById( editor::selectedObjectId() );
						if ( parent != nullptr && editor::selectedComponentId() != 0 )
							parent = nullptr;

						if ( GameObject* spawned = editor::spawnPrefabFromAssetPath( scene->getObjectManager(), path, parent ) )
							editor::selectGameObject( spawned );
					}
					else
					{
						SW_LOG_INFO( "[GameView] Dropped asset (no active scene): %#", path );
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::End();
	}
} // namespace sw
