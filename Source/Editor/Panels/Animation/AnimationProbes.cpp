/**
 * @file AnimationProbes.cpp
 * @brief 애니메이션 그래프 · 시퀀서 패널의 자동화 탐침입니다 — `Editor.AnimationGraph*Count` · `Editor.SequencerKeyFrameCount` · `Editor.ObjectPositionX`(시퀀서 키 트랙 미리보기가 옮긴 값).
 * @details 탐침 레지스트리(`Engine/Automation/Probe`)가 기능 층보다 아래라 기능이 자기 탐침을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorProbeSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Animation/AnimationGraphPanel.h"
#include "Editor/Panels/Animation/SequencerPanel.h"
#include "Editor/SelfTest/EditorProbeSupport.h"

#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 애니메이션 그래프 패널의 개수 하나를 읽습니다(@p count 는 패널의 개수 함수). 패널이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readAnimationGraphCount( uint32 ( AnimationGraphPanel::*count )() const, float64& outValue )
        {
            EditorContext* pContext = getProbeContext( __func__ );
            if ( pContext == nullptr )
                return false;
            const AnimationGraphPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<AnimationGraphPanel>();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( ( pPanel->*count )() );
            return true;
        }

        [[nodiscard]] bool readAnimationGraphLinkCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readAnimationGraphCount( &AnimationGraphPanel::getLinkCount, outValue );
        }

        [[nodiscard]] bool readAnimationGraphConditionCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readAnimationGraphCount( &AnimationGraphPanel::countConditionLinks, outValue );
        }

        [[nodiscard]] bool readAnimationGraphLayerCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readAnimationGraphCount( &AnimationGraphPanel::getLayerCount, outValue );
        }

        [[nodiscard]] bool readAnimationGraphBoneMaskCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readAnimationGraphCount( &AnimationGraphPanel::getBoneMaskCount, outValue );
        }

        /** @brief `-gv_editorProbeObject` 오브젝트의 로컬 위치 X 입니다. 시퀀서 키 트랙 미리보기가 옮긴 값입니다. */
        [[nodiscard]] bool readObjectPositionX( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GameObjectManager* pManager = editor::getActiveObjectManager();
            const GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( gv_editorProbeObject.c_str() ) ) : nullptr;
            const SceneComponent*    pScene   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr )
                return false;
            outValue = static_cast<float64>( pScene->getLocalPosition()._x );
            return true;
        }

        /** @brief Sequencer 패널이 선택한 키 트랙의 키 프레임 수입니다. 패널이 닫혔거나 선택한 트랙이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readSequencerKeyFrameCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getProbeContext( __func__ );
            if ( pContext == nullptr )
                return false;
            const SequencerPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<SequencerPanel>();
            uint32                count{ 0 };
            const bool            bKeyFrameCountUnavailable = pPanel == nullptr || pPanel->isOpen() == false || pPanel->findSelectedTrackKeyFrameCount( count ) == false;
            if ( bKeyFrameCountUnavailable )
                return false;
            outValue = static_cast<float64>( count );
            return true;
        }
    } // namespace

    SW_AUTOMATION_PROBE( editorAnimationGraphLinkCount, "Editor.AnimationGraphLinkCount", "Transitions in the open animation graph document",
                         &readAnimationGraphLinkCount );
    SW_AUTOMATION_PROBE( editorAnimationGraphConditionCount, "Editor.AnimationGraphConditionCount", "Transitions with a condition in the open animation graph document",
                         &readAnimationGraphConditionCount );
    SW_AUTOMATION_PROBE( editorAnimationGraphLayerCount, "Editor.AnimationGraphLayerCount", "Layers in the open animation graph document",
                         &readAnimationGraphLayerCount );
    SW_AUTOMATION_PROBE( editorAnimationGraphBoneMaskCount, "Editor.AnimationGraphBoneMaskCount", "Bone masks in the open animation graph document",
                         &readAnimationGraphBoneMaskCount );
    SW_AUTOMATION_PROBE( editorObjectPositionX, "Editor.ObjectPositionX", "Local position X of the object named by gv_editorProbeObject",
                         &readObjectPositionX );
    SW_AUTOMATION_PROBE( editorSequencerKeyFrameCount, "Editor.SequencerKeyFrameCount", "Key frames of the key track selected in the Sequencer panel",
                         &readSequencerKeyFrameCount );
} // namespace sw::editor
