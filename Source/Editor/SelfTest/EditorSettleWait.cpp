#include "pch.h"

#include "Editor/SelfTest/EditorSettleWait.h"

#include "DevTools/Client/Automation/Scenario/AutomationEnvironmentSteps.h"

#include "Editor/Common/GUI/Panel/EditorDockLayout.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Renderer/Capture/CaptureReadiness.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    EditorSettleWait::EditorSettleWait()
        : _waitSerial{ 0 }
        , _judge{}
        , _waitingFor{}
    {
    }

    EditorSettleResult EditorSettleWait::advance( GameObjectManager* pActiveManager )
    {
        // 번호가 없거나(상태 저장소가 아직 없었다) 지금 번호보다 크면 상태 저장소가 새로 섰다 — 거기서 다시 기다린다.
        if ( _waitSerial == 0 || _waitSerial > EditorSelfTestInput::getSettleWaitSerial() )
        {
            _waitSerial = EditorSelfTestInput::beginSettleWait();
            _judge      = EditorSettleState{};
        }
        const EditorDockLayout* pDockLayout    = editor::findSubsystem<EditorDockLayout>();
        const bool              bScreenSettled = AutomationEnvironmentSteps::isWindowResizeApplied() && pDockLayout != nullptr && pDockLayout->isApplyPending() == false &&
                                    EditorSelfTestInput::hasPendingEvents() == false && EditorSelfTestInput::isLayoutSettledAfter( _waitSerial );
        const bool                               bSceneSettled  = editor::getSubsystem<SceneManager>().isTransitioning() == false;
        const bool                               bAssetsSettled = CaptureReadiness::countPendingWork( pActiveManager ) == 0;
        const utf8* const                        pPendingWork   = AutomationStepRegistry::findFinishBlocker(); // 뒤에 끝날 일(저장 · 스크린샷 · 캡처 쓰기) — 모듈이 제 일을 등록한다
        StringBuilder<constant::buffer::kMax256> waitingForBuilder;
        waitingForBuilder.appendFormat( "the editor to settle (screen %#, scene %#, assets %#, work %#)",
                                        bScreenSettled ? "settled" : ( "moving: " + EditorSelfTestInput::describeLastLayoutChange() ).c_str(),
                                        bSceneSettled ? "settled" : "transitioning", bAssetsSettled ? "settled" : "pending", pPendingWork != nullptr ? pPendingWork : "none" );
        _waitingFor = string( waitingForBuilder.view() );
        return _judge.advance( bScreenSettled, bSceneSettled && bAssetsSettled && pPendingWork == nullptr );
    }

    string EditorSettleWait::describeOscillation() const
    {
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "the editor screen kept moving for %# frame(s) with no pending work (%#) - a layout that never settles", _judge._idleMovingFrames,
                                    EditorSelfTestInput::describeLastLayoutChange().c_str() );
        return string( resultBuilder.view() );
    }
} // namespace sw::editor
