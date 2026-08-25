#include "pch.h"

#include "Editor/Viewport/EditorViewportClient.h"
#include "Editor/Workspace/EditorWorkspace.h"
#include "Editor/Workspace/SelectionManager.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "RuntimeAPI/EditorUIContext.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw
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

	void EditorViewportClient::draw( const EditorUIContext& /*ctx*/, const void* pTextureId, const float2& canvasSize )
	{
		// 1) 툴바
		_toolbar.draw( _toolbarSettings, canvasSize._x );

		// 2) 렌더 타깃 이미지 캔버스
		const ImVec2 imagePos = ImGui::GetCursorScreenPos();
		if ( pTextureId != nullptr )
		{
			ImGui::Image( reinterpret_cast<ImTextureID>( pTextureId ), ImVec2{ canvasSize._x, canvasSize._y } );
		}
		else
		{
			ImGui::Dummy( ImVec2{ canvasSize._x, canvasSize._y } );
		}

		// 3) 기즈모 렌더링
		GameObjectPtr pPrimary = SelectionManager::getPrimaryObject();
		if ( pPrimary.isValid() )
		{
			float32 arrView[16];
			float32 arrProj[16];
			getViewMatrix( arrView );
			getProjectionMatrix( arrProj, canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f ) );
			drawGizmo( pPrimary, arrView, arrProj, float2{ imagePos.x, imagePos.y }, canvasSize );
		}
	}

	void EditorViewportClient::drawGizmo( GameObjectPtr pObj, const float32* pView, const float32* pProj,
										  const float2& canvasPos, const float2& canvasSize )
	{
		GameObject* pRaw = pObj.get();
		if ( pRaw == nullptr )
			return;

		SceneComponent* pSceneComp = pRaw->getComponent<SceneComponent>().get();
		if ( pSceneComp == nullptr )
			return;

		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect( canvasPos._x, canvasPos._y, canvasSize._x, canvasSize._y );

		float4x4 worldMat = pSceneComp->getWorldMatrix();
		float32	 arrMatrix[16];
		Memory::copy( arrMatrix, &worldMat, sizeof( arrMatrix ) );

		const int32			opInt = EditorWorkspace::gizmoOperation();
		ImGuizmo::OPERATION op	  = ImGuizmo::TRANSLATE;
		if ( opInt == 1 )
			op = ImGuizmo::ROTATE;
		else if ( opInt == 2 )
			op = ImGuizmo::SCALE;

		const ImGuizmo::MODE mode = EditorWorkspace::gizmoLocalSpace() ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

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
			Memory::copy( &newWorldMat, arrMatrix, sizeof( newWorldMat ) );

			SceneComponent* pParentSc = pSceneComp->getParent();
			if ( pParentSc != nullptr )
			{
				const float4x4 invParentWorld = pParentSc->getWorldMatrix().invert();
				const float4x4 newLocalMat	  = invParentWorld * newWorldMat;
				Memory::copy( arrMatrix, &newLocalMat, sizeof( arrMatrix ) );
			}

			float3 translation, rotation, scale;
			ImGuizmo::DecomposeMatrixToComponents( arrMatrix, &translation._x, &rotation._x, &scale._x );

			pSceneComp->setLocalPosition( translation );
			pSceneComp->setLocalRotation( rotation );
			pSceneComp->setLocalScale( scale );
		}
	}
} // namespace sw
