#include "pch.h"

#include "Editor/Viewport/EditorViewportClient.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <algorithm>

namespace sw::editor
{
	namespace
	{
		void setIdentity( float32* pM )
		{
			for ( int32 matrixIndex = 0; matrixIndex < 16; ++matrixIndex )
				pM[matrixIndex] = 0.0f;
			pM[0] = pM[5] = pM[10] = pM[15] = 1.0f;
		}

		void lookAt( float32* pOut, float32 eyeX, float32 eyeY, float32 eyeZ, float32 atX, float32 atY, float32 atZ )
		{
			float32 fx = atX - eyeX;
			float32 fy = atY - eyeY;
			float32 fz = atZ - eyeZ;
			float32 fl = MathUtil::sqrt( fx * fx + fy * fy + fz * fz );
			if ( fl > 1e-6f )
			{
				fx /= fl;
				fy /= fl;
				fz /= fl;
			}
			float32 sx = fy * 0.0f - fz * 1.0f;
			float32 sy = fz * 0.0f - fx * 0.0f;
			float32 sz = fx * 1.0f - fy * 0.0f;
			float32 sl = MathUtil::sqrt( sx * sx + sy * sy + sz * sz );
			if ( sl > 1e-6f )
			{
				sx /= sl;
				sy /= sl;
				sz /= sl;
			}
			const float32 ux = sy * fz - sz * fy;
			const float32 uy = sz * fx - sx * fz;
			const float32 uz = sx * fy - sy * fx;

			setIdentity( pOut );
			pOut[0]	 = sx;
			pOut[4]	 = sy;
			pOut[8]	 = sz;
			pOut[1]	 = ux;
			pOut[5]	 = uy;
			pOut[9]	 = uz;
			pOut[2]	 = -fx;
			pOut[6]	 = -fy;
			pOut[10] = -fz;
			pOut[12] = -( sx * eyeX + sy * eyeY + sz * eyeZ );
			pOut[13] = -( ux * eyeX + uy * eyeY + uz * eyeZ );
			pOut[14] = -( -fx * eyeX + -fy * eyeY + -fz * eyeZ );
		}

		void perspective( float32* pOut, float32 fovYDeg, float32 aspect, float32 zNear, float32 zFar )
		{
			setIdentity( pOut );
			const float32 f = 1.0f / MathUtil::tan( MathUtil::toRadian( fovYDeg * 0.5f ) );
			pOut[0]			= f / aspect;
			pOut[5]			= f;
			pOut[10]		= ( zFar + zNear ) / ( zNear - zFar );
			pOut[11]		= -1.0f;
			pOut[14]		= ( 2.0f * zFar * zNear ) / ( zNear - zFar );
			pOut[15]		= 0.0f;
		}

		void storeColumnMajor( float32* pOut, const float4x4& matrix )
		{
			const float4x4 columnMajor = matrix.transpose();
			Memory::copy( pOut, &columnMajor, sizeof( float32 ) * 16 );
		}

		void loadColumnMajor( float4x4& outMatrix, const float32* pIn )
		{
			float4x4 columnMajor{};
			Memory::copy( &columnMajor, pIn, sizeof( float32 ) * 16 );
			outMatrix = columnMajor.transpose();
		}

		bool unproject( const float4x4& invViewProj, float32 ndcX, float32 ndcY, float32 ndcZ, float3& outWorld )
		{
			const float4 clip{ ndcX, ndcY, ndcZ, 1.0f };
			const float4 world = float4::transform( clip, invViewProj );
			if ( MathUtil::abs( world._w ) < 1e-8f )
				return false;
			outWorld = float3{ world._x / world._w, world._y / world._w, world._z / world._w };
			return true;
		}

		bool rayHitsSphere( const float3& origin, const float3& dir, const float3& center, float32 radius, float32& outT )
		{
			const float3  m		= origin - center;
			const float32 b		= m.dot( dir );
			const float32 c		= m.dot( m ) - radius * radius;
			const bool	  bAway = ( c > 0.0f && b > 0.0f );
			if ( bAway )
				return false;

			const float32 discr = b * b - c;
			if ( discr < 0.0f )
				return false;

			const float32 sqrtDiscr = MathUtil::sqrt( discr );
			float32		  hitT		= -b - sqrtDiscr;
			if ( hitT < 0.0f )
				hitT = -b + sqrtDiscr;
			if ( hitT < 0.0f )
				return false;

			outT = hitT;
			return true;
		}

		CameraComponent* getGameViewCamera()
		{
			SceneManager* pSceneManager = editor::getService<SceneManager>();
			if ( pSceneManager == nullptr )
				return nullptr;
			Scene* pScene = pSceneManager->getActiveScene();
			if ( pScene == nullptr )
				return nullptr;
			pScene->ensureDefaultCameras();
			return pScene->getActiveRenderCamera( true );
		}

		bool projectPointToScreen( const float4x4& viewProj, const float3& worldPt, const float2& canvasPos,
								   const float2& canvasSize, ImVec2& outScreenPt )
		{
			const float32 w = worldPt._x * viewProj._14 + worldPt._y * viewProj._24 + worldPt._z * viewProj._34 + viewProj._44;
			if ( w <= 0.001f )
				return false;
			const float32 invW = 1.0f / w;
			const float32 x	   = ( worldPt._x * viewProj._11 + worldPt._y * viewProj._21 + worldPt._z * viewProj._31 + viewProj._41 ) * invW;
			const float32 y	   = ( worldPt._x * viewProj._12 + worldPt._y * viewProj._22 + worldPt._z * viewProj._32 + viewProj._42 ) * invW;

			outScreenPt.x = canvasPos._x + ( x * 0.5f + 0.5f ) * canvasSize._x;
			outScreenPt.y = canvasPos._y + ( 1.0f - ( y * 0.5f + 0.5f ) ) * canvasSize._y;
			return true;
		}

