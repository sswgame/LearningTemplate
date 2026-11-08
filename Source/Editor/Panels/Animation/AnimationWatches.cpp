/**
 * @file AnimationWatches.cpp
 * @brief 애니메이션 그래프 · 시퀀서 패널의 자동화 감시 값입니다 — `Editor.AnimationGraph*Count` · `Editor.SequencerKeyFrameCount` · `Editor.ObjectPositionX`(시퀀서 키 트랙 미리보기가 옮긴 값).
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Animation/AnimationGraphPanel.h"
#include "Editor/Panels/Animation/SequencerPanel.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
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
            EditorContext* pContext = getWatchContext( __func__ );
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

        [[nodiscard]] bool readAnimationGraphMaskEntryCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readAnimationGraphCount( &AnimationGraphPanel::countBoneMaskEntries, outValue );
        }

        [[nodiscard]] bool readAnimationGraphMaskBoneRowCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readAnimationGraphCount( &AnimationGraphPanel::getMaskBoneRowCount, outValue );
        }

        /** @brief `-gv_editorWatchObject` 오브젝트의 로컬 위치 X 입니다. 시퀀서 키 트랙 미리보기가 옮긴 값입니다. */
        [[nodiscard]] bool readObjectPositionX( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GameObjectManager* pManager = editor::getActiveObjectManager();
            const GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( gv_editorWatchObject.c_str() ) ) : nullptr;
            const SceneComponent*    pScene   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr )
                return false;
            outValue = static_cast<float64>( pScene->getLocalPosition()._x );
            return true;
        }

        /** @brief Sequencer 패널이 선택한 키 트랙의 키 프레임 수입니다. 패널이 닫혔거나 선택한 트랙이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readSequencerKeyFrameCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
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

        /** @brief 열린 Sequencer 패널입니다. 닫혔으면 nullptr 입니다. */
        [[nodiscard]] const SequencerPanel* findOpenSequencer( const utf8* pCaller )
        {
            EditorContext* pContext = getWatchContext( pCaller );
            if ( pContext == nullptr )
                return nullptr;
            const SequencerPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<SequencerPanel>();
            return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
        }

        /** @brief Sequencer 문서의 바인딩 수입니다. */
        [[nodiscard]] bool readSequencerBindingCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SequencerPanel* pPanel = findOpenSequencer( __func__ );
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getDocument()._asset._listBinding.size() );
            return true;
        }

        /** @brief Sequencer 의 바인딩 문맥(이 시퀀스를 트는 플레이어)에서 묶이지 않은 바인딩 수입니다. */
        [[nodiscard]] bool readSequencerUnboundCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SequencerPanel* pPanel = findOpenSequencer( __func__ );
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->countUnboundBindings() );
            return true;
        }

        /** @brief Sequencer 문서의 카메라 컷 수입니다(컷 차선). */
        [[nodiscard]] bool readSequencerCutCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SequencerPanel* pPanel = findOpenSequencer( __func__ );
            if ( pPanel == nullptr )
                return false;
            vector<int32> listCut;
            pPanel->getDocument().collectCutLane( listCut );
            outValue = static_cast<float64>( listCut.size() );
            return true;
        }

        /** @brief Sequencer 에서 고른 키 수입니다. */
        [[nodiscard]] bool readSequencerSelectedKeyCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SequencerPanel* pPanel = findOpenSequencer( __func__ );
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getDocument().getSelectedKeys().size() );
            return true;
        }

        /** @brief Sequencer 의 오류 배지가 붙은 트랙 수입니다. */
        [[nodiscard]] bool readSequencerTrackIssueCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SequencerPanel* pPanel = findOpenSequencer( __func__ );
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getDocument().countTrackIssues() );
            return true;
        }

        /** @brief Sequencer 에서 고른 키 트랙 번호입니다(없으면 -1). */
        [[nodiscard]] bool readSequencerSelectedTrack( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SequencerPanel* pPanel = findOpenSequencer( __func__ );
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getDocument()._selectedTrack );
            return true;
        }

        /** @brief Sequencer 의 키 띠 · 컷 차선이 보이는 프레임 폭입니다(확대하면 준다). */
        [[nodiscard]] bool readSequencerViewFrameSpan( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SequencerPanel* pPanel = findOpenSequencer( __func__ );
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getDocument().getViewFrameSpan() );
            return true;
        }

        /** @brief Sequencer 문서의 구간 항목 수입니다(컷 포함). */
        [[nodiscard]] bool readSequencerItemCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SequencerPanel* pPanel = findOpenSequencer( __func__ );
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getDocument()._asset._listItem.size() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorSequencerBindingCount, "Editor.SequencerBindingCount", "Bindings in the sequence open in the Sequencer panel", &readSequencerBindingCount );
    SW_AUTOMATION_WATCH( editorSequencerUnboundCount, "Editor.SequencerUnboundCount", "Bindings of the open sequence that the scene's Sequence Player does not bind to a live object",
                         &readSequencerUnboundCount );
    SW_AUTOMATION_WATCH( editorSequencerCutCount, "Editor.SequencerCutCount", "Camera cuts in the sequence open in the Sequencer panel", &readSequencerCutCount );
    SW_AUTOMATION_WATCH( editorSequencerSelectedKeyCount, "Editor.SequencerSelectedKeyCount", "Keys selected in the Sequencer panel's key strips", &readSequencerSelectedKeyCount );
    SW_AUTOMATION_WATCH( editorSequencerTrackIssueCount, "Editor.SequencerTrackIssueCount", "Key tracks with an error badge in the Sequencer panel (unknown binding, channel count, empty target path, keys outside the timeline)",
                         &readSequencerTrackIssueCount );
    SW_AUTOMATION_WATCH( editorSequencerSelectedTrack, "Editor.SequencerSelectedTrack", "Index of the key track selected in the Sequencer panel (-1: none)", &readSequencerSelectedTrack );
    SW_AUTOMATION_WATCH( editorSequencerViewFrameSpan, "Editor.SequencerViewFrameSpan", "Frames the Sequencer panel's key strips and cut lane show (the timeline length when fitted)",
                         &readSequencerViewFrameSpan );
    SW_AUTOMATION_WATCH( editorSequencerItemCount, "Editor.SequencerItemCount", "Span items (camera cuts included) in the sequence open in the Sequencer panel", &readSequencerItemCount );
    SW_AUTOMATION_WATCH( editorAnimationGraphLinkCount, "Editor.AnimationGraphLinkCount", "Transitions in the open animation graph document",
                         &readAnimationGraphLinkCount );
    SW_AUTOMATION_WATCH( editorAnimationGraphConditionCount, "Editor.AnimationGraphConditionCount", "Transitions with a condition in the open animation graph document",
                         &readAnimationGraphConditionCount );
    SW_AUTOMATION_WATCH( editorAnimationGraphLayerCount, "Editor.AnimationGraphLayerCount", "Layers in the open animation graph document",
                         &readAnimationGraphLayerCount );
    SW_AUTOMATION_WATCH( editorAnimationGraphBoneMaskCount, "Editor.AnimationGraphBoneMaskCount", "Bone masks in the open animation graph document",
                         &readAnimationGraphBoneMaskCount );
    SW_AUTOMATION_WATCH( editorAnimationGraphMaskEntryCount, "Editor.AnimationGraphMaskEntryCount", "Entries of every bone mask in the open animation graph document",
                         &readAnimationGraphMaskEntryCount );
    SW_AUTOMATION_WATCH( editorAnimationGraphMaskBoneRowCount, "Editor.AnimationGraphMaskBoneRowCount",
                         "Bones the Layers tab skeleton view laid out for the selected bone mask (0 without a skeleton)", &readAnimationGraphMaskBoneRowCount );
    SW_AUTOMATION_WATCH( editorObjectPositionX, "Editor.ObjectPositionX", "Local position X of the object named by gv_editorWatchObject",
                         &readObjectPositionX );
    SW_AUTOMATION_WATCH( editorSequencerKeyFrameCount, "Editor.SequencerKeyFrameCount", "Key frames of the key track selected in the Sequencer panel",
                         &readSequencerKeyFrameCount );
} // namespace sw::editor
