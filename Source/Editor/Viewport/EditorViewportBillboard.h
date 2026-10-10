/**
 * @file EditorViewportBillboard.h
 * @brief 메시가 없는 컴포넌트(빛 · 카메라 · 오디오 …)를 뷰포트에 아이콘 배지로 보이고, 눌러 고르게 합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

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
    struct EditorComponentIconRow;

    /** @brief 이번 프레임에 화면에 보이는 빌보드 하나입니다. */
    struct EditorViewportBillboardItem
    {
        GameObject*                   _pObject{ nullptr };
        Component*                    _pComponent{ nullptr };
        const EditorComponentIconRow* _pRow{ nullptr };
        float2                        _screen{}; ///< 배지 가운데(캔버스 화면 좌표)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorViewportBillboard
     * @brief 언리얼 UBillboardComponent(에디터 전용 스프라이트) · 유니티 Gizmo 아이콘 · Godot 3D 기즈모 아이콘의 자리입니다.
     * @details 그리기(시각화 `billboard`, 툴바 "Icons")와 클릭 선택(`EditorViewportClient::processPicking` 이 레이 피킹보다 먼저)이 같은 `collect` 를 씁니다.
     *          씬 전체를 훑지 않고 등록부(카메라 · 빛 · 바람)만 봅니다 — 오브젝트 8000 개 벤치에서 모든 컴포넌트를 훑으면 Debug 로 프레임마다 약 10 ms 였다.
     *          그래서 등록부가 없는 종류(오디오 · 2D 빛)는 아직 빌보드가 없다. 씬 뷰 카메라 자신은 화면 가운데를 가리므로 건너뜁니다.
     */
    struct SW_EDITOR_API EditorViewportBillboard
    {
        /** @brief 시각화 등록 id 입니다(툴바 켬/끔과 클릭 선택이 같이 본다). */
        static constexpr const utf8* kVisualizerID = "billboard";

        /** @brief 등록부의 빌보드 종류 컴포넌트 중 화면에 들어오는 것을 모읍니다(먼저 비운다). 한 오브젝트에 둘이면 먼저 찾은 하나만입니다. */
        static void collect( const GameObjectManager& manager, const float4x4& viewProj, const float2& canvasPos, const float2& canvasSize,
                             const CameraComponent* pSkipCamera, vector<EditorViewportBillboardItem>& outListItem );
        /** @brief 화면 점 @p point 에서 배지 반지름 안의 가장 가까운 빌보드 자리입니다. 없으면 `invalid_index::kUint32` 입니다. */
        static uint32 findAt( const vector<EditorViewportBillboardItem>& listItem, const float2& point );
        /** @brief 배지 지름(픽셀 — 26 × DPI 배율)입니다. */
        static float32 getDiameter();
    };
} // namespace sw::editor