		void drawDebugVisualizers( ImDrawList* pDrawList, const float4x4& viewProj, const float2& canvasPos,
								   const float2& canvasSize, const ViewportToolbarSettings& settings,
								   CameraComponent* pActiveCamera )
		{
			if ( pDrawList == nullptr )
				return;

			SceneManager* pSceneManager = editor::getService<SceneManager>();
			if ( pSceneManager == nullptr || pSceneManager->getActiveScene() == nullptr )
				return;
			GameObjectManager* pManager = pSceneManager->getActiveScene()->getObjectManager();
			if ( pManager == nullptr )
				return;

			const vector<GameObject*>& listObjects = pManager->getAllGameObjects();

			// 1) BoxCollider2D 와이어프레임 렌더링
			if ( settings._bShowColliders )
			{
				constexpr ImU32 colWire = IM_COL32( 60, 230, 80, 220 );
				for ( GameObject* pObj : listObjects )
				{
					if ( pObj == nullptr || pObj->isActive() == false )
						continue;
					BoxCollider2DComponent* pBox = pObj->getComponent<BoxCollider2DComponent>();
					if ( pBox == nullptr || pBox->isActive() == false )
						continue;

					const float2  offsetPos = pBox->getOffsetPosition();
					const float2  offsetScl = pBox->getOffsetScaleVec();
					const float3  center	= pBox->getWorldPosition() + float3{ offsetPos._x, offsetPos._y, 0.0f };
					const float32 hx		= MathUtil::max( offsetScl._x * 0.5f, 0.05f );
					const float32 hy		= MathUtil::max( offsetScl._y * 0.5f, 0.05f );

					const float3 p0{ center._x - hx, center._y - hy, center._z };
					const float3 p1{ center._x + hx, center._y - hy, center._z };
					const float3 p2{ center._x + hx, center._y + hy, center._z };
					const float3 p3{ center._x - hx, center._y + hy, center._z };

					ImVec2 s0, s1, s2, s3;
					if ( projectPointToScreen( viewProj, p0, canvasPos, canvasSize, s0 ) &&
						 projectPointToScreen( viewProj, p1, canvasPos, canvasSize, s1 ) &&
						 projectPointToScreen( viewProj, p2, canvasPos, canvasSize, s2 ) &&
						 projectPointToScreen( viewProj, p3, canvasPos, canvasSize, s3 ) )
					{
						pDrawList->AddLine( s0, s1, colWire, 1.5f );
						pDrawList->AddLine( s1, s2, colWire, 1.5f );
						pDrawList->AddLine( s2, s3, colWire, 1.5f );
						pDrawList->AddLine( s3, s0, colWire, 1.5f );
					}
				}
			}

			// 2) CameraComponent Frustum 와이어프레임 렌더링
			if ( settings._bShowCameras )
			{
				constexpr ImU32 colCamWire = IM_COL32( 60, 200, 255, 200 );
				for ( GameObject* pObj : listObjects )
				{
					if ( pObj == nullptr || pObj->isActive() == false )
						continue;
					CameraComponent* pCam = pObj->getComponent<CameraComponent>();
					if ( pCam == nullptr || pCam == pActiveCamera || pCam->isActive() == false )
						continue;

					const float4x4 camWorld = pCam->getWorldMatrix();
					const float3   eye		= float3{ camWorld._41, camWorld._42, camWorld._43 };
					const float3   rgt		= float3{ camWorld._11, camWorld._12, camWorld._13 };
					const float3   up		= float3{ camWorld._21, camWorld._22, camWorld._23 };
					const float3   fwd		= float3{ camWorld._31, camWorld._32, camWorld._33 };

					const float3 nearCenter = eye + fwd * 1.0f;
					const float3 p0			= nearCenter - rgt * 0.6f - up * 0.4f;
					const float3 p1			= nearCenter + rgt * 0.6f - up * 0.4f;
					const float3 p2			= nearCenter + rgt * 0.6f + up * 0.4f;
					const float3 p3			= nearCenter - rgt * 0.6f + up * 0.4f;

					ImVec2 sEye, s0, s1, s2, s3;
					if ( projectPointToScreen( viewProj, eye, canvasPos, canvasSize, sEye ) &&
						 projectPointToScreen( viewProj, p0, canvasPos, canvasSize, s0 ) &&
						 projectPointToScreen( viewProj, p1, canvasPos, canvasSize, s1 ) &&
						 projectPointToScreen( viewProj, p2, canvasPos, canvasSize, s2 ) &&
						 projectPointToScreen( viewProj, p3, canvasPos, canvasSize, s3 ) )
					{
						pDrawList->AddLine( sEye, s0, colCamWire, 1.2f );
						pDrawList->AddLine( sEye, s1, colCamWire, 1.2f );
						pDrawList->AddLine( sEye, s2, colCamWire, 1.2f );
						pDrawList->AddLine( sEye, s3, colCamWire, 1.2f );
						pDrawList->AddLine( s0, s1, colCamWire, 1.2f );
						pDrawList->AddLine( s1, s2, colCamWire, 1.2f );
						pDrawList->AddLine( s2, s3, colCamWire, 1.2f );
						pDrawList->AddLine( s3, s0, colCamWire, 1.2f );
					}
				}
			}
		}
	} // namespace

	void EditorViewportClient::drawStatsOverlay( ImDrawList* pDrawList, const float2& canvasPos,
												 const float2& canvasSize )
	{
		if ( pDrawList == nullptr )
			return;

		const float32 fps		  = ImGui::GetIO().Framerate;
		const float32 frameTimeMs = ( fps > 0.0f ) ? ( 1000.0f / fps ) : 0.0f;

		SceneManager*	   pSceneManager = editor::getService<SceneManager>();
		Scene*			   pScene		 = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
		GameObjectManager* pManager		 = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
		const uint32	   totalObjects	 = ( pManager != nullptr )
											 ? static_cast<uint32>( pManager->getAllGameObjects().size() )
											 : 0;

		constexpr float32 overlayW = 160.0f;
		constexpr float32 overlayH = 72.0f;
		const float32	  x0	   = canvasPos._x + canvasSize._x - overlayW - 12.0f;
		const float32	  y0	   = canvasPos._y + 12.0f;
		const float32	  x1	   = x0 + overlayW;
		const float32	  y1	   = y0 + overlayH;

		// Background & Border
		pDrawList->AddRectFilled( ImVec2( x0, y0 ), ImVec2( x1, y1 ), IM_COL32( 15, 17, 22, 210 ), 6.0f );
		pDrawList->AddRect( ImVec2( x0, y0 ), ImVec2( x1, y1 ), IM_COL32( 50, 60, 80, 180 ), 6.0f );

		// Text lines
		utf8 arrFps[32];
		formatstring( arrFps, sizeof( arrFps ), "FPS: %.1f (%.2f ms)", static_cast<float64>( fps ),
					  static_cast<float64>( frameTimeMs ) );
		pDrawList->AddText( ImVec2( x0 + 10.0f, y0 + 8.0f ), IM_COL32( 80, 230, 120, 240 ), arrFps );

		utf8 arrObjs[32];
		formatstring( arrObjs, sizeof( arrObjs ), "Objects: %u", totalObjects );
		pDrawList->AddText( ImVec2( x0 + 10.0f, y0 + 28.0f ), IM_COL32( 210, 215, 230, 230 ), arrObjs );

		utf8 arrRes[32];
		formatstring( arrRes, sizeof( arrRes ), "Res: %.0fx%.0f", static_cast<float64>( canvasSize._x ),
					  static_cast<float64>( canvasSize._y ) );
		pDrawList->AddText( ImVec2( x0 + 10.0f, y0 + 48.0f ), IM_COL32( 140, 160, 190, 220 ), arrRes );
	}

	EditorViewportClient::EditorViewportClient()
		: _cameraPos{ 0.0f, 3.0f, -6.0f }
		, _cameraRot{ 20.0f, 0.0f, 0.0f }
		, _orbitTarget{ 0.0f, 0.0f, 0.0f }
		, _rulerStartWorld{ 0.0f, 0.0f, 0.0f }
		, _rulerEndWorld{ 0.0f, 0.0f, 0.0f }
		, _orbitDistance{ 8.0f }
		, _fovY{ 60.0f }
		, _nearZ{ 0.1f }
		, _farZ{ 1000.0f }
		, _cameraMode{ CameraControlMode::Fly }
		, _toolbarSettings{}
		, _bRulerActive{ false }
	{
	}

	void EditorViewportClient::getViewMatrix( float32* pOutMatrix ) const
	{
		const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
		const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );

