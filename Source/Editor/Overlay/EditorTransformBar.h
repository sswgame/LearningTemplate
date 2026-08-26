/**
 * @file EditorTransformBar.h
 * @brief 선택된 오브젝트의 Translate/Rotate/Scale 및 스냅 플로팅 바
 */
#pragma once
#include "Core/Math/VectorMath.h"

namespace sw
{
	struct ViewportToolbarSettings;

	/**
	 * @brief Game View 위에 트랜스폼 플로팅 바를 그립니다.
	 * 선택이 없으면 비활성화됩니다.
	 */
	void drawEditorTransformBar( ViewportToolbarSettings& settings, const float2& anchorPos );
} // namespace sw
