/**
 * @file DebugDrawVisualizer.cpp
 * @brief 게임 코드가 `DebugDrawQueue` 에 넣은 선 · 구 · 상자 · 화살표 · 글자를 뷰포트에 그립니다(2D · 3D 뷰 모두)
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerGeometry.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Profiling/DebugOverlayState.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct DebugDrawVisualizerInternal
        {
            /** @brief 글자 그림자 색입니다(밝은 배경에서도 읽히게). */
            static constexpr Color kTextShadow{ Color::fromBytes( 0, 0, 0, 200 ) };

            /** @brief 선분을 펼치는 재사용 버퍼들입니다(에디터 UI 는 게임 스레드 하나). 에디터 범위의 상태 저장소가 소유한다. */
            struct Scratch
            {
                vector<DebugLine>          _listLine;
                vector<EditorWorldSegment> _listSegment;
            };

            static ImU32 toColor( const float4& color )
            {
                return EditorColorUtil::toImU32( Color::fromFloat4( color ) );
            }

            /**
             * @brief 디버그 큐가 확정한 도형을 그립니다. 구는 대원 셋(2D 뷰는 XY 원 하나), 선분은 근평면에서 잘라 투영합니다.
             * @details 큐는 `Engine::endFrame` 이 지난 프레임에 넣은 것을 확정합니다 — 게임 업데이트든 씬 틱이든 넣은 것이 모두 보인다.
             *          재사용 버퍼 하나로 충분합니다(에디터 UI 는 게임 스레드 하나에서 돈다).
             */
            static void draw( const EditorViewportVisualizerArgs& args )
            {
                EditorDebugDrawStats& stats = args._pStats->_debugDraw;
                stats._segmentCount         = 0;
                stats._textCount            = 0;
                stats._frame                = ImGui::GetFrameCount();
                if ( args._pDebugDrawQueue == nullptr )
                    return;

                Scratch& scratch = getEditorState<Scratch>();
                scratch._listSegment.clear();
                EditorVisualizerGeometryUtil::appendDebugDrawSegments( *args._pDebugDrawQueue, args._bFlat2D, scratch._listLine, scratch._listSegment );
                for ( const EditorWorldSegment& segment : scratch._listSegment )
                {
                    ImVec2 screenFrom;
                    ImVec2 screenTo;
                    if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, segment._from, segment._to, args._canvasPos, args._canvasSize, screenFrom,
                                                                       screenTo ) == false )
                        continue;
                    args._pDrawList->AddLine( screenFrom, screenTo, toColor( segment._color ), 1.5f );
                    ++stats._segmentCount;
                }

                for ( const DebugText& text : args._pDebugDrawQueue->getVisibleTexts() )
                {
                    ImVec2 screen;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, text._position, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    // 글자 그림자를 먼저 깔아 밝은 배경에서도 읽히게 한다.
                    args._pDrawList->AddText( ImVec2( screen.x + 1.0f, screen.y + 1.0f ), EditorColorUtil::toImU32( kTextShadow ), text._text.c_str() );
                    args._pDrawList->AddText( screen, toColor( text._color ), text._text.c_str() );
                    ++stats._textCount;
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( DebugDraw, "debug_draw", 300, editoricon::kBug, "Dbg", "게임 코드가 DebugDrawQueue 에 넣은 선 · 구 · 상자 · 화살표 · 글자를 표시합니다", true,
                          &DebugDrawVisualizerInternal::draw );
} // namespace sw::editor

namespace sw::editor
{
    // 콘솔 명령 — 시나리오 · 콘솔 입력 줄이 부른다(`DevCommandRegistry`). 그 기능 옆에 둔다(언리얼 Exec 명령이 그 시스템 파일에 있는 것과 같다).
    namespace
    {
        struct DebugDrawDemoConsoleInternal
        {
            /**
             * @brief 씬 뷰 카메라 앞 6 m 에 디버그 도형 네 개(상자, 화살표, 구, 글자)와 오버레이 값 하나를 둡니다. 씬 뷰 시각화와 게임 뷰 HUD 가 동작하는지 눈으로 확인할 때 씁니다.
             */
            static bool runDebugDrawDemo( const vector<string>& listArgument, string& outReply )
            {
                uint32 seconds = 10;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && DevCommandRegistry::readCountArgument( listArgument[0], seconds ) == false ) )
                    return false;
                const CameraComponent* pCamera = EditorCamera::ensure( editor::getActiveScene() );
                if ( pCamera == nullptr )
                    return false;
                DebugDrawQueue* pQueue   = &editor::getSubsystem<DebugDrawQueue>();
                const float4x4  world    = pCamera->getWorldMatrix();
                const float3    forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world ).normalize();
                const float3    right    = float3::transformVector( float3{ 1.0f, 0.0f, 0.0f }, world ).normalize();
                const float3    center   = pCamera->getWorldPosition() + forward * 6.0f;
                const float32   duration = static_cast<float32>( seconds );
                pQueue->drawBox( center - right * 1.5f, float3{ 0.5f, 0.5f, 0.5f }, float4{ 1.0f, 0.4f, 0.1f, 1.0f }, duration, "Demo" );
                pQueue->drawSphere( center + right * 1.5f, 0.6f, float4{ 0.2f, 0.8f, 1.0f, 1.0f }, duration, "Demo" );
                pQueue->drawArrow( center - right * 0.6f, center + right * 0.6f, float4{ 0.3f, 1.0f, 0.3f, 1.0f }, duration, "Demo" );
                pQueue->drawText( center + float3{ 0.0f, 1.0f, 0.0f }, "DebugDrawQueue demo", float4{ 1.0f, 1.0f, 0.3f, 1.0f }, duration, "Demo" );
                editor::getSubsystem<DebugOverlayState>().setFloat( hashed_string( "debugdraw.demo.seconds" ), duration );
                outReply = formatToString( "debug draw demo for %# s", seconds );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( DebugDrawDemo, "debugdraw.demo", "debugdraw.demo [seconds]", "Draw a box, sphere, arrow and text in front of the scene view camera",
                    &DebugDrawDemoConsoleInternal::runDebugDrawDemo );
} // namespace sw::editor
