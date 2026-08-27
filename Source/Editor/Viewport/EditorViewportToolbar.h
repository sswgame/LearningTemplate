#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
	/** @brief 뷰포트 렌더 모드 */
	enum class ViewportRenderMode : uint8
	{
		Lit = 0,
		Unlit,
		Wireframe
	};

	/** @brief 뷰포트 툴바 설정 데이터 */
	struct ViewportToolbarSettings
	{
		bool			   _bGridSnap{ false };
		float32			   _gridSnapValue{ 1.0f };
		bool			   _bRotationSnap{ false };
		float32			   _rotationSnapValue{ 15.0f };
		bool			   _bScaleSnap{ false };
		float32			   _scaleSnapValue{ 0.1f };
		ViewportRenderMode _renderMode{ ViewportRenderMode::Lit };
		float32			   _cameraSpeed{ 5.0f };
		bool			   _bShowStats{ true };
	};

	/**
	 * @class EditorViewportToolbar
	 * @brief 뷰포트 상단 툴바와 기즈모 트랜스폼 바
	 */
	class EditorViewportToolbar
	{
	public:
		/** @brief 뷰포트 상단 뷰모드/카메라 속도 툴바를 그립니다. */
		static void draw( ViewportToolbarSettings& settings, float32 viewportWidth );
		/** @brief 선택된 오브젝트의 Translate/Rotate/Scale 및 스냅 플로팅 바를 그립니다. */
		static void drawTransformBar( ViewportToolbarSettings& settings, const float2& anchorPos, bool bEnabled );
	};
} // namespace sw::editor
