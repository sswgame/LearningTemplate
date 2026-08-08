/**
 * @file GameViewWindow.cpp
 */
#include "Windows/GameViewWindow.h"
#include "Workspace/EditorAssetDrop.h"
#include "Workspace/EditorWorkspace.h"
#include "Widgets/EditorWidgets.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/GameState.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/SceneComponent.h"
#include "Core/Utility/Log/Logger.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <cmath>
#include <limits>

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

		bool unprojectRay( float mx, float my, float canvasX, float canvasY, float canvasW, float canvasH,
						   float& outOx, float& outOy, float& outOz,
						   float& outDx, float& outDy, float& outDz )
		{
			if ( canvasW < 1.0f || canvasH < 1.0f )
				return false;

			const float nx = ( ( mx - canvasX ) / canvasW ) * 2.0f - 1.0f;
			const float ny = 1.0f - ( ( my - canvasY ) / canvasH ) * 2.0f;

			// Match GameViewWindow lookAt eye (3,3,3) ??origin; NDC offsets fan the ray.
			outOx = 3.0f;
			outOy = 3.0f;
			outOz = 3.0f;
			outDx = -3.0f + nx * 4.0f;
			outDy = -3.0f;
			outDz = -3.0f + ny * 4.0f;
			const float len = std::sqrt( outDx * outDx + outDy * outDy + outDz * outDz );
			if ( len > 1e-6f )
			{
				outDx /= len;
				outDy /= len;
				outDz /= len;
			}
			return true;
		}

		bool rayGroundY0( float ox, float oy, float oz, float dx, float dy, float dz, float& hx, float& hz )
		{
			if ( std::fabs( dy ) < 1e-6f )
				return false;
			const float t = -oy / dy;
			if ( t < 0.0f )
				return false;
			hx = ox + dx * t;
			hz = oz + dz * t;
			return true;
		}
	} // namespace

	GameViewWindow::GameViewWindow()
	{
		lookAt( _view, 3.0f, 3.0f, 3.0f, 0.0f, 0.0f, 0.0f );
		perspective( _proj, 45.0f, 1.0f, 0.1f, 100.0f );
	}

	uint64 GameViewWindow::pickNearestOnGround( float mouseX, float mouseY, float canvasX, float canvasY,
											   float canvasW, float canvasH ) const
	{
		Scene* scene = core::getSceneManager().getActiveScene();
		if ( scene == nullptr || scene->getObjectManager() == nullptr )
			return 0;

		float ox, oy, oz, dx, dy, dz;
		if ( unprojectRay( mouseX, mouseY, canvasX, canvasY, canvasW, canvasH, ox, oy, oz, dx, dy, dz ) == false )
			return 0;

		float hitX = 0.0f;
		float hitZ = 0.0f;
		if ( rayGroundY0( ox, oy, oz, dx, dy, dz, hitX, hitZ ) == false )
			return 0;

		uint64		bestId	 = 0;
		float		bestDist = std::numeric_limits<float>::max();
		const auto& objects	 = scene->getObjectManager()->getAllGameObjects();
		for ( GameObject* obj : objects )
		{
			if ( obj == nullptr )
				continue;
			SceneComponent* root = obj->getComponent<SceneComponent>();
			if ( root == nullptr )
				continue;
			const float3 p	  = root->getWorldPosition();
			const float	 dxz  = p._x - hitX;
			const float	 dzz  = p._z - hitZ;
			const float	 dist = dxz * dxz + dzz * dzz;
			if ( dist < bestDist )
			{
				bestDist = dist;
				bestId	 = obj->getObjectId();
			}
		}

		// Reject if too far from click on ground
		if ( bestDist > 4.0f * 4.0f )
			return 0;
		return bestId;
	}

	void GameViewWindow::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		int32& op = editor::gizmoOperation();
		ImGui::RadioButton( "Translate", &op, 0 );
		ImGui::SameLine();
		ImGui::RadioButton( "Rotate", &op, 1 );
		ImGui::SameLine();
		ImGui::RadioButton( "Scale", &op, 2 );
		ImGui::SameLine();
		bool& bLocal = editor::gizmoLocalSpace();
		ImGui::Checkbox( "Local", &bLocal );
		{
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
		}
		if ( _hoverObjectId != 0 )
		{
			ImGui::SameLine();
			ImGui::TextDisabled( "Hover: %llu", static_cast<unsigned long long>( _hoverObjectId ) );
		}

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

			if ( ImGui::IsItemHovered() && ImGuizmo::IsUsing() == false && ImGuizmo::IsOver() == false )
			{
				const ImVec2 mouse = ImGui::GetMousePos();
				_hoverObjectId	   = pickNearestOnGround( mouse.x, mouse.y, canvasPos.x, canvasPos.y, size.x, size.y );
				if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && _hoverObjectId != 0 )
				{
					Scene* scene = core::getSceneManager().getActiveScene();
					if ( scene != nullptr && scene->getObjectManager() != nullptr )
					{
						if ( GameObject* obj = scene->getObjectManager()->findGameObjectById( _hoverObjectId ) )
							editor::selectGameObject( obj );
					}
				}
			}
			else if ( ImGui::IsItemHovered() == false )
			{
				_hoverObjectId = 0;
			}

			// Hover highlight ring in screen-ish overlay
			if ( _hoverObjectId != 0 && editor::selectedObjectId() != _hoverObjectId )
			{
				Scene* scene = core::getSceneManager().getActiveScene();
				if ( scene != nullptr && scene->getObjectManager() != nullptr )
				{
					if ( GameObject* hover = scene->getObjectManager()->findGameObjectById( _hoverObjectId ) )
					{
						if ( SceneComponent* root = hover->getComponent<SceneComponent>() )
						{
							const float3 p = root->getWorldPosition();
							ImGui::GetWindowDrawList()->AddText(
								ImVec2( canvasPos.x + 8.0f, canvasPos.y + 8.0f ),
								IM_COL32( 255, 220, 120, 220 ),
								hover->getName().c_str() );
							(void)p;
						}
					}
				}
			}

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

				ImGuizmo::OPERATION gizmoOp = ImGuizmo::TRANSLATE;
				if ( editor::gizmoOperation() == 1 )
					gizmoOp = ImGuizmo::ROTATE;
				else if ( editor::gizmoOperation() == 2 )
					gizmoOp = ImGuizmo::SCALE;
				const ImGuizmo::MODE gizmoMode = editor::gizmoLocalSpace() ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

				if ( ImGuizmo::Manipulate( _view, _proj, gizmoOp, gizmoMode, matrix ) )
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
