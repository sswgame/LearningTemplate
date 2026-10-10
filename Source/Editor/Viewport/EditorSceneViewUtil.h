/**
 * @file EditorSceneViewUtil.h
 * @brief 씬 뷰(에디터 카메라)를 다른 코드(확장 모듈의 패널 · 커맨드)가 움직이는 진입점입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct float3;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorSceneViewUtil
     * @brief 씬 뷰 패널을 찾아 그 에디터 카메라를 옮깁니다. 패널 클래스를 모르는 확장 모듈이 씁니다.
     */
    struct SW_EDITOR_API EditorSceneViewUtil
    {
        /** @brief 에디터 카메라가 @p target 을 반지름 @p radius 의 물체처럼 담게 옮깁니다. 씬 뷰 패널이 없으면 false 입니다. */
        static bool focusOn( const float3& target, float32 radius );
    };
} // namespace sw::editor
