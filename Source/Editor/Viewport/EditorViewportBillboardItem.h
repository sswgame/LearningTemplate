/**
 * @file EditorViewportBillboardItem.h
 * @brief 메시가 없는 컴포넌트(빛 · 카메라 · 오디오 …)를 뷰포트에 아이콘 배지로 보이고, 눌러 고르게 합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"

namespace sw
{
    struct float4x4;

    class CameraComponent;
    class Component;
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 이번 프레임에 화면에 보이는 빌보드 하나입니다. */
    struct SW_EDITOR_API EditorViewportBillboardItem
    {
        GameObject*            _pObject{ nullptr };
        Component*             _pComponent{ nullptr };
        EditorComponentIconRow _row{};    ///< 값으로 보관한다 — `findRow` 는 값을 돌려주므로 그 주소는 수집이 끝나면 무효다
        float2                 _screen{}; ///< 배지 가운데(캔버스 화면 좌표)

        // 언리얼 UBillboardComponent(에디터 전용 스프라이트) · 유니티 Gizmo 아이콘 · Godot 3D 기즈모 아이콘에 해당합니다.
        // 그리기(시각화 `billboard`, 툴바 "Icons")와 클릭 선택(`EditorViewportPicker` 가 레이 피킹보다 먼저)이 같은 `collect` 를 씁니다.
        /** @brief 시각화 등록 id 입니다(툴바 켬/끔과 클릭 선택이 같이 본다). */
        static constexpr const utf8* kVisualizerID = "billboard";

        /** @brief 레지스트리의 빌보드 종류 컴포넌트 중 화면에 들어오는 것을 모읍니다(먼저 비운다). 한 오브젝트에 둘이면 먼저 찾은 하나만입니다. */
        static void collect( const GameObjectManager& manager, const float4x4& viewProj, const float2& canvasPos, const float2& canvasSize,
                             const CameraComponent* pSkipCamera, vector<EditorViewportBillboardItem>& outListItem );
        /** @brief 화면 점 @p point 에서 배지 반지름 안의 가장 가까운 빌보드 위치입니다. 없으면 `constant::invalid_index::kUint32` 입니다. */
        static uint32 findAt( const vector<EditorViewportBillboardItem>& listItem, const float2& point );
        /** @brief 배지 지름(픽셀 — 26 × DPI 배율)입니다. */
        static float32 getDiameter();
        /**
         * @brief 지난 그리기에서 @p object 의 배지에 실제로 넘긴 글리프의 코드 포인트입니다(감시 값 `Editor.BillboardIcon`).
         *        그리지 않았거나 글이 한 글자가 아니면(무효 메모리를 읽은 쓰레기 문자열) 0 입니다.
         */
        static uint32 findDrawnGlyph( const GameObject& object );
    };
} // namespace sw::editor
