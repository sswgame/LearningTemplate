/**
 * @file EditorPlacementPose.h
 * @brief 씬 뷰에 애셋을 놓을 자세 — 자리와 위 축 둘레 회전입니다. 종류의 놓기(`IEditorAssetTypeActions::placeInViewport`)와 스폰 명령이 받습니다.
 * @details 회전은 놓기 전에 고른다(끄는 동안 회전 키 — `EditorViewportAssetPlacement`). 언리얼 · 유니티는 놓은 뒤 기즈모로 돌리고, 하우징 · 건설 게임은 놓기 전에 돌린다 —
 *          여기서는 놓기 전에 고르고, 같은 값이 되돌리기 기록보다 먼저 입혀져 다시 실행해도 같은 자세로 돌아온다(위치와 같은 규칙).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 놓을 자세 하나입니다. */
    struct EditorPlacementPose
    {
        float3  _position{};
        float32 _yaw{ 0.0f };      ///< 위 축(3D 는 Y, 2D 는 Z) 둘레 회전(라디안, 0 ~ 2π)
        bool    _b2DMode{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        /** @brief 놓은 오브젝트의 로컬 오일러 회전(피치, 요, 롤)에 더할 값입니다 — 3D 는 요, 2D 는 롤(화면 평면)입니다. 프리팹이 가진 회전은 그대로 두고 더한다. */
        float3 makeRotationOffset() const { return _b2DMode ? float3{ 0.0f, 0.0f, _yaw } : float3{ 0.0f, _yaw, 0.0f }; }
    };
} // namespace sw::editor
