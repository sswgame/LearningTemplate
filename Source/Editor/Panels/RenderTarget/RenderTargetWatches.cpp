/**
 * @file RenderTargetWatchs.cpp
 * @brief 렌더 타깃 패널의 자동화 감시 값입니다 — `Editor.RenderTarget*`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/RenderTarget/RenderTargetPanel.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 렌더 타깃 창 값 하나를 읽습니다. 열려 있지 않으면 값을 내지 않으며, 픽셀 값은 읽은 적이 없으면 내지 않습니다. */
        [[nodiscard]] bool readRenderTargetPanelValue( uint32 which, float64& outValue )
        {
            EditorContext*           pContext = getWatchContext( __func__ );
            const RenderTargetPanel* pPanel   = pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<RenderTargetPanel>()
                                                                    : nullptr;
            if ( pPanel == nullptr || pPanel->isOpen() == false )
                return false;
            switch ( which )
            {
                case 0:
                {
                    outValue = pPanel->hasSnapshot() ? 1.0 : 0.0;
                    break;
                }
                case 1:
                {
                    outValue = pPanel->isSelectedDepth() ? 1.0 : 0.0;
                    break;
                }
                case 2:
                {
                    outValue = static_cast<float64>( pPanel->getChannel() );
                    break;
                }
                case 3:
                {
                    outValue = pPanel->isDepthLinearized() ? 1.0 : 0.0;
                    break;
                }
                default:
                {
                    if ( pPanel->hasWatchdPixel() == false )
                        return false;
                    outValue = static_cast<float64>( pPanel->getWatchdPixel()._x );
                    break;
                }
            }
            return true;
        }

        [[nodiscard]] bool readRenderTargetSnapshotReady( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderTargetPanelValue( 0, outValue );
        }

        [[nodiscard]] bool readRenderTargetSelectedDepth( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderTargetPanelValue( 1, outValue );
        }

        [[nodiscard]] bool readRenderTargetChannel( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderTargetPanelValue( 2, outValue );
        }

        [[nodiscard]] bool readRenderTargetLinearized( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderTargetPanelValue( 3, outValue );
        }

        [[nodiscard]] bool readRenderTargetPixelRed( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readRenderTargetPanelValue( 4, outValue );
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorRenderTargetSnapshotReady, "Editor.RenderTargetSnapshotReady", "1 when the Render Targets panel holds a CPU snapshot of the picked target",
                         &readRenderTargetSnapshotReady );
    SW_AUTOMATION_WATCH( editorRenderTargetSelectedDepth, "Editor.RenderTargetSelectedDepth", "1 when the Render Targets panel shows a depth attachment",
                         &readRenderTargetSelectedDepth );
    SW_AUTOMATION_WATCH( editorRenderTargetChannel, "Editor.RenderTargetChannel", "Channel the Render Targets panel shows: 0 RGBA, 1 R, 2 G, 3 B, 4 A",
                         &readRenderTargetChannel );
    SW_AUTOMATION_WATCH( editorRenderTargetLinearized, "Editor.RenderTargetLinearized", "1 when the Render Targets panel linearizes depth",
                         &readRenderTargetLinearized );
    SW_AUTOMATION_WATCH( editorRenderTargetPixelRed, "Editor.RenderTargetPixelRed",
                         "First channel (depth for a depth target) of the pixel last read under the mouse in the Render Targets panel; no value before one is read",
                         &readRenderTargetPixelRed );
} // namespace sw::editor
