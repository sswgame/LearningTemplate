/**
 * @file EditorCurveEditor.h
 * @brief `FloatCurve` 의 작은 미리보기와 편집기를 그립니다. 편집기는 격자, 키 드래그, 접선 핸들, 메뉴, 화면 맞추기를 지원합니다.
 */
#pragma once
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Widgets/EditorCurveView.h"

#include "Engine/Curve/FloatCurve.h"

namespace sw::editor
{
    /** @brief 편집기에서 현재 드래그하는 대상입니다. */
    enum class EditorCurveDrag : uint8
    {
        None,
        Key,           ///< 키
        ArriveTangent, ///< 들어오는 접선 손잡이
        LeaveTangent,  ///< 나가는 접선 손잡이
        Pan,           ///< 가운데 버튼으로 화면 이동
    };

    /**
     * @struct EditorCurveEditorState
     * @brief 열린 커브 편집기 하나의 상태입니다. 편집은 작업 복사본(`_working`)에 하고, 마치면(드래그를 놓을 때, 메뉴 동작) 호출하는 쪽이 값에 적용합니다.
     */
    struct EditorCurveEditorState
    {
        EditorCurveView _view{};
        FloatCurve      _working{};                             ///< 편집 중인 복사본
        uint32          _selectedKey{ invalid_index::kUint32 }; ///< 선택한 키
        uint32          _menuKey{ invalid_index::kUint32 };     ///< 오른쪽 클릭 메뉴가 가리키는 키다. 빈 곳이면 없다
        float32         _menuTime{ 0.0f };                      ///< 오른쪽 클릭한 위치(커브 좌표)
        float32         _menuValue{ 0.0f };
        EditorCurveDrag _drag{ EditorCurveDrag::None };
        bool            _bDragMoved{ false }; ///< 드래그하는 동안 실제로 움직였으면 true 다. 놓을 때만 편집으로 남긴다
        bool            _bFitPending{ true }; ///< 다음 그리기에서 화면을 커브에 맞춘다. 처음 열 때와 F 키를 눌렀을 때 켠다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorCurveEditor
     * @brief 언리얼 Curve Editor 와 유니티 CurveField 에 해당합니다. 미리보기는 프로퍼티 행이, 편집기는 그 행에서 여는 팝업이 그립니다.
     */
    struct SW_EDITOR_API EditorCurveEditor
    {
        /** @brief 폭 전체와 높이 @p height 의 미리보기(꺾은선)를 그립니다. 눌렀으면 true 입니다. */
        static bool drawPreview( const utf8* pID, const FloatCurve& curve, float32 height );
        /**
         * @brief 편집기 본문을 그립니다. 위쪽 행의 숫자 입력란과 남은 영역 전체의 캔버스로 이루어집니다.
         * @return 작업 복사본의 편집을 마쳤으면 true 입니다. 드래그를 놓았거나, 메뉴 동작을 했거나, 숫자 입력을 마친 경우입니다. 호출하는 쪽이 `_working` 을 값에 적용합니다.
         * @details 조작은 다음과 같다. 왼쪽 드래그는 키를 옮기며(Shift 는 시간만, Ctrl 은 값만) 접선 핸들도 옮긴다(Cubic 키이며 자동 접선이 꺼진다).
         *          오른쪽 클릭은 키 추가, 삭제, 보간 메뉴다. F 는 전체 보기, 휠은 확대, 가운데 드래그는 이동이다. 레이블은 `curve.canvas`, `curve.key.<n>`, `curve.menu.addKey` 다.
         */
        static bool drawEditor( EditorCurveEditorState& state );
    };
} // namespace sw::editor
