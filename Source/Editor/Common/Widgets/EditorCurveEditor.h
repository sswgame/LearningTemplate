/**
 * @file EditorCurveEditor.h
 * @brief `FloatCurve` 의 작은 미리보기와 편집기(격자 · 키 끌기 · 접선 손잡이 · 메뉴 · 화면 맞추기)를 그립니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Widgets/EditorCurveView.h"

#include "Engine/Utility/FloatCurve.h"

namespace sw::editor
{
    /** @brief 편집기에서 지금 끄는 것입니다. */
    enum class EditorCurveDrag : uint8
    {
        None,
        Key,           ///< 키 자리
        ArriveTangent, ///< 들어오는 접선 손잡이
        LeaveTangent,  ///< 나가는 접선 손잡이
        Pan,           ///< 가운데 단추로 화면 옮기기
    };

    /**
     * @struct EditorCurveEditorState
     * @brief 열린 커브 편집기 하나의 상태입니다. 편집은 작업 사본(`_working`)에 하고, 마치면(끌기를 놓을 때 · 메뉴 동작) 부르는 쪽이 값에 입힙니다.
     */
    struct EditorCurveEditorState
    {
        EditorCurveView _view{};
        FloatCurve      _working{};                             ///< 편집 중인 사본
        uint32          _selectedKey{ invalid_index::kUint32 }; ///< 고른 키
        uint32          _menuKey{ invalid_index::kUint32 };     ///< 오른쪽 클릭 메뉴가 가리키는 키(빈 곳이면 없음)
        float32         _menuTime{ 0.0f };                      ///< 오른쪽 클릭 자리(커브 좌표)
        float32         _menuValue{ 0.0f };
        EditorCurveDrag _drag{ EditorCurveDrag::None };
        bool            _bDragMoved{ false }; ///< 끄는 동안 실제로 움직였으면 true — 놓을 때만 편집으로 남긴다
        bool            _bFitPending{ true }; ///< 다음 그리기에서 화면을 커브에 맞춘다(처음 열 때 · F)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorCurveEditor
     * @brief 언리얼 Curve Editor · 유니티 CurveField 의 자리입니다. 미리보기는 프로퍼티 줄, 편집기는 그 줄에서 여는 팝업이 그립니다.
     */
    struct SW_EDITOR_API EditorCurveEditor
    {
        /** @brief 폭 전체 · 높이 @p height 의 미리보기(꺾은선)를 그립니다. 눌렀으면 true 입니다. */
        static bool drawPreview( const utf8* pID, const FloatCurve& curve, float32 height );
        /**
         * @brief 편집기 본문(위 줄의 숫자 칸 + 남은 자리 전부의 캔버스)을 그립니다.
         * @return 작업 사본의 편집을 마쳤으면 true 입니다(끌기를 놓음 · 메뉴 동작 · 숫자 칸 입력을 마침). 부르는 쪽이 `_working` 을 값에 입힌다.
         * @details 조작: 왼쪽 끌기 = 키(Shift 는 시간만 · Ctrl 은 값만) · 접선 손잡이(Cubic 키, 자동 접선이 꺼진다), 오른쪽 클릭 = 키 더하기 / 지우기 / 보간,
         *          F = 전체 보기, 휠 = 확대, 가운데 끌기 = 이동. 이름표: `curve.canvas` · `curve.key.<n>` · `curve.menu.addKey`.
         */
        static bool drawEditor( EditorCurveEditorState& state );
    };
} // namespace sw::editor
