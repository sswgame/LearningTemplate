/**
 * @file EditorVisualizerDrawList.h
 * @brief 뷰포트 시각화가 모은 도형 값 목록(선 · 글자)입니다. 모으는 쪽(Model, 컴포넌트를 읽는다)이 채우고 그리는 쪽(View, `EditorViewportVisualizer::drawAll`)은 이 값만 읽습니다.
 * @details 언리얼의 컴포넌트 비주얼라이저가 `FPrimitiveDrawInterface` 에 선 · 점 값을 넘기고 그리기는 렌더 쪽이 하는 가름과 같다. ImGui 를 모른다.
 *          테마 색은 그리는 쪽이 푼다 — 값에는 색 역할(`EditorVisualizerColorRole`)만 든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Physics/PhysicsDebugDraw.h"

namespace sw
{
    struct AABB;
} // namespace sw

namespace sw::editor
{
    struct EditorViewportVisualizerArgs;
    class EditorVisualizerToggles;

    /** @brief 선 색을 어디서 정하는지입니다. `Explicit` 이 아니면 그리는 쪽이 테마 토큰으로 풀고 값의 `_color` 알파를 곱한다(값에는 테마가 없다). */
    enum class EditorVisualizerColorRole : uint8
    {
        Explicit,         ///< 값의 `_color` 를 그대로 쓴다
        ColliderWire,     ///< 테마 `Viewport.ColliderWire`
        CameraWire,       ///< 테마 `Viewport.CameraWire`
        SelectionOutline, ///< 테마 `Viewport.SelectionOutline`
        Hover,            ///< 테마 뷰포트 호버 색
        AxisX,            ///< 테마 `Viewport.AxisX`
        AxisY,            ///< 테마 `Viewport.AxisY`
        AxisZ,            ///< 테마 `Viewport.AxisZ`
    };

    /** @brief 그린 선 · 글자를 어느 통계(`EditorVisualizerStats`)에 셀지입니다. 통계는 화면에 실제로 그린 수(근평면에서 다 잘린 것은 빼고)라 그리는 쪽이 센다. */
    enum class EditorVisualizerStatSlot : uint8
    {
        None,
        PhysicsShape,
        NavMesh,
        DebugDraw,
    };

    /** @brief 월드 선분 하나입니다. */
    struct EditorVisualizerLine
    {
        float3                    _from{};
        float3                    _to{};
        float4                    _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float32                   _thickness{ 1.5f };
        EditorVisualizerColorRole _colorRole{ EditorVisualizerColorRole::Explicit };
        EditorVisualizerStatSlot  _statSlot{ EditorVisualizerStatSlot::None };
    };

    /** @brief 월드 위치에 그리는 글자 하나입니다(그림자를 깔아 그린다). */
    struct EditorVisualizerText
    {
        float3                   _position{};
        float4                   _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        string                   _text;
        EditorVisualizerStatSlot _statSlot{ EditorVisualizerStatSlot::None };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorVisualizerDrawList
     * @brief 한 프레임에 모은 시각화 도형입니다. 뷰포트가 값으로 들고 프레임마다 `clear` 해 다시 쓴다(용량은 남는다).
     */
    struct SW_EDITOR_API EditorVisualizerDrawList
    {
        /** @brief 꺾은선이 선분을 내는 가장 적은 점 수입니다. */
        static constexpr uint32 kMinLoopPointCount = 2;

        vector<EditorVisualizerLine> _listLine;
        vector<EditorVisualizerText> _listText;
        bool                         _bDebugDrawGathered{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 디버그 드로우 시각화가 이번 프레임에 돌았다(그리는 쪽이 프레임 번호를 적는다)

        /** @brief 목록을 비웁니다(용량은 남긴다). */
        void clear();
        /** @brief 선분 하나를 더합니다. */
        void addLine( const EditorVisualizerLine& line ) { _listLine.push_back( line ); }
        /** @brief 상자의 12 모서리를 선분으로 더합니다. @p prototype 의 색 · 굵기 · 역할 · 통계를 모든 모서리에 쓴다. */
        void addBox( const AABB& box, const EditorVisualizerLine& prototype );
        /** @brief 닫힌 꺾은선(마지막 점에서 첫 점으로 잇는다)을 더합니다. */
        void addLoop( const float3* pArrPoint, uint32 pointCount, const EditorVisualizerLine& prototype );
        /** @brief 비운 뒤 켜진 시각화의 모으기 함수(`EditorVisualizerRegistration::_pGather`)를 차례로 불러 채웁니다(메인 스레드, 씬을 읽는다). */
        void gatherAll( const EditorViewportVisualizerArgs& args, const EditorVisualizerToggles& toggles );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorVisualizerLineSink
     * @brief 물리 · 내비메시의 디버그 선 출구(`IPhysicsDebugRenderer`)를 도형 목록으로 받습니다. 엔진은 그리는 쪽을 모르므로 선 값만 넘긴다.
     */
    class SW_EDITOR_API EditorVisualizerLineSink final : public IPhysicsDebugRenderer
    {
    public:
        EditorVisualizerLineSink( EditorVisualizerDrawList& drawList, EditorVisualizerStatSlot statSlot )
            : _drawList{ drawList }
            , _statSlot{ statSlot }
        {
        }

        void drawLine( const float3& from, const float3& to, const float4& color ) override;

    private:
        EditorVisualizerDrawList& _drawList;
        EditorVisualizerStatSlot  _statSlot;
    };
} // namespace sw::editor
