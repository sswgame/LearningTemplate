#include "pch.h"

#include "Editor/Viewport/EditorViewportClient.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

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
	} // namespace

	EditorViewportClient::EditorViewportClient()
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
		if ( bWindowHovered == false && bWindowFocused == false )
			return;

		ImGuiIO& io = ImGui::GetIO();

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
			SceneManager* pSceneManager = editor::getService<SceneManager>();
			if ( pSceneManager != nullptr )
			{
				Scene* pScene = pSceneManager->getActiveScene();
				if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
					pScene->getObjectManager()->flushSceneTransforms();
			}

			processPicking( canvasPos, canvasSize, pCamera );

			GameObjectPtr pPrimary = EditorContext::get()->getSelectionManager().getPrimaryObject();
			if ( pPrimary.isValid() )
			{
				const float32 aspect = canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
				float32		  arrView[16];
				float32		  arrProj[16];
				storeColumnMajor( arrView, pCamera->getViewMatrix() );
				storeColumnMajor( arrProj, pCamera->getProjectionMatrix( aspect ) );
				drawGizmo( pPrimary, arrView, arrProj, canvasPos, canvasSize );
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

		GameObject*	   pBestObj{ nullptr };
		MeshComponent* pBestMesh{ nullptr };
		float32		   bestT{ MathUtil::MaxFloat };

		const vector<GameObject*> listObjects = pManager->getAllGameObjects();
		for ( GameObject* pObj : listObjects )
		{
			if ( pObj == nullptr || pObj->isActive() == false )
				continue;

			MeshComponent* pMeshComp = pObj->getComponent<MeshComponent>().get();
			if ( pMeshComp == nullptr || pMeshComp->isActive() == false )
				continue;
			if ( pMeshComp->isVisible() == false )
				continue;

			const float3  scale	   = pMeshComp->getLocalScale();
			const float32 absX	   = MathUtil::abs( scale._x );
			const float32 absY	   = MathUtil::abs( scale._y );
			const float32 absZ	   = MathUtil::abs( scale._z );
			const float32 maxScale = MathUtil::max( absX, MathUtil::max( absY, absZ ) );
			const float32 radius   = pMeshComp->getBoundsRadius() * MathUtil::max( maxScale, 0.001f );
			const float3  center   = pMeshComp->getWorldPosition();
			float32		  hitT{ 0.0f };
			if ( rayHitsSphere( nearPt, dir, center, radius, hitT ) == false )
				continue;
			if ( hitT >= bestT )
				continue;

			bestT	  = hitT;
			pBestObj  = pObj;
			pBestMesh = pMeshComp;
		}

		if ( pBestObj != nullptr )
			EditorContext::get()->getWorkspace().selectComponent( GameObjectPtr{ pBestObj }, ComponentPtr{ pBestMesh } );
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

			pSceneComp->setLocalPosition( translation );
			pSceneComp->setLocalRotation( float3{
				MathUtil::toRadian( rotationDeg._x ),
				MathUtil::toRadian( rotationDeg._y ),
				MathUtil::toRadian( rotationDeg._z ) } );
			pSceneComp->setLocalScale( scale );
		}
	}
} // namespace sw::editor
