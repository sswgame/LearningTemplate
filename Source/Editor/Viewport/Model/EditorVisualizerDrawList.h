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

#include "Editor/Common/Config/EditorThemeKey.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"

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
        ThemeToken,       ///< 값의 `_themeKey`(테마 토큰 이름, 예 `Viewport.Label`) — 확장 시각화가 제 토큰을 쓴다
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
        EditorThemeColorKey       _themeKey{}; ///< `ThemeToken` 역할의 테마 토큰 이름(`SW_THEME_COLOR_KEY`)
    };

    /** @brief 월드 위치에 그리는 글자 하나입니다. */
    struct EditorVisualizerText
    {
        float3                    _position{};
        float4                    _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        string                    _text;
        EditorVisualizerStatSlot  _statSlot{ EditorVisualizerStatSlot::None };
        EditorVisualizerColorRole _colorRole{ EditorVisualizerColorRole::Explicit };
        EditorThemeColorKey       _themeKey{}; ///< `ThemeToken` 역할의 테마 토큰 이름(`SW_THEME_COLOR_KEY`)
        bool                      _bShadow{ true };
    };

    /**
     * @brief 메시가 없는 컴포넌트(빛 · 카메라 · 오디오 …)의 아이콘 배지 하나입니다 — 언리얼 `UBillboardComponent` · 유니티 Gizmo 아이콘 · Godot 3D 기즈모 아이콘 자리.
     * @details 모으는 쪽(빌보드 시각화)이 캔버스 안에 보이는 것만 오브젝트마다 하나 낸다. 그리는 쪽이 배지를 긋고 그 화면 자리를 `EditorVisualizerScreenBillboard` 로 남겨 클릭 선택이 쓴다.
     */
    struct EditorVisualizerBillboard
    {
        uint64                 _objectID{ 0 };
        uint64                 _componentID{ 0 };
        EditorComponentIconRow _row{};      ///< 값으로 보관한다 — `findRow` 는 값을 돌려주므로 그 주소는 모으기가 끝나면 무효다
        float3                 _position{}; ///< 배지 가운데(월드)
        string                 _name;       ///< 오브젝트 이름(셀프테스트 마크 `viewport.billboard.<이름>`)
    };

    /** @brief 그리는 쪽이 이번 프레임에 그은 배지의 화면 자리입니다. 피커가 레이 피킹보다 먼저 본다(배지가 맞으면 그 컴포넌트를 고른다). */
    struct EditorVisualizerScreenBillboard
    {
        uint64  _objectID{ 0 };
        uint64  _componentID{ 0 };
        float2  _screen{};       ///< 배지 가운데(캔버스 화면 좌표)
        float32 _radius{ 0.0f }; ///< 배지 반지름(픽셀 — DPI 배율을 먹였다)
        uint32  _codePoint{ 0 }; ///< 넘긴 글리프의 코드 포인트. 글이 한 글자가 아니면(무효 메모리를 읽은 쓰레기 문자열) 0 — 감시 값 `Editor.BillboardIcon`
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

        vector<EditorVisualizerLine>      _listLine;
        vector<EditorVisualizerText>      _listText;
        vector<EditorVisualizerBillboard> _listBillboard;
        bool                              _bDebugDrawGathered{ false };

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
