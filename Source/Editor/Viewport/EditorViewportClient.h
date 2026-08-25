#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Engine/Object/GameObject/GameObjectPtr.h"

namespace sw
{
	class IRHIDevice;
	struct EditorUIContext;

	/** @brief 에디터 카메라 제어 모드 */
	enum class CameraControlMode : uint8
	{
		Fly = 0, ///< WASD + RMB 회전 (Unreal 스타일)
		Orbit,	 ///< Alt + LMB 회전, Alt + RMB 줌 (Maya/Unity 스타일)
		Ortho2D	 ///< 2D 휠 줌 & MMB 패닝
	};

	/**
	 * @class EditorViewportClient
	 * @brief 뷰포트 캔버스 렌더링, 카메라 조작, 기즈모 및 상단 툴바를 통합 관리하는 클라이언트
	 */
	class EditorViewportClient
	{
	public:
		EditorViewportClient();
		~EditorViewportClient() = default;

		/** @brief 뷰포트 프레임 틱 및 입력 처리 */
		void update( float32 deltaTime, bool bWindowFocused, bool bWindowHovered );

		/** @brief 뷰포트 UI 및 ImGuizmo 렌더링 */
		void draw( const EditorUIContext& ctx, const void* pTextureId, const float2& canvasSize );

		/** @brief View Matrix 계산 */
		void getViewMatrix( float32* pOutMatrix ) const;
		/** @brief Projection Matrix 계산 */
		void getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const;

		/** @brief 카메라 위치 및 회전 설정 */
		void setCameraPosition( const float3& pos ) { _cameraPos = pos; }
		void setCameraRotation( const float3& rot ) { _cameraRot = rot; }
		void setOrbitTarget( const float3& target ) { _orbitTarget = target; }
		void setCameraMode( CameraControlMode mode ) { _cameraMode = mode; }

		const float3&			 getCameraPosition() const { return _cameraPos; }
		const float3&			 getCameraRotation() const { return _cameraRot; }
		CameraControlMode		 getCameraMode() const { return _cameraMode; }
		ViewportToolbarSettings& getToolbarSettings() { return _toolbarSettings; }

	private:
		void processFlyInput( float32 deltaTime );
		void processOrbitInput();
		void drawGizmo( GameObjectPtr pObj, const float32* pView, const float32* pProj,
						const float2& canvasPos, const float2& canvasSize );

	private:
		float3					_cameraPos{ 0.0f, 3.0f, -6.0f };
		float3					_cameraRot{ 20.0f, 0.0f, 0.0f }; // Pitch, Yaw, Roll
		float3					_orbitTarget{ 0.0f, 0.0f, 0.0f };
		float32					_orbitDistance{ 8.0f };
		float32					_fovY{ 60.0f };
		float32					_nearZ{ 0.1f };
		float32					_farZ{ 1000.0f };
		CameraControlMode		_cameraMode{ CameraControlMode::Fly };
		ViewportToolbarSettings _toolbarSettings{};
		EditorViewportToolbar	_toolbar{};
	};
} // namespace sw
