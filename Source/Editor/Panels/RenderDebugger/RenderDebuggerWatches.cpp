/**
 * @file RenderDebuggerWatches.cpp
 * @brief 렌더 디버거 창의 자동화 감시 값입니다 — `Editor.RenderDebugger*`(시나리오 `editor.renderdebugger`).
 * @details 렌더 스레드가 프레임 경계에서 공개하는 값(기록 · 그래프 사본 · 버퍼 목록 · 읽기)은 그린 프레임을 따라 오므로 벽시계 감시 값(`SW_AUTOMATION_WALL_WATCH`)이고
 *          활동 수는 그린 프레임 수다 — 그리기가 멈추면 값도 오지 않는다(렌더 타깃 창 감시 값과 같다).
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Panels/RenderDebugger/RenderDebuggerPanel.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Renderer/Frame/View/RenderStability.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 감시 값의 갈래입니다(읽기 함수 하나가 창을 한 번 찾는다). */
        enum class RenderDebuggerWatchKind : uint8
        {
            Tab,
            EventCount,
            StopEvent,
            GraphPasses,
            GraphNodes,
            BufferCount,
            BufferRows,
        };

        /** @brief 렌더 디버거 창 값 하나를 읽습니다. 창이 열려 있지 않으면 값을 내지 않습니다. */
        [[nodiscard]] bool readRenderDebuggerValue( RenderDebuggerWatchKind kind, float64& outValue )
        {
            const EditorPanelManager*  pPanels = getWatchPanelManager( __func__ );
            const RenderDebuggerPanel* pPanel  = pPanels != nullptr ? pPanels->findPanel<RenderDebuggerPanel>() : nullptr;
            if ( pPanel == nullptr || pPanel->isOpen() == false )
                return false;
            switch ( kind )
            {
                case RenderDebuggerWatchKind::Tab:
                {
                    outValue = static_cast<float64>( pPanel->getTab() );
                    break;
                }
                case RenderDebuggerWatchKind::EventCount:
                {
                    outValue = static_cast<float64>( pPanel->getEventCount() );
                    break;
                }
                case RenderDebuggerWatchKind::StopEvent:
                {
                    outValue = static_cast<float64>( pPanel->getStopEventIndex() );
                    break;
                }
                case RenderDebuggerWatchKind::GraphPasses:
                {
                    outValue = static_cast<float64>( pPanel->getGraphPassCount() );
                    break;
                }
                case RenderDebuggerWatchKind::GraphNodes:
                {
                    outValue = static_cast<float64>( pPanel->getDrawnGraphNodeCount() );
                    break;
                }
                case RenderDebuggerWatchKind::BufferCount:
                {
                    outValue = static_cast<float64>( pPanel->getBufferCount() );
                    break;
                }
                case RenderDebuggerWatchKind::BufferRows:
                {
                    outValue = static_cast<float64>( pPanel->getBufferRowCount() );
                    break;
                }
            }
            return true;
        }

        /** @brief 렌더 스레드가 공개하는 값의 활동 수 — 그린 프레임 수입니다. */
        [[nodiscard]] uint64 readRenderDebuggerActivity( const GameObjectManager* /*pManager*/ )
        {
            return RenderPresentCounter::read();
        }

        [[nodiscard]] bool readRenderDebuggerTab( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderDebuggerValue( RenderDebuggerWatchKind::Tab, outValue );
        }

        [[nodiscard]] bool readRenderDebuggerEvents( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderDebuggerValue( RenderDebuggerWatchKind::EventCount, outValue );
        }

        [[nodiscard]] bool readRenderDebuggerStopEvent( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderDebuggerValue( RenderDebuggerWatchKind::StopEvent, outValue );
        }

        [[nodiscard]] bool readRenderDebuggerGraphPasses( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderDebuggerValue( RenderDebuggerWatchKind::GraphPasses, outValue );
        }

        [[nodiscard]] bool readRenderDebuggerGraphNodes( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderDebuggerValue( RenderDebuggerWatchKind::GraphNodes, outValue );
        }

        [[nodiscard]] bool readRenderDebuggerBuffers( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderDebuggerValue( RenderDebuggerWatchKind::BufferCount, outValue );
        }

        [[nodiscard]] bool readRenderDebuggerBufferRows( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderDebuggerValue( RenderDebuggerWatchKind::BufferRows, outValue );
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorRenderDebuggerTab, "Editor.RenderDebuggerTab", "Tab the Render Debugger panel shows: 0 frame debugger, 1 render graph, 2 buffers, 3 occlusion and streaming",
                         &readRenderDebuggerTab );
    SW_AUTOMATION_WALL_WATCH( editorRenderDebuggerEvents, "Editor.RenderDebuggerEvents", "Main-view scene draws the frame debugger recorded in the last published frame",
                              &readRenderDebuggerEvents, &readRenderDebuggerActivity );
    SW_AUTOMATION_WALL_WATCH( editorRenderDebuggerStopEvent, "Editor.RenderDebuggerStopEvent",
                              "Recorded draw the frame debugger stops at (0 = first); -1 while it records the whole frame or is off", &readRenderDebuggerStopEvent,
                              &readRenderDebuggerActivity );
    SW_AUTOMATION_WALL_WATCH( editorRenderDebuggerGraphPasses, "Editor.RenderDebuggerGraphPasses", "Passes of the render graph capture the Render Debugger panel shows",
                              &readRenderDebuggerGraphPasses, &readRenderDebuggerActivity );
    SW_AUTOMATION_WALL_WATCH( editorRenderDebuggerGraphNodes, "Editor.RenderDebuggerGraphNodes", "Pass nodes the Render Debugger graph canvas drew last frame (must equal the passes)",
                              &readRenderDebuggerGraphNodes, &readRenderDebuggerActivity );
    SW_AUTOMATION_WALL_WATCH( editorRenderDebuggerBuffers, "Editor.RenderDebuggerBuffers", "Frame graph buffers (FrameBufferTable) the Render Debugger panel lists",
                              &readRenderDebuggerBuffers, &readRenderDebuggerActivity );
    SW_AUTOMATION_WALL_WATCH( editorRenderDebuggerBufferRows, "Editor.RenderDebuggerBufferRows", "Rows of the buffer the Render Debugger panel read back (0 before the read arrives)",
                              &readRenderDebuggerBufferRows, &readRenderDebuggerActivity );
} // namespace sw::editor