		const float32 forwardX = MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad );
		const float32 forwardY = -MathUtil::sin( pitchRad );
		const float32 forwardZ = MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad );

		const float3 target = float3{ _cameraPos._x + forwardX, _cameraPos._y + forwardY, _cameraPos._z + forwardZ };
		lookAt( pOutMatrix, _cameraPos._x, _cameraPos._y, _cameraPos._z, target._x, target._y, target._z );
	}

	void EditorViewportClient::getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const
	{
		perspective( pOutMatrix, _fovY, aspect > 0.001f ? aspect : 1.0f, _nearZ, _farZ );
	}

	void EditorViewportClient::update( float32 deltaTime, bool bWindowFocused, bool bWindowHovered )
	{
		if ( bWindowHovered || bWindowFocused )
		{
			ImGuiIO& io = ImGui::GetIO();

			if ( _toolbarSettings._requestedBookmarkSlot >= 0 )
			{
				EditorContext* pContext = EditorContext::get();
				if ( pContext != nullptr )
				{
					const CameraBookmark* pBm = pContext->getWorkspace().getCameraBookmark(
						static_cast<uint32>( _toolbarSettings._requestedBookmarkSlot ) );
					if ( pBm != nullptr && pBm->_bValid )
					{
						_cameraPos	   = pBm->_position;
						_cameraRot	   = pBm->_rotation;
						_orbitTarget   = pBm->_orbitTarget;
						_orbitDistance = pBm->_orbitDistance;
					}
				}
				_toolbarSettings._requestedBookmarkSlot = -1;
			}

			if ( io.WantTextInput == false )
			{
				for ( int32 keyIndex = 0; keyIndex < 9; ++keyIndex )
				{
					const ImGuiKey key = static_cast<ImGuiKey>( ImGuiKey_1 + keyIndex );
					if ( ImGui::IsKeyPressed( key, false ) )
					{
						EditorContext* pContext = EditorContext::get();
						if ( pContext != nullptr )
						{
							if ( io.KeyCtrl )
							{
								CameraBookmark bm{};
								bm._position	  = _cameraPos;
								bm._rotation	  = _cameraRot;
								bm._orbitTarget	  = _orbitTarget;
								bm._orbitDistance = _orbitDistance;
								utf8 arrName[32];
								formatstring( arrName, sizeof( arrName ), "POI %d", keyIndex + 1 );
								bm._name = arrName;
								pContext->getWorkspace().setCameraBookmark( static_cast<uint32>( keyIndex ), bm );
							}
							else if ( io.KeyAlt == false && io.KeyShift == false )
							{
								const CameraBookmark* pBm = pContext->getWorkspace().getCameraBookmark(
									static_cast<uint32>( keyIndex ) );
								if ( pBm != nullptr && pBm->_bValid )
								{
									_cameraPos	   = pBm->_position;
									_cameraRot	   = pBm->_rotation;
									_orbitTarget   = pBm->_orbitTarget;
									_orbitDistance = pBm->_orbitDistance;
								}
							}
						}
					}
				}

				if ( ImGui::IsKeyPressed( ImGuiKey_F, false ) && io.KeyCtrl == false && io.KeyAlt == false )
				{
					frameSelected();
				}
			}

			if ( io.KeyAlt )
			{
				_cameraMode = CameraControlMode::Orbit;
				processOrbitInput();
			}
			else if ( io.MouseDown[1] )
			{
				_cameraMode = CameraControlMode::Fly;
				processFlyInput( deltaTime );
			}
		}

		CameraComponent* pCam = getGameViewCamera();
		if ( pCam != nullptr )
		{
			pCam->setLocalPosition( _cameraPos );
			const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
			const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );
			const float3  forward{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
								   MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
			const float3  target{ _cameraPos._x + forward._x, _cameraPos._y + forward._y, _cameraPos._z + forward._z };
			pCam->lookAt( target );
		}
	}

	void EditorViewportClient::processFlyInput( float32 deltaTime )
	{
		ImGuiIO& io = ImGui::GetIO();

		// 회전 (마우스 델타)
		_cameraRot._y += io.MouseDelta.x * 0.2f;
		_cameraRot._x += io.MouseDelta.y * 0.2f;
		_cameraRot._x = MathUtil::clamp( _cameraRot._x, -89.0f, 89.0f );

		// 이동 (WASD + QE)
		const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
		const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );

		const float3 forward = float3{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
									   MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
		const float3 right	 = float3{ MathUtil::cos( yawRad ), 0.0f, -MathUtil::sin( yawRad ) };
		const float3 up		 = float3{ 0.0f, 1.0f, 0.0f };

		float3 moveDir{ 0.0f, 0.0f, 0.0f };
		if ( ImGui::IsKeyDown( ImGuiKey_W ) )
			moveDir = float3{ moveDir._x + forward._x, moveDir._y + forward._y, moveDir._z + forward._z };
		if ( ImGui::IsKeyDown( ImGuiKey_S ) )
			moveDir = float3{ moveDir._x - forward._x, moveDir._y - forward._y, moveDir._z - forward._z };
		if ( ImGui::IsKeyDown( ImGuiKey_D ) )
			moveDir = float3{ moveDir._x + right._x, moveDir._y + right._y, moveDir._z + right._z };
		if ( ImGui::IsKeyDown( ImGuiKey_A ) )
			moveDir = float3{ moveDir._x - right._x, moveDir._y - right._y, moveDir._z - right._z };
		if ( ImGui::IsKeyDown( ImGuiKey_E ) )
			moveDir = float3{ moveDir._x + up._x, moveDir._y + up._y, moveDir._z + up._z };
		if ( ImGui::IsKeyDown( ImGuiKey_Q ) )
			moveDir = float3{ moveDir._x - up._x, moveDir._y - up._y, moveDir._z - up._z };

		const float32 speed = _toolbarSettings._cameraSpeed * ( io.KeyShift ? 3.0f : 1.0f ) * deltaTime;
		_cameraPos			= float3{ _cameraPos._x + moveDir._x * speed, _cameraPos._y + moveDir._y * speed,
							  _cameraPos._z + moveDir._z * speed };
	}

	void EditorViewportClient::processOrbitInput()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Alt + LMB: Orbit Rotate
		if ( io.MouseDown[0] )
		{
			_cameraRot._y += io.MouseDelta.x * 0.3f;
			_cameraRot._x += io.MouseDelta.y * 0.3f;
			_cameraRot._x = MathUtil::clamp( _cameraRot._x, -89.0f, 89.0f );
		}

		// Alt + RMB 또는 휠: Orbit Zoom
		if ( io.MouseDown[1] )
		{
			_orbitDistance += ( io.MouseDelta.x - io.MouseDelta.y ) * 0.05f;
			if ( _orbitDistance < 0.5f )
				_orbitDistance = 0.5f;
		}

		if ( MathUtil::abs( io.MouseWheel ) > 0.01f )
		{
			_orbitDistance -= io.MouseWheel * 1.0f;
			if ( _orbitDistance < 0.5f )
				_orbitDistance = 0.5f;
		}

		const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
		const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );

		const float3 forward = float3{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
									   MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };

		_cameraPos = float3{ _orbitTarget._x - forward._x * _orbitDistance, _orbitTarget._y - forward._y * _orbitDistance,
							 _orbitTarget._z - forward._z * _orbitDistance };
	}

	void EditorViewportClient::drawViewportToolbar( float32 viewportWidth )
	{
		EditorViewportToolbar::draw( _toolbarSettings, viewportWidth );
	}

	void EditorViewportClient::drawTransformBar( const float2& anchorPos )
	{
		const bool bHasSelection = EditorContext::get()->getSelectionManager().getSelectedObjectCount() > 0;
		EditorViewportToolbar::drawTransformBar( _toolbarSettings, anchorPos, bHasSelection );
	}

	void EditorViewportClient::draw( const void* pTextureId, const float2& canvasSize )
	{
		const ImVec2 imagePos = ImGui::GetCursorScreenPos();
		if ( pTextureId != nullptr )
			ImGui::Image( reinterpret_cast<ImTextureID>( pTextureId ), ImVec2{ canvasSize._x, canvasSize._y } );
		else
			ImGui::Dummy( ImVec2{ canvasSize._x, canvasSize._y } );

		CameraComponent* pCamera = getGameViewCamera();
		const float2	 canvasPos{ imagePos.x, imagePos.y };

		if ( pCamera != nullptr )
		{
			const float32  aspect	= canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
			const float4x4 viewProj = pCamera->getViewProjectionMatrix( aspect );

			float32 arrView[16];
			float32 arrProj[16];
			storeColumnMajor( arrView, pCamera->getViewMatrix() );
			storeColumnMajor( arrProj, pCamera->getProjectionMatrix( aspect ) );

			if ( _toolbarSettings._bShowGrid )
				drawAdaptiveGrid( ImGui::GetWindowDrawList(), canvasPos, canvasSize, arrView, arrProj );

			drawDebugVisualizers( ImGui::GetWindowDrawList(), viewProj, canvasPos, canvasSize, _toolbarSettings,
								  pCamera );

			processRulerTool( ImGui::GetWindowDrawList(), canvasPos, canvasSize, arrView, arrProj );

			processPicking( canvasPos, canvasSize, pCamera );

			GameObjectPtr pPrimary = EditorContext::get()->getSelectionManager().getPrimaryObject();
			if ( pPrimary.isValid() )
			{
				SceneManager* pSceneManager = editor::getService<SceneManager>();
				if ( pSceneManager != nullptr )
				{
					Scene* pScene = pSceneManager->getActiveScene();
					if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
						pScene->getObjectManager()->flushSceneTransforms();
				}

				drawGizmo( pPrimary, arrView, arrProj, canvasPos, canvasSize );
			}

			if ( _toolbarSettings._bShowStats )
				drawStatsOverlay( ImGui::GetWindowDrawList(), canvasPos, canvasSize );

			if ( _toolbarSettings._bShowOrientationCube )
				drawOrientationCube( ImGui::GetWindowDrawList(), canvasPos, canvasSize );

			if ( ImGui::BeginDragDropTarget() )
			{
				const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( "SW_ASSET_PATH" );
				if ( pPayload != nullptr )
				{
					const utf8* pAssetPath = static_cast<const utf8*>( pPayload->Data );
					if ( pAssetPath != nullptr )
					{
						handleViewportAssetDrop( pAssetPath, canvasPos, canvasSize, arrView, arrProj );
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
	}

	void EditorViewportClient::processPicking( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera )
	{
		if ( pCamera == nullptr )
			return;
		if ( ImGui::IsItemClicked( ImGuiMouseButton_Left ) == false )
			return;
		if ( ImGuizmo::IsOver() || ImGuizmo::IsUsing() )
			return;
		if ( ImGui::GetIO().KeyAlt )
			return;
		if ( canvasSize._x <= 1.0f || canvasSize._y <= 1.0f )
			return;

		const ImVec2  mouse = ImGui::GetIO().MousePos;
		const float32 u		= ( mouse.x - canvasPos._x ) / canvasSize._x;
		const float32 v		= ( mouse.y - canvasPos._y ) / canvasSize._y;
		const bool	  bInside =
			( u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f );
		if ( bInside == false )
			return;

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr )
			return;
		Scene* pScene = pSceneManager->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
			return;
		GameObjectManager* pManager = pScene->getObjectManager();
		pManager->flushSceneTransforms();

		const float32  aspect	   = canvasSize._x / canvasSize._y;
		const float4x4 invViewProj = pCamera->getViewProjectionMatrix( aspect ).invert();
		const float32  ndcX		   = u * 2.0f - 1.0f;
		const float32  ndcY		   = 1.0f - v * 2.0f;

		float3 nearPt{};
		float3 farPt{};
		if ( unproject( invViewProj, ndcX, ndcY, 0.0f, nearPt ) == false )
			return;
		if ( unproject( invViewProj, ndcX, ndcY, 1.0f, farPt ) == false )
			return;

		float3		  dir	 = farPt - nearPt;
		const float32 dirLen = dir.getLength();
		if ( dirLen < 1e-8f )
			return;
		dir = dir * ( 1.0f / dirLen );

		GameObject* pBestObj{ nullptr };
		Component*	pBestComp{ nullptr };
		float32		bestT{ MathUtil::MaxFloat };

		const vector<GameObject*> listObjects = pManager->getAllGameObjects();
		for ( GameObject* pObj : listObjects )
		{
			if ( pObj == nullptr || pObj->isActive() == false )
				continue;

			// 1) 3D MeshComponent
			MeshComponent* pMeshComp = pObj->getComponent<MeshComponent>();
			if ( pMeshComp != nullptr && pMeshComp->isActive() && pMeshComp->isVisible() )
			{
				const float3  scale	   = pMeshComp->getLocalScale();
				const float32 absX	   = MathUtil::abs( scale._x );
				const float32 absY	   = MathUtil::abs( scale._y );
				const float32 absZ	   = MathUtil::abs( scale._z );
				const float32 maxScale = MathUtil::max( absX, MathUtil::max( absY, absZ ) );
				const float32 radius   = pMeshComp->getBoundsRadius() * MathUtil::max( maxScale, 0.001f );
				const float3  center   = pMeshComp->getWorldPosition();
				float32		  hitT{ 0.0f };
				if ( rayHitsSphere( nearPt, dir, center, radius, hitT ) && hitT < bestT )
				{
					bestT	  = hitT;
					pBestObj  = pObj;
					pBestComp = pMeshComp;
					continue;
				}
			}

			// 2) 2D SpriteComponent
			SpriteComponent* pSpriteComp = pObj->getComponent<SpriteComponent>();
			if ( pSpriteComp != nullptr && pSpriteComp->isActive() )
			{
				const float3  scale	 = pSpriteComp->getLocalScale();
				const float32 absX	 = MathUtil::abs( scale._x );
				const float32 absY	 = MathUtil::abs( scale._y );
				const float32 radius = MathUtil::max( absX, absY ) * 0.7f + 0.1f;
				const float3  center = pSpriteComp->getWorldPosition();
				float32		  hitT{ 0.0f };
				if ( rayHitsSphere( nearPt, dir, center, radius, hitT ) && hitT < bestT )
				{
					bestT	  = hitT;
					pBestObj  = pObj;
					pBestComp = pSpriteComp;
					continue;
				}
			}

			// 3) 2D BoxCollider2DComponent
			BoxCollider2DComponent* pBoxComp = pObj->getComponent<BoxCollider2DComponent>();
			if ( pBoxComp != nullptr && pBoxComp->isActive() )
			{
				const float2  offsetPos = pBoxComp->getOffsetPosition();
				const float2  offsetScl = pBoxComp->getOffsetScaleVec();
				const float3  center	= pBoxComp->getWorldPosition() + float3{ offsetPos._x, offsetPos._y, 0.0f };
				const float32 radius	= offsetScl.getLength() * 0.5f + 0.1f;
				float32		  hitT{ 0.0f };
				if ( rayHitsSphere( nearPt, dir, center, radius, hitT ) && hitT < bestT )
				{
					bestT	  = hitT;
					pBestObj  = pObj;
					pBestComp = pBoxComp;
					continue;
				}
			}

			// 4) Fallback: SceneComponent
			SceneComponent* pSceneComp = pObj->getPrimarySceneComponent();
			if ( pSceneComp != nullptr && pSceneComp->isActive() )
			{
				const float3  center = pSceneComp->getWorldPosition();
				const float32 radius = 0.35f;
				float32		  hitT{ 0.0f };
				if ( rayHitsSphere( nearPt, dir, center, radius, hitT ) && hitT < bestT )
				{
					bestT	  = hitT;
					pBestObj  = pObj;
					pBestComp = pSceneComp;
				}
			}
		}

		if ( pBestObj != nullptr )
			EditorContext::get()->getWorkspace().selectComponent( GameObjectPtr{ pBestObj }, ComponentPtr{ pBestComp } );
		else
			EditorContext::get()->getWorkspace().clearSelection();
	}

	void EditorViewportClient::drawGizmo( GameObjectPtr pObj, const float32* pView, const float32* pProj,
										  const float2& canvasPos, const float2& canvasSize )
	{
		GameObject* pRaw = pObj.get();
		if ( pRaw == nullptr )
			return;

		SceneComponent* pSceneComp = pRaw->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect( canvasPos._x, canvasPos._y, canvasSize._x, canvasSize._y );

		float32 arrMatrix[16];
		storeColumnMajor( arrMatrix, pSceneComp->getWorldMatrix() );

		EditorWorkspace&	ws	  = EditorContext::get()->getWorkspace();
		const int32			opInt = ws.getGizmoOperation();
		ImGuizmo::OPERATION op	  = ImGuizmo::TRANSLATE;
		if ( opInt == 1 )
			op = ImGuizmo::ROTATE;
		else if ( opInt == 2 )
			op = ImGuizmo::SCALE;

		const ImGuizmo::MODE mode = ws.isGizmoLocalSpace() ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

		float32 arrSnap[3] = { 0.0f, 0.0f, 0.0f };
		if ( op == ImGuizmo::TRANSLATE && _toolbarSettings._bGridSnap )
			arrSnap[0] = arrSnap[1] = arrSnap[2] = _toolbarSettings._gridSnapValue;
		else if ( op == ImGuizmo::ROTATE && _toolbarSettings._bRotationSnap )
			arrSnap[0] = arrSnap[1] = arrSnap[2] = _toolbarSettings._rotationSnapValue;
		else if ( op == ImGuizmo::SCALE && _toolbarSettings._bScaleSnap )
			arrSnap[0] = arrSnap[1] = arrSnap[2] = _toolbarSettings._scaleSnapValue;

		const bool bUseSnap = ( arrSnap[0] > 0.0f );
		if ( ImGuizmo::Manipulate( pView, pProj, op, mode, arrMatrix, nullptr, bUseSnap ? arrSnap : nullptr ) )
		{
			float4x4 newWorldMat{};
			loadColumnMajor( newWorldMat, arrMatrix );

			float4x4		localMat  = newWorldMat;
			SceneComponent* pParentSc = pSceneComp->getParent();
			if ( pParentSc != nullptr )
				localMat = newWorldMat * pParentSc->getWorldMatrix().invert();

			storeColumnMajor( arrMatrix, localMat );

			float3 translation{};
			float3 rotationDeg{};
			float3 scale{};
			ImGuizmo::DecomposeMatrixToComponents( arrMatrix, &translation._x, &rotationDeg._x, &scale._x );

			if ( op == ImGuizmo::TRANSLATE && _toolbarSettings._bSurfaceSnap )
			{
				// Raycast downwards from object position to find top of ground or other colliders
				float3 rayStart = translation;
				rayStart._y += 1.0f;

				float32 hitY = 0.0f;
				bool	bHit = false;

				SceneManager* pSceneManager = editor::getService<SceneManager>();
				Scene*		  pScene		= ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
				if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
				{
					const vector<GameObject*>& listAll = pScene->getObjectManager()->getAllGameObjects();
					for ( const GameObject* pOther : listAll )
					{
						if ( pOther == nullptr || pOther == pRaw )
							continue;

						BoxCollider2DComponent* pOtherBox = pOther->getComponent<BoxCollider2DComponent>();
						if ( pOtherBox != nullptr && pOtherBox->isActive() )
						{
							const float3  otherPos = pOtherBox->getWorldPosition();
							const float2  otherScl = pOtherBox->getOffsetScaleVec();
							const float32 topY	   = otherPos._y + otherScl._y * 0.5f;
							if ( topY <= rayStart._y && ( topY > hitY || bHit == false ) )
							{
								const float32 halfW = otherScl._x * 0.5f;
								if ( otherPos._x - halfW <= translation._x && translation._x <= otherPos._x + halfW )
								{
									hitY = topY;
									bHit = true;
								}
							}
						}

						MeshComponent* pOtherMesh = pOther->getComponent<MeshComponent>();
						if ( pOtherMesh != nullptr && pOtherMesh->isActive() )
						{
							const float3  otherPos = pOtherMesh->getWorldPosition();
							const float3  otherScl = pOtherMesh->getLocalScale();
							const float32 topY	   = otherPos._y + otherScl._y * 0.5f;
							if ( topY <= rayStart._y && ( topY > hitY || bHit == false ) )
							{
								const float32 halfW = otherScl._x * 0.5f;
								const float32 halfD = otherScl._z * 0.5f;
								if ( otherPos._x - halfW <= translation._x && translation._x <= otherPos._x + halfW &&
									 otherPos._z - halfD <= translation._z && translation._z <= otherPos._z + halfD )
								{
									hitY = topY;
									bHit = true;
								}
							}
						}
					}
				}

				float32					bottomOffset = 0.0f;
				BoxCollider2DComponent* pMyBox		 = pRaw->getComponent<BoxCollider2DComponent>();
				if ( pMyBox != nullptr )
					bottomOffset = pMyBox->getOffsetScaleVec()._y * 0.5f;
				MeshComponent* pMyMesh = pRaw->getComponent<MeshComponent>();
				if ( pMyMesh != nullptr )
					bottomOffset = scale._y * 0.5f;

				translation._y = ( bHit ? hitY : 0.0f ) + bottomOffset;
			}

			pSceneComp->setLocalPosition( translation );
			pSceneComp->setLocalRotation( float3{
				MathUtil::toRadian( rotationDeg._x ),
				MathUtil::toRadian( rotationDeg._y ),
				MathUtil::toRadian( rotationDeg._z ) } );
			pSceneComp->setLocalScale( scale );
		}
	}

	void EditorViewportClient::frameSelected()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;

		GameObjectPtr pPrimary = pContext->getSelectionManager().getPrimaryObject();
		if ( pPrimary.isValid() == false )
			return;

		GameObject* pRaw = pPrimary.get();
		if ( pRaw == nullptr )
			return;

		SceneComponent* pSceneComp = pRaw->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		const float3 worldPos = pSceneComp->getWorldPosition();
		_orbitTarget		  = worldPos;

		float32					objectRadius = 2.0f;
		BoxCollider2DComponent* pBox		 = pRaw->getComponent<BoxCollider2DComponent>();
		if ( pBox != nullptr )
		{
			const float2 scl = pBox->getOffsetScaleVec();
			objectRadius	 = MathUtil::max( scl._x, scl._y ) * 0.6f;
		}
		MeshComponent* pMesh = pRaw->getComponent<MeshComponent>();
		if ( pMesh != nullptr )
		{
			const float3 scl = pMesh->getLocalScale();
			objectRadius	 = MathUtil::max( scl._x, MathUtil::max( scl._y, scl._z ) ) * 1.5f;
		}

		_orbitDistance = MathUtil::clamp( objectRadius * 2.5f, 3.0f, 60.0f );

		const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
		const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );
		const float3  forward{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
							   MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };

		_cameraPos = float3{ _orbitTarget._x - forward._x * _orbitDistance,
							 _orbitTarget._y - forward._y * _orbitDistance,
							 _orbitTarget._z - forward._z * _orbitDistance };
	}

	void EditorViewportClient::drawOrientationCube( ImDrawList* pDrawList, const float2& canvasPos,
													const float2& canvasSize )
	{
		if ( pDrawList == nullptr )
			return;

		const float32	  cubeCenterX = canvasPos._x + canvasSize._x - 45.0f;
		const float32	  cubeCenterY = canvasPos._y + ( _toolbarSettings._bShowStats ? 128.0f : 45.0f );
		constexpr float32 cubeRadius  = 26.0f;

		// Circular background disc
		pDrawList->AddCircleFilled( ImVec2( cubeCenterX, cubeCenterY ), cubeRadius + 6.0f,
									IM_COL32( 18, 22, 30, 200 ) );
		pDrawList->AddCircle( ImVec2( cubeCenterX, cubeCenterY ), cubeRadius + 6.0f, IM_COL32( 55, 65, 85, 180 ), 0,
							  1.5f );

		const float32 pitchRad = MathUtil::toRadian( _cameraRot._x );
		const float32 yawRad   = MathUtil::toRadian( _cameraRot._y );

		const float3 forward{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
							  MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
		const float3 right{ MathUtil::cos( yawRad ), 0.0f, -MathUtil::sin( yawRad ) };
		const float3 up{ right._y * forward._z - right._z * forward._y, right._z * forward._x - right._x * forward._z,
						 right._x * forward._y - right._y * forward._x };

		struct AxisItem
		{
			float3		_dir;
			ImU32		_col;
			const utf8* _label;
			float32		_depth;
			float2		_screenOffset;
			float3		_targetRot;
		};

		AxisItem arrAxes[6] = {
			{ float3{ 1.0f, 0.0f, 0.0f }, IM_COL32( 235,	 65,	 65, 255 ),	"X", 0.0f, float2{},
			  float3{ 0.0f, -90.0f, 0.0f }},
			{float3{ -1.0f, 0.0f, 0.0f }, IM_COL32( 130,  60,  60, 200 ), "-X", 0.0f, float2{},
			  float3{ 0.0f, 90.0f, 0.0f } },
			{ float3{ 0.0f, 1.0f, 0.0f },  IM_COL32( 65, 220,	 95, 255 ),	"Y", 0.0f, float2{},
			  float3{ 89.0f, 0.0f, 0.0f } },
			{float3{ 0.0f, -1.0f, 0.0f },  IM_COL32( 50, 130,  70, 200 ), "-Y", 0.0f, float2{},
			  float3{ -89.0f, 0.0f, 0.0f }},
			{ float3{ 0.0f, 0.0f, 1.0f },  IM_COL32( 65, 130, 245, 255 ),	 "Z", 0.0f, float2{},
			  float3{ 0.0f, 180.0f, 0.0f }},
			{float3{ 0.0f, 0.0f, -1.0f },  IM_COL32( 50,	 70, 140, 200 ), "-Z", 0.0f, float2{},
			  float3{ 0.0f, 0.0f, 0.0f }	 }
		   };

		for ( uint32 axisIndex = 0; axisIndex < 6; ++axisIndex )
		{
			AxisItem&	  ax   = arrAxes[axisIndex];
			const float32 dotR = ax._dir._x * right._x + ax._dir._y * right._y + ax._dir._z * right._z;
			const float32 dotU = ax._dir._x * up._x + ax._dir._y * up._y + ax._dir._z * up._z;
			const float32 dotF = ax._dir._x * forward._x + ax._dir._y * forward._y + ax._dir._z * forward._z;

			ax._depth		 = dotF;
			ax._screenOffset = float2{ dotR * cubeRadius * 0.78f, -dotU * cubeRadius * 0.78f };
		}

		// Sort by depth ascending so further items are drawn first
		std::sort( std::begin( arrAxes ), std::end( arrAxes ),
				   []( const AxisItem& a, const AxisItem& b )
		{ return a._depth < b._depth; } );

		const ImVec2 mousePos = ImGui::GetMousePos();

		for ( uint32 axisIndex = 0; axisIndex < 6; ++axisIndex )
		{
			const AxisItem& ax = arrAxes[axisIndex];
			const ImVec2	pt( cubeCenterX + ax._screenOffset._x, cubeCenterY + ax._screenOffset._y );

			// Axis line from center
			pDrawList->AddLine( ImVec2( cubeCenterX, cubeCenterY ), pt, ax._col, 1.8f );

			// Disc handle
			const float32 handleRadius = ( ax._depth > 0.0f ) ? 6.5f : 4.5f;
			const float32 distToMouse  = MathUtil::sqrt( ( mousePos.x - pt.x ) * ( mousePos.x - pt.x ) +
														 ( mousePos.y - pt.y ) * ( mousePos.y - pt.y ) );
			const bool	  bHovered	   = ( distToMouse <= handleRadius + 2.0f );

			pDrawList->AddCircleFilled( pt, handleRadius, bHovered ? IM_COL32( 255, 255, 255, 255 ) : ax._col );

			if ( ax._depth > -0.2f && ax._label[0] != '-' )
			{
				pDrawList->AddText( ImVec2( pt.x - 3.5f, pt.y - 6.0f ), IM_COL32( 15, 15, 20, 255 ), ax._label );
			}

			if ( bHovered && ImGui::IsMouseClicked( 0 ) )
			{
				_cameraRot				  = ax._targetRot;
				const float32 newPitchRad = MathUtil::toRadian( _cameraRot._x );
				const float32 newYawRad	  = MathUtil::toRadian( _cameraRot._y );
				const float3  newForward{ MathUtil::sin( newYawRad ) * MathUtil::cos( newPitchRad ),
										  -MathUtil::sin( newPitchRad ),
										  MathUtil::cos( newYawRad ) * MathUtil::cos( newPitchRad ) };
				_cameraPos = float3{ _orbitTarget._x - newForward._x * _orbitDistance,
									 _orbitTarget._y - newForward._y * _orbitDistance,
									 _orbitTarget._z - newForward._z * _orbitDistance };
			}
		}
	}

	void EditorViewportClient::drawAdaptiveGrid( ImDrawList* pDrawList, const float2& canvasPos,
												 const float2& canvasSize, const float32* pView, const float32* pProj )
	{
		if ( pDrawList == nullptr || pView == nullptr || pProj == nullptr )
			return;

		float4x4 viewMat{};
		float4x4 projMat{};
		loadColumnMajor( viewMat, pView );
		loadColumnMajor( projMat, pProj );
		const float4x4 viewProj = viewMat * projMat;

		constexpr int32	  kGridExtent = 20;
		constexpr float32 kGridStep	  = 1.0f;

		if ( _toolbarSettings._bIs2DMode )
		{
			// XY plane vertical grid for 2D mode
			const float32 centerX = MathUtil::floor( _cameraPos._x );
			const float32 centerY = MathUtil::floor( _cameraPos._y );

			for ( int32 index = -kGridExtent; index <= kGridExtent; ++index )
			{
				const float32 current  = static_cast<float32>( index ) * kGridStep;
				const bool	  bOriginX = ( MathUtil::abs( centerX + current ) < 0.01f );
				const bool	  bOriginY = ( MathUtil::abs( centerY + current ) < 0.01f );
				const bool	  bMajor   = ( index % 5 == 0 );

				const ImU32 colX = bOriginX ? IM_COL32( 65, 220, 95, 180 )
											: ( bMajor ? IM_COL32( 90, 100, 120, 100 ) : IM_COL32( 60, 65, 80, 55 ) );
				const ImU32 colY = bOriginY ? IM_COL32( 220, 60, 60, 180 )
											: ( bMajor ? IM_COL32( 90, 100, 120, 100 ) : IM_COL32( 60, 65, 80, 55 ) );

				// Vertical lines parallel to Y
				const float3 pY0{ centerX + current, centerY - static_cast<float32>( kGridExtent ), 0.0f };
				const float3 pY1{ centerX + current, centerY + static_cast<float32>( kGridExtent ), 0.0f };
				ImVec2		 sY0, sY1;
				if ( projectPointToScreen( viewProj, pY0, canvasPos, canvasSize, sY0 ) &&
					 projectPointToScreen( viewProj, pY1, canvasPos, canvasSize, sY1 ) )
				{
					pDrawList->AddLine( sY0, sY1, colX, ( bOriginX || bMajor ) ? 1.5f : 1.0f );
				}

				// Horizontal lines parallel to X
				const float3 pX0{ centerX - static_cast<float32>( kGridExtent ), centerY + current, 0.0f };
				const float3 pX1{ centerX + static_cast<float32>( kGridExtent ), centerY + current, 0.0f };
				ImVec2		 sX0, sX1;
				if ( projectPointToScreen( viewProj, pX0, canvasPos, canvasSize, sX0 ) &&
					 projectPointToScreen( viewProj, pX1, canvasPos, canvasSize, sX1 ) )
				{
					pDrawList->AddLine( sX0, sX1, colY, ( bOriginY || bMajor ) ? 1.5f : 1.0f );
				}
			}
		}
		else
		{
			// XZ plane ground grid for 3D mode
			const float32 centerX = MathUtil::floor( _cameraPos._x );
			const float32 centerZ = MathUtil::floor( _cameraPos._z );

			for ( int32 index = -kGridExtent; index <= kGridExtent; ++index )
			{
				const float32 current  = static_cast<float32>( index ) * kGridStep;
				const bool	  bOriginX = ( MathUtil::abs( centerX + current ) < 0.01f );
				const bool	  bOriginZ = ( MathUtil::abs( centerZ + current ) < 0.01f );
				const bool	  bMajor   = ( index % 5 == 0 );

				const ImU32 colX = bOriginX ? IM_COL32( 220, 60, 60, 180 )
											: ( bMajor ? IM_COL32( 90, 100, 120, 100 ) : IM_COL32( 60, 65, 80, 55 ) );
				const ImU32 colZ = bOriginZ ? IM_COL32( 60, 110, 240, 180 )
											: ( bMajor ? IM_COL32( 90, 100, 120, 100 ) : IM_COL32( 60, 65, 80, 55 ) );

				// Line parallel to Z
				const float3 pZ0{ centerX + current, 0.0f, centerZ - static_cast<float32>( kGridExtent ) };
				const float3 pZ1{ centerX + current, 0.0f, centerZ + static_cast<float32>( kGridExtent ) };
				ImVec2		 sZ0, sZ1;
				if ( projectPointToScreen( viewProj, pZ0, canvasPos, canvasSize, sZ0 ) &&
					 projectPointToScreen( viewProj, pZ1, canvasPos, canvasSize, sZ1 ) )
				{
					pDrawList->AddLine( sZ0, sZ1, colX, ( bOriginX || bMajor ) ? 1.5f : 1.0f );
				}

				// Line parallel to X
				const float3 pX0{ centerX - static_cast<float32>( kGridExtent ), 0.0f, centerZ + current };
				const float3 pX1{ centerX + static_cast<float32>( kGridExtent ), 0.0f, centerZ + current };
				ImVec2		 sX0, sX1;
				if ( projectPointToScreen( viewProj, pX0, canvasPos, canvasSize, sX0 ) &&
					 projectPointToScreen( viewProj, pX1, canvasPos, canvasSize, sX1 ) )
				{
					pDrawList->AddLine( sX0, sX1, colZ, ( bOriginZ || bMajor ) ? 1.5f : 1.0f );
				}
			}
		}
	}

	void EditorViewportClient::processRulerTool( ImDrawList* pDrawList, const float2& canvasPos,
												 const float2& canvasSize, const float32* pView, const float32* pProj )
	{
		if ( pDrawList == nullptr || pView == nullptr || pProj == nullptr )
			return;

		if ( ImGui::IsKeyDown( ImGuiKey_M ) == false && _toolbarSettings._bShowRuler == false )
		{
			_bRulerActive = false;
			return;
		}

		float4x4 viewMat{};
		float4x4 projMat{};
		loadColumnMajor( viewMat, pView );
		loadColumnMajor( projMat, pProj );
		const float4x4 viewProj	   = viewMat * projMat;
		const float4x4 invViewProj = viewProj.invert();

		const ImVec2  mousePos	   = ImGui::GetMousePos();
		const float32 mouseCanvasX = mousePos.x - canvasPos._x;
		const float32 mouseCanvasY = mousePos.y - canvasPos._y;

		if ( 0.0f <= mouseCanvasX && mouseCanvasX <= canvasSize._x &&
			 0.0f <= mouseCanvasY && mouseCanvasY <= canvasSize._y )
		{
			const float32 ndcX = ( mouseCanvasX / canvasSize._x ) * 2.0f - 1.0f;
			const float32 ndcY = 1.0f - ( mouseCanvasY / canvasSize._y ) * 2.0f;

			float3 nearPt{}, farPt{};
			if ( unproject( invViewProj, ndcX, ndcY, 0.0f, nearPt ) &&
				 unproject( invViewProj, ndcX, ndcY, 1.0f, farPt ) )
			{
				const float3 dir = farPt - nearPt;
				if ( MathUtil::abs( dir._y ) > 1e-4f )
				{
					const float32 t		   = -nearPt._y / dir._y;
					const float3  groundPt = nearPt + dir * t;

					if ( ImGui::IsMouseClicked( 0 ) )
					{
						_rulerStartWorld = groundPt;
						_rulerEndWorld	 = groundPt;
						_bRulerActive	 = true;
					}
					else if ( ImGui::IsMouseDown( 0 ) && _bRulerActive )
					{
						_rulerEndWorld = groundPt;
					}
				}
			}
		}

		if ( _bRulerActive )
		{
			ImVec2 sStart, sEnd;
			if ( projectPointToScreen( viewProj, _rulerStartWorld, canvasPos, canvasSize, sStart ) &&
				 projectPointToScreen( viewProj, _rulerEndWorld, canvasPos, canvasSize, sEnd ) )
			{
				// Measurement line
				pDrawList->AddLine( sStart, sEnd, IM_COL32( 255, 215, 40, 240 ), 2.5f );
				pDrawList->AddCircleFilled( sStart, 5.0f, IM_COL32( 255, 230, 80, 255 ) );
				pDrawList->AddCircleFilled( sEnd, 5.0f, IM_COL32( 255, 230, 80, 255 ) );

				const float3  delta = _rulerEndWorld - _rulerStartWorld;
				const float32 dist	= delta.getLength();

				utf8 arrDistText[64];
				formatstring( arrDistText, sizeof( arrDistText ), "%.2f m (dX: %.2f, dZ: %.2f)",
							  static_cast<float64>( dist ), static_cast<float64>( delta._x ),
							  static_cast<float64>( delta._z ) );

				const ImVec2 mid( ( sStart.x + sEnd.x ) * 0.5f, ( sStart.y + sEnd.y ) * 0.5f - 16.0f );
				pDrawList->AddRectFilled( ImVec2( mid.x - 4.0f, mid.y - 2.0f ),
										  ImVec2( mid.x + 160.0f, mid.y + 18.0f ), IM_COL32( 20, 24, 32, 220 ), 4.0f );
				pDrawList->AddText( mid, IM_COL32( 255, 230, 80, 255 ), arrDistText );
			}
		}
	}

	void EditorViewportClient::handleViewportAssetDrop( const utf8* pAssetPath, const float2& canvasPos,
														const float2& canvasSize, const float32* pView,
														const float32* pProj )
	{
		if ( pAssetPath == nullptr || pView == nullptr || pProj == nullptr )
			return;

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr )
			return;
		Scene* pScene = pSceneManager->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
			return;

		GameObjectManager* pManager = pScene->getObjectManager();

		// Calculate 3D spawn world position from mouse cursor
		float4x4 viewMat{};
		float4x4 projMat{};
		loadColumnMajor( viewMat, pView );
		loadColumnMajor( projMat, pProj );
		const float4x4 viewProj	   = viewMat * projMat;
		const float4x4 invViewProj = viewProj.invert();

		const ImVec2  mousePos	   = ImGui::GetMousePos();
		const float32 mouseCanvasX = mousePos.x - canvasPos._x;
		const float32 mouseCanvasY = mousePos.y - canvasPos._y;

		float3 spawnPos{ 0.0f, 0.0f, 0.0f };
		if ( 0.0f <= mouseCanvasX && mouseCanvasX <= canvasSize._x &&
			 0.0f <= mouseCanvasY && mouseCanvasY <= canvasSize._y )
		{
			const float32 ndcX = ( mouseCanvasX / canvasSize._x ) * 2.0f - 1.0f;
			const float32 ndcY = 1.0f - ( mouseCanvasY / canvasSize._y ) * 2.0f;

			float3 nearPt{}, farPt{};
			if ( unproject( invViewProj, ndcX, ndcY, 0.0f, nearPt ) &&
				 unproject( invViewProj, ndcX, ndcY, 1.0f, farPt ) )
			{
				const float3 dir = farPt - nearPt;
				if ( _toolbarSettings._bIs2DMode )
				{
					// Intersect with Z = 0 plane
					if ( MathUtil::abs( dir._z ) > 1e-4f )
					{
						const float32 t = -nearPt._z / dir._z;
						spawnPos		= nearPt + dir * t;
						spawnPos._z		= 0.0f;
					}
				}
				else
				{
					// Intersect with Y = 0 ground plane
					if ( MathUtil::abs( dir._y ) > 1e-4f )
					{
						const float32 t = -nearPt._y / dir._y;
						spawnPos		= nearPt + dir * t;
						spawnPos._y		= 0.0f;
					}
				}
			}
		}

		const string ext = FileUtil::getExtension( pAssetPath );
		if ( ext == ".prefab" || ext == ".pfb" || StringUtil::stristr( pAssetPath, ".prefab.xml" ) != nullptr )
		{
			GameObject* pSpawned = EditorUtil::spawnPrefabFromAssetPath( pManager, pAssetPath, nullptr );
			if ( pSpawned != nullptr )
			{
				SceneComponent* pSc = pSpawned->getPrimarySceneComponent();
				if ( pSc != nullptr )
					pSc->setLocalPosition( spawnPos );
				EditorTransaction::recordCreation( GameObjectPtr{ pSpawned }, "Spawn Prefab in Viewport" );
				EditorContext::get()->getWorkspace().selectGameObject( GameObjectPtr{ pSpawned },
																	   SelectionMode::Replace );
			}
		}
		else if ( ext == ".scene" || ( ext == ".xml" && StringUtil::stristr( pAssetPath, ".scene" ) != nullptr ) )
		{
			EditorContext::get()->getWorkspace().requestLoadScene( pAssetPath );
		}
		else if ( ext == ".png" || ext == ".jpg" || ext == ".dds" || ext == ".tga" || ext == ".bmp" )
		{
			string filename = FileUtil::getFileNamePart( pAssetPath );
			filename		= FileUtil::removeExtension( filename );

			GameObject* pSpawned = pManager->createGameObject( hashed_string( filename ) );
			if ( pSpawned != nullptr )
			{
				SceneComponent* pSc = pSpawned->addComponent<SceneComponent>();
				if ( pSc != nullptr )
					pSc->setLocalPosition( spawnPos );

				SpriteComponent* pSprite = pSpawned->addComponent<SpriteComponent>();
				if ( pSprite != nullptr )
				{
					string relPath;
					FileUtil::makePathRelative( FileUtil::getCurrentPath(), pAssetPath, relPath );
					relPath = FileUtil::normalizeSeparators( relPath );
					pSprite->setTextureName( relPath );
				}

				EditorTransaction::recordCreation( GameObjectPtr{ pSpawned }, "Spawn Sprite in Viewport" );
				EditorContext::get()->getWorkspace().selectGameObject( GameObjectPtr{ pSpawned },
																	   SelectionMode::Replace );
			}
		}
	}
} // namespace sw::editor
