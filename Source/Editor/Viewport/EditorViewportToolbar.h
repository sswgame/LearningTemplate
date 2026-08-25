#pragma once
#include "Core/Common/Types.h"

namespace sw
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
	 * @brief 뷰포트 상단 스냅/뷰모드/카메라 속도 오버레이 툴바
	 */
	class EditorViewportToolbar
	{
	public:
		EditorViewportToolbar()	 = default;
		~EditorViewportToolbar() = default;

		/** @brief 뷰포트 상단 툴바 UI를 렌더링합니다. */
		void draw( ViewportToolbarSettings& settings, float32 viewportWidth );
	};
} // namespace sw
