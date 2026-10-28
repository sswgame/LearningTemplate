#include "pch.h"

#include "Editor/Viewport/EditorViewportCanvasOverlay.h"

#include "Core/Foundation/Defines.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/Color.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Viewport/EditorViewportCamera.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/Model/EditorGridUtil.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportCanvasOverlayInternal
        {
            static constexpr Color kCubeBackground{ Color::fromBytes( 18, 22, 30, 200 ) };
            static constexpr Color kCubeRing{ Color::fromBytes( 55, 65, 85, 180 ) };
            static constexpr Color kAxisNegativeX{ Color::fromBytes( 130, 60, 60, 200 ) };
            static constexpr Color kAxisNegativeY{ Color::fromBytes( 50, 130, 70, 200 ) };
            static constexpr Color kAxisNegativeZ{ Color::fromBytes( 50, 70, 140, 200 ) };
            static constexpr Color kCubeHandleHovered{ Color::fromBytes( 255, 255, 255, 255 ) };
            static constexpr Color kCubeLabel{ Color::fromBytes( 15, 15, 20, 255 ) };
            static constexpr Color kRulerLine{ Color::fromBytes( 255, 215, 40, 240 ) };
            static constexpr Color kRulerAccent{ Color::fromBytes( 255, 230, 80, 255 ) };
            static constexpr Color kRulerLabelBg{ Color::fromBytes( 20, 24, 32, 220 ) };

            // 축 색의 원본이다. 그리드와 오리엔테이션 큐브가 같은 값을 읽는다. 기즈모(ImGuizmo)도 같은 관례(X 빨강, Y 초록, Z 파랑)를 쓴다.
            static constexpr Color kAxisX{ Color::fromBytes( 235, 65, 65 ) };
            static constexpr Color kAxisY{ Color::fromBytes( 65, 220, 95 ) };
            static constexpr Color kAxisZ{ Color::fromBytes( 65, 130, 245 ) };

            // 선 하나를 이 개수의 조각으로 나눠 그려서 가장자리가 흐려 보이게 한다. ImGui 의 선은 한 가지 색이다.
            static constexpr uint32  kSegmentCount    = 8;
            static constexpr float32 kMinVisibleAlpha = 0.01f;
            static constexpr float32 kMinorWidth      = 1.0f;
            static constexpr float32 kMajorWidth      = 1.5f;
            static constexpr float32 kByteMax         = 255.0f;
            static constexpr float32 kMinorColor[4]   = { 60.0f, 65.0f, 80.0f, 55.0f };
            static constexpr float32 kMajorColor[4]   = { 90.0f, 100.0f, 120.0f, 100.0f };

            /** @brief 격자 선 한 방향 묶음을 그리는 데 필요한 값입니다. u · v 는 격자 평면의 두 축입니다. */
            struct GridDrawContext
            {
                ImDrawList*            _pDrawList;
                const float4x4&        _viewProj;
                const float2&          _canvasPos;
                const float2&          _canvasSize;
                const EditorGridLevel& _level;
                EditorGridStats&       _stats;
                float32                _centerU;
                float32                _centerV;
                bool                   _b2D;
            };

            static float3 toWorld( bool b2D, float32 u, float32 v )
            {
                return b2D ? float3{ u, v, 0.0f } : float3{ u, 0.0f, v };
            }

            // bAlongV 는 선이 v 방향으로 뻗는다는 뜻이다(좌표는 u). 원점 선의 색은 3D 에서 x == 0 선이 Z 축, z == 0 선이 X 축이고, 2D 에서 x == 0 선이 Y 축이다.
            // 이 둘의 색을 바꿔 쓰면 그리드의 축 색이 오리엔테이션 큐브와 기즈모의 색과 달라진다.
            static void drawGridFamily( const GridDrawContext& ctx, bool bAlongV )
            {
                const EditorGridLevel& level     = ctx._level;
                const float32          radius    = level._radius;
                const float32          center    = bAlongV ? ctx._centerU : ctx._centerV;
                const float32          across    = bAlongV ? ctx._centerV : ctx._centerU;
                const int64            firstIdx  = static_cast<int64>( MathUtil::ceil( ( center - radius ) / level._step ) );
                const int64            lastIdx   = static_cast<int64>( MathUtil::floor( ( center + radius ) / level._step ) );
                const Color            axisColor = bAlongV ? ( ctx._b2D ? kAxisY : kAxisZ ) : kAxisX;

                for ( int64 worldIndex = firstIdx; worldIndex <= lastIdx; ++worldIndex )
                {
                    const float32 coordinate = static_cast<float32>( worldIndex ) * level._step;
                    const float32 offset     = coordinate - center;
                    const float32 halfChord  = MathUtil::sqrt( MathUtil::max( radius * radius - offset * offset, 0.0f ) );
                    if ( halfChord <= 0.0f )
                        continue;

                    const bool                bOrigin = ( worldIndex == 0 );
                    const EditorGridLineStyle style   = EditorGridUtil::evaluateLine( worldIndex, level );
                    if ( bOrigin == false && style._visibility < kMinVisibleAlpha )
                        continue;

                    float32 arrColor[4];
                    for ( uint32 channel = 0; channel < 4; ++channel )
                    {
                        arrColor[channel] = MathUtil::lerp( kMinorColor[channel], kMajorColor[channel], style._majorWeight );
                    }
                    const float32 lineAlpha = bOrigin ? 1.0f : style._visibility;
                    const float32 width     = bOrigin ? kMajorWidth : MathUtil::lerp( kMinorWidth, kMajorWidth, style._majorWeight );

                    if ( bOrigin == false && style._majorWeight >= 0.5f )
                    {
                        ++ctx._stats._majorLineCount;
                        const float32 majorPeriod = level._step * static_cast<float32>( EditorGridUtil::kMajorEvery );
                        const float32 phase       = MathUtil::abs( coordinate - MathUtil::round( coordinate / majorPeriod ) * majorPeriod );
                        if ( phase > 0.001f * level._step )
                            ++ctx._stats._misplacedMajorCount;
                    }

                    for ( uint32 segment = 0; segment < kSegmentCount; ++segment )
                    {
                        const float32 from     = -halfChord + 2.0f * halfChord * static_cast<float32>( segment ) / static_cast<float32>( kSegmentCount );
                        const float32 to       = -halfChord + 2.0f * halfChord * static_cast<float32>( segment + 1 ) / static_cast<float32>( kSegmentCount );
                        const float32 middle   = 0.5f * ( from + to );
                        const float32 edgeFade = EditorGridUtil::computeEdgeFade( MathUtil::sqrt( offset * offset + middle * middle ), radius );
                        const float32 alpha    = lineAlpha * edgeFade;
                        if ( alpha < kMinVisibleAlpha )
                            continue;

                        ImU32 color = 0;
                        if ( bOrigin )
                        {
                            color = EditorColorUtil::toImU32( axisColor.scaleAlpha( alpha ) );
                        }
                        else
                        {
                            // 바이트 단위(0~255)로 자른 값을 0~1 로 되돌려 같은 ImU32 가 되게 한다.
                            const float32 red       = static_cast<float32>( static_cast<uint8>( arrColor[0] ) ) / kByteMax;
                            const float32 green     = static_cast<float32>( static_cast<uint8>( arrColor[1] ) ) / kByteMax;
                            const float32 blue      = static_cast<float32>( static_cast<uint8>( arrColor[2] ) ) / kByteMax;
                            const float32 alphaUnit = static_cast<float32>( static_cast<uint8>( arrColor[3] * alpha + 0.5f ) ) / kByteMax;
                            color                   = EditorColorUtil::toImU32( Color{ red, green, blue, alphaUnit } );
                        }

                        const float3 worldA = bAlongV ? toWorld( ctx._b2D, coordinate, across + from ) : toWorld( ctx._b2D, across + from, coordinate );
                        const float3 worldB = bAlongV ? toWorld( ctx._b2D, coordinate, across + to ) : toWorld( ctx._b2D, across + to, coordinate );
                        ImVec2       screenA, screenB;
                        if ( EditorViewportProjectionUtil::projectSegment( ctx._viewProj, worldA, worldB, ctx._canvasPos, ctx._canvasSize, screenA, screenB ) )
                            ctx._pDrawList->AddLine( screenA, screenB, color, width );
                    }
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorViewportCanvasOverlay::EditorViewportCanvasOverlay()
        : _gridStats{}
        , _rulerStartWorld{ 0.0f, 0.0f, 0.0f }
        , _rulerEndWorld{ 0.0f, 0.0f, 0.0f }
        , _bRulerActive{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void EditorViewportCanvasOverlay::drawOrientationCube( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, float32 topInset,
                                                           EditorViewportCamera& camera )
    {
        if ( pDrawList == nullptr )
            return;

        const float32     cubeCenterX = canvasPos._x + canvasSize._x - 45.0f;
        const float32     cubeCenterY = canvasPos._y + topInset + 45.0f; // HUD 박스 아래
        constexpr float32 cubeRadius  = 26.0f;

        // 원형 배경
        pDrawList->AddCircleFilled( ImVec2( cubeCenterX, cubeCenterY ), cubeRadius + 6.0f,
                                    EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kCubeBackground ) );
        pDrawList->AddCircle( ImVec2( cubeCenterX, cubeCenterY ), cubeRadius + 6.0f, EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kCubeRing ), 0,
                              1.5f );

        const float3& rotation = camera.getRotation();
        const float32 pitchRad = MathUtil::toRadian( rotation._x );
        const float32 yawRad   = MathUtil::toRadian( rotation._y );

        const float3 forward{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ),
                              MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
        const float3 right{ MathUtil::cos( yawRad ), 0.0f, -MathUtil::sin( yawRad ) };
        const float3 up{ right._y * forward._z - right._z * forward._y, right._z * forward._x - right._x * forward._z,
                         right._x * forward._y - right._y * forward._x };

        struct AxisItem
        {
            float3      _dir;
            ImU32       _color;
            const utf8* _pLabel;
            float32     _depth;
            float2      _screenOffset;
            float3      _targetRot;
        };

        AxisItem arrAxis[6] = {
            { float3{ 1.0f, 0.0f, 0.0f },         EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kAxisX ),  "X", 0.0f, float2{},
             float3{ 0.0f, -90.0f, 0.0f }},
            {float3{ -1.0f, 0.0f, 0.0f }, EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kAxisNegativeX ), "-X", 0.0f, float2{},
             float3{ 0.0f, 90.0f, 0.0f } },
            { float3{ 0.0f, 1.0f, 0.0f },         EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kAxisY ),  "Y", 0.0f, float2{},
             float3{ 89.0f, 0.0f, 0.0f } },
            {float3{ 0.0f, -1.0f, 0.0f }, EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kAxisNegativeY ), "-Y", 0.0f, float2{},
             float3{ -89.0f, 0.0f, 0.0f }},
            { float3{ 0.0f, 0.0f, 1.0f },         EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kAxisZ ),  "Z", 0.0f, float2{},
             float3{ 0.0f, 180.0f, 0.0f }},
            {float3{ 0.0f, 0.0f, -1.0f }, EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kAxisNegativeZ ), "-Z", 0.0f, float2{},
             float3{ 0.0f, 0.0f, 0.0f }  }
        };

        for ( uint32 axisIndex = 0; axisIndex < 6; ++axisIndex )
        {
            AxisItem&     ax   = arrAxis[axisIndex];
            const float32 dotR = ax._dir._x * right._x + ax._dir._y * right._y + ax._dir._z * right._z;
            const float32 dotU = ax._dir._x * up._x + ax._dir._y * up._y + ax._dir._z * up._z;
            const float32 dotF = ax._dir._x * forward._x + ax._dir._y * forward._y + ax._dir._z * forward._z;

            ax._depth        = dotF;
            ax._screenOffset = float2{ dotR * cubeRadius * 0.78f, -dotU * cubeRadius * 0.78f };
        }

        // 깊이 오름차순으로 정렬해 먼 것부터 그린다
        std::sort( std::begin( arrAxis ), std::end( arrAxis ),
                   []( const AxisItem& a, const AxisItem& b )
        { return a._depth < b._depth; } );

        const ImVec2 mousePos = ImGui::GetMousePos();

        for ( uint32 axisIndex = 0; axisIndex < 6; ++axisIndex )
        {
            const AxisItem& ax = arrAxis[axisIndex];
            const ImVec2    pt( cubeCenterX + ax._screenOffset._x, cubeCenterY + ax._screenOffset._y );

            // 중심에서 뻗는 축 선
            pDrawList->AddLine( ImVec2( cubeCenterX, cubeCenterY ), pt, ax._color, 1.8f );

            // 원판 손잡이
            const float32 handleRadius = ( ax._depth > 0.0f ) ? 6.5f : 4.5f;
            const float32 distToMouse  = float2::getDistance( float2{ mousePos.x, mousePos.y }, float2{ pt.x, pt.y } );
            const bool    bHovered     = ( distToMouse <= handleRadius + 2.0f );

            pDrawList->AddCircleFilled( pt, handleRadius, bHovered ? EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kCubeHandleHovered ) : ax._color );

            if ( ax._depth > -0.2f && ax._pLabel[0] != '-' )
                pDrawList->AddText( ImVec2( pt.x - 3.5f, pt.y - 6.0f ), EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kCubeLabel ), ax._pLabel );

            if ( bHovered && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
                camera.orbitTo( ax._targetRot );
        }
    }

    void EditorViewportCanvasOverlay::drawAdaptiveGrid( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float32* pView,
                                                        const float32* pProj, const float3& cameraPosition, bool b2DMode )
    {
        const bool bInvalidOverlayInputs = pDrawList == nullptr || pView == nullptr || pProj == nullptr;
        if ( bInvalidOverlayInputs )
            return;

        float4x4 viewProj{};
        EditorViewportProjectionUtil::loadViewProjection( pView, pProj, viewProj );

        // 격자 평면의 두 축(u, v)과 평면까지의 거리다. 2D 는 XY 평면(직교 뷰이며 보이는 반 높이로 단계를 고른다), 3D 는 XZ 바닥이다.
        const bool    b2D        = b2DMode;
        const bool    bOrtho     = MathUtil::abs( pProj[15] - 1.0f ) < 0.001f;
        const float32 centerU    = cameraPosition._x;
        const float32 centerV    = b2D ? cameraPosition._y : cameraPosition._z;
        const float32 viewHeight = bOrtho && MathUtil::abs( pProj[5] ) > 1e-6f ? 2.0f / MathUtil::abs( pProj[5] ) : ( b2D ? cameraPosition._z : cameraPosition._y );

        const EditorGridLevel level = EditorGridUtil::selectLevel( viewHeight );

        EditorGridStats& stats     = _gridStats;
        stats._step                = level._step;
        stats._cameraPos           = cameraPosition;
        stats._majorLineCount      = 0;
        stats._misplacedMajorCount = 0;
        stats._frame               = ImGui::GetFrameCount();

        const EditorViewportCanvasOverlayInternal::GridDrawContext context{ pDrawList, viewProj, canvasPos, canvasSize, level, stats, centerU, centerV, b2D };
        EditorViewportCanvasOverlayInternal::drawGridFamily( context, true );
        EditorViewportCanvasOverlayInternal::drawGridFamily( context, false );
    }

    void EditorViewportCanvasOverlay::processRulerTool( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float32* pView,
                                                        const float32* pProj, bool bShowRuler )
    {
        const bool bInvalidRulerInputs = pDrawList == nullptr || pView == nullptr || pProj == nullptr;
        if ( bInvalidRulerInputs )
            return;

        if ( EditorInputGUI::isActionDown( "viewport.rulerHold" ) == false && bShowRuler == false )
        {
            _bRulerActive = SW_FALSE;
            return;
        }

        float4x4 viewProj{};
        EditorViewportProjectionUtil::loadViewProjection( pView, pProj, viewProj );

        // 마우스 아래 바닥(Y = 0)의 점이 자의 엔드포인트다.
        EditorPickRay mouseRay{};
        float3        groundPt{};
        if ( EditorViewportProjectionUtil::makeMouseRay( viewProj.invert(), canvasPos, canvasSize, mouseRay ) &&
             EditorViewportRayPick::rayHitsAxisPlane( mouseRay, 1, groundPt ) )
        {
            if ( EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
            {
                _rulerStartWorld = groundPt;
                _rulerEndWorld   = groundPt;
                _bRulerActive    = SW_TRUE;
            }
            else if ( EditorInputGUI::readPointer().isDown( EditorMouseButton::Left ) && _bRulerActive == SW_TRUE )
            {
                _rulerEndWorld = groundPt;
            }
        }

        if ( _bRulerActive == SW_TRUE )
        {
            ImVec2 sStart, sEnd;
            if ( EditorViewportProjectionUtil::projectPoint( viewProj, _rulerStartWorld, canvasPos, canvasSize, sStart ) &&
                 EditorViewportProjectionUtil::projectPoint( viewProj, _rulerEndWorld, canvasPos, canvasSize, sEnd ) )
            {
                // 측정선
                pDrawList->AddLine( sStart, sEnd, EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kRulerLine ), 2.5f );
                pDrawList->AddCircleFilled( sStart, 5.0f, EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kRulerAccent ) );
                pDrawList->AddCircleFilled( sEnd, 5.0f, EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kRulerAccent ) );

                const float3  delta = _rulerEndWorld - _rulerStartWorld;
                const float32 dist  = delta.getLength();

                fixed_string<constant::kMaxBuffer64> arrDistText;
                formatstring( arrDistText.data(), arrDistText.capacity(), "%# m (dX: %#, dZ: %#)",
                              Fmt( static_cast<float64>( dist ), Format().precision( 2 ) ), Fmt( static_cast<float64>( delta._x ), Format().precision( 2 ) ),
                              Fmt( static_cast<float64>( delta._z ), Format().precision( 2 ) ) );

                const ImVec2 mid( ( sStart.x + sEnd.x ) * 0.5f, ( sStart.y + sEnd.y ) * 0.5f - 16.0f );
                pDrawList->AddRectFilled( ImVec2( mid.x - 4.0f, mid.y - 2.0f ), ImVec2( mid.x + 160.0f, mid.y + 18.0f ),
                                          EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kRulerLabelBg ), 4.0f );
                pDrawList->AddText( mid, EditorColorUtil::toImU32( EditorViewportCanvasOverlayInternal::kRulerAccent ), arrDistText.c_str() );
            }
        }
    }
} // namespace sw::editor
