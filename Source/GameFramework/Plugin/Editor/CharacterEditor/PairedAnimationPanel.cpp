#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/PairedAnimationPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PairedAnimationPanelInternal
        {
            /// @brief 뷰포트의 최소 높이(96 DPI 기준 픽셀)입니다.
            static constexpr float32 kMinViewportHeight = 220.0f;
            /// @brief 화면에 담을 반지름(미터)입니다 — 두 사람이 1 ~ 2 m 떨어져 선다.
            static constexpr float32 kFrameRadius = 2.5f;
            /// @brief 기즈모가 옮겼다고 보는 자리 차이(m)와 요 차이(라디안)입니다. 창이 놓은 값을 다시 읽는 반올림은 이보다 작다.
            static constexpr float32 kMovedToleranceMeters  = 1.0e-4f;
            static constexpr float32 kMovedToleranceRadians = 1.0e-4f;

            /** @brief 자리 · 요(라디안)의 월드 변환입니다. */
            static float4x4 makeWorld( const float3& position, float32 yaw )
            {
                return float4x4::makeTrs( position, quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, yaw ), float3{ 1.0f, 1.0f, 1.0f } );
            }

            /** @brief 열린 창입니다. 닫혔으면 nullptr 입니다(감시 값이 실패한다). */
            [[nodiscard]] static const PairedAnimationPanel* findOpenPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                const PairedAnimationPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<PairedAnimationPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            /** @brief 감시 값 `Editor.PairedAnimationRoleCount` — 연 파일의 역할 수입니다(창이 닫혔거나 파일을 읽지 못했으면 실패). */
            [[nodiscard]] static bool readRoleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const PairedAnimationPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr || pPanel->getModel().getContent()._listRole.empty() ) // 읽은 파일은 역할이 하나 이상이다(PairedAnimationDef 검사)
                    return false;
                outValue = static_cast<float64>( pPanel->getModel().getContent()._listRole.size() );
                return true;
            }

            /** @brief 감시 값 `Editor.PairedAnimationSceneFrames` — 창이 렌더러에 그려 달라고 낸 미리보기 씬 프레임 수입니다. */
            [[nodiscard]] static bool readPreviewSceneFrames( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const PairedAnimationPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPreviewViewport().getSubmittedFrameCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( PairedAnimationPanel, PairedAnimationPanel::kPanelID, EditorPanelCategory::Custom, 9030 );
    SW_AUTOMATION_WATCH( editorPairedAnimationRoleCount, "Editor.PairedAnimationRoleCount", "Roles in the paired animation file the Paired Animation panel has open (fails while the panel is closed or has no file)",
                         &PairedAnimationPanelInternal::readRoleCount );
    SW_AUTOMATION_WATCH( editorPairedAnimationSceneFrames, "Editor.PairedAnimationSceneFrames", "Frames the Paired Animation panel asked the renderer to draw its preview scene",
                         &PairedAnimationPanelInternal::readPreviewSceneFrames );

    PairedAnimationPanel::PairedAnimationPanel()
        : EditorPreviewPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, false }
        , _model{}
        , _gizmo{}
        , _listSlot{}
        , _listFiredNotify{}
        , _status{}
        , _editError{}
        , _newNotifyName{ "Notify" }
        , _previousScrubTime{ 0.0f }
    {
    }

    void PairedAnimationPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "pairedanim" );

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "OpenAPairedanimXmlToScrubItsRoles" ) );
            return;
        }
        ImGui::TextDisabled( "%s%s", getLoadedAssetPath().c_str(), _model.isDirty() ? "  (modified)" : "" );
        drawHistoryRow();
        drawTimeline();
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdPlugin", "Roles" ), ImGuiTreeNodeFlags_DefaultOpen ) )
            drawRoleRows();
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdPlugin", "SharedNotifies" ), ImGuiTreeNodeFlags_DefaultOpen ) )
            drawNotifyRows();

        syncPreviewRoles();
        const float2 viewSize = computeViewportFillSize( PairedAnimationPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale() );
        drawPreviewViewport( viewSize, _gizmo.hasMouse() );
        _gizmo.draw( *this );
        EditorSelfTestMarks::record( "pairedanim.previewScene" );
        EditorWidgets::drawPanelStatus( _editError.empty() ? _status.c_str() : _editError.c_str() );
    }

    bool PairedAnimationPanel::saveDocument()
    {
        const auto writePath = getDocumentDiskPath(); // 읽은 그 자리
        if ( writePath.has_value() == false )
        {
            _status = "No write path for this asset";
            return false;
        }
        if ( const expected<void> saved = _model.saveDocument( *writePath ); saved.has_value() == false )
        {
            _status = EditorWidgets::makeStatusText( saved.error() );
            return false;
        }
        SW_LOG_INFO_ID( "PairedAnimationPanel.PairedAnimationSaved", "Paired animation saved to %#", writePath->c_str() );
        _status = "Saved";
        acknowledgePreviewEdits();
        return true;
    }

    bool PairedAnimationPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, PairedAnimationDef::kExtension );
    }

    ToolAssetLoadResult PairedAnimationPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;

        const auto readPath = getDocumentDiskPath();
        if ( readPath.has_value() == false )
        {
            _status = "The paired animation '" + path + "' has no file on disk to read";
            return ToolAssetLoadResult::Malformed;
        }
        if ( _model.loadFromFile( readPath->str() ) == false )
        {
            _status = "The paired animation '" + readPath->str() + "' could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        // 역할 수가 바뀌었을 수 있다 — 미리보기 오브젝트를 새로 놓는다(프리팹 경로는 같은 인덱스면 남긴다).
        GameObjectManager* pManager = findPreviewManager();
        for ( RoleSlot& slot : _listSlot )
        {
            GameObject* pObject = pManager != nullptr ? pManager->resolveGameObject( slot._object ) : nullptr;
            if ( pObject != nullptr )
                pManager->destroyGameObject( pObject );
            slot._object = GameObjectHandle{};
            slot._spawnedPath.clear();
        }
        _listSlot.resize( _model.getContent()._listRole.size() );
        _listFiredNotify.clear();
        _previousScrubTime = 0.0f;
        resetPreviewEdits();
        _status = "Loaded";
        _editError.clear();
        return ToolAssetLoadResult::Loaded;
    }

    void PairedAnimationPanel::populatePreviewScene( Scene& scene )
    {
        (void)scene; // 역할 오브젝트는 프리팹을 고른 뒤 놓는다(syncPreviewRoles)
        for ( RoleSlot& slot : _listSlot )
        {
            slot._object = GameObjectHandle{};
            slot._spawnedPath.clear();
        }
    }

    void PairedAnimationPanel::drawHistoryRow()
    {
        ImGui::BeginDisabled( _model.canUndo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Undo" ) ) && _model.undo() )
            reportEdit( expected<void>{} );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "pairedanim.undo" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Redo" ) ) );
        ImGui::BeginDisabled( _model.canRedo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Redo" ) ) && _model.redo() )
            reportEdit( expected<void>{} );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "pairedanim.redo" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Check" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Check" ) ) )
            _status = _model.isValid() ? "The paired animation follows every rule" : "The paired animation breaks a rule - see the log";
        EditorSelfTestMarks::record( "pairedanim.check" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "ReadTheEditedContentWithTheRuntime" ) );
    }

    void PairedAnimationPanel::drawTimeline()
    {
        float32 duration = _model.getContent()._duration;
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "DurationS" ) );
        EditorChrome::setNextItemWidthColumns( 16.0f );
        if ( ImGui::DragFloat( "##Durations", &duration, 0.01f, 0.01f, 60.0f, "%.3f" ) )
            reportEdit( _model.setDuration( duration, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            _model.endContinuousEdit();
        EditorSelfTestMarks::record( "pairedanim.duration" );

        // 공유 시간축의 문지르기는 뷰포트 위 공통 재생 막대가 맡는다(readPreviewTimeline · seekPreviewTimeline).
        // 앞으로 지났으면 울린 알림을 보인다(뒤로 가면 지운다 — 세션은 되감지 않는다).
        const float32 now = _model.getScrubTime();
        if ( now > _previousScrubTime )
            _model.collectNotifiesBetween( _previousScrubTime, now, _listFiredNotify );
        else if ( now < _previousScrubTime )
            _listFiredNotify.clear();
        _previousScrubTime = now;
        for ( const uint32 notifyIndex : _listFiredNotify )
        {
            if ( notifyIndex < _model.getContent()._listNotify.size() )
            {
                const PairedAnimationNotifyDef& notify = _model.getContent()._listNotify[notifyIndex];
                ImGui::SameLine();
                EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Signal", "Warning" ) );
                ImGui::Text( "[%s -> %s]", notify._name.c_str(), notify._role.c_str() );
                EditorThemeUtil::popTextColor();
            }
        }
        ImGui::NewLine();
    }

    void PairedAnimationPanel::drawRoleRows()
    {
        const vector<PairedAnimationRoleDef>& listRole = _model.getContent()._listRole;
        _listSlot.resize( listRole.size() );
        for ( uint32 roleIndex = 0; roleIndex < static_cast<uint32>( listRole.size() ); ++roleIndex )
        {
            const PairedAnimationRoleDef&    role    = listRole[roleIndex];
            const PairedAnimationRolePreview preview = _model.computeRolePreview( roleIndex, _listSlot[roleIndex]._clipSeconds );
            const utf8*                      pStage  = preview._stage == PairedAnimationPreviewStage::Waiting ? "waiting"
                                                     : preview._stage == PairedAnimationPreviewStage::Playing ? "playing"
                                                                                                              : "exited";
            ImGui::PushID( static_cast<int32>( roleIndex ) );
            ImGui::SeparatorText( role._name.c_str() );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "ClipS", TextArgumentList().addText( "role", role._bAnchor ? "anchor" : ( role._bRequired ? "required" : "optional" ) ).addText( "clip", role._clip.c_str() ).addText( "stage", pStage ).addDecimal( "clipSeconds", static_cast<float64>( preview._clipSeconds ), 3 ).addText( "clipLength", _listSlot[roleIndex]._clipSeconds > 0.0f ? "known" : "unknown" ) ).c_str() );

            RoleSlot& slot = _listSlot[roleIndex];
            ImGui::SetNextItemWidth( -1.0f );
            (void)EditorWidgets::drawAssetPathField( "##Prefab", slot._prefabPath.data(), slot._prefabPath.max_size(), "Preview prefab (resource id, not saved)", ".prefab.xml,.prefab.json",
                                                     ( string( "pairedanim.role." ) + role._name.c_str() + ".prefab" ).c_str() );

            if ( role._bAnchor == false )
            {
                float32 start = role._startSeconds;
                EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "StartS" ) );
                EditorChrome::setNextItemWidthColumns( 16.0f );
                if ( ImGui::DragFloat( "##Starts", &start, 0.01f, 0.0f, _model.getContent()._duration, "%.3f" ) )
                    reportEdit( _model.setRoleStart( roleIndex, start, true ) );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    _model.endContinuousEdit();
                ImGui::SameLine();
            }
            float32 arrBlend[2] = { role._enterBlendSeconds, role._exitBlendSeconds };
            EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "EnterExitBlendS" ) );
            EditorChrome::setNextItemWidthColumns( 27.0f );
            if ( ImGui::DragFloat2( "##Enterexitblends", arrBlend, 0.01f, 0.0f, 5.0f, "%.3f" ) )
                reportEdit( _model.setRoleBlend( roleIndex, arrBlend[0], arrBlend[1], true ) );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                _model.endContinuousEdit();

            const bool bAligned = role._bAnchor == false && role._align != PairedAnimationAlign::None;
            if ( bAligned )
            {
                float3  offset     = role._offset;
                float32 yawDegrees = role._yawRadians * MathUtil::kRadianToDegree;
                EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "OffsetM" ) );
                EditorChrome::setNextItemWidthColumns( 32.0f );
                const bool bOffsetChanged = ImGui::DragFloat3( "##Offsetm", offset.data(), 0.01f, -10.0f, 10.0f, "%.3f" );
                const bool bOffsetDone    = ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "YawDeg" ) );
                EditorChrome::setNextItemWidthColumns( 13.0f );
                const bool bYawChanged = ImGui::DragFloat( "##Yawdeg", &yawDegrees, 0.5f, -180.0f, 180.0f, "%.1f" );
                const bool bYawDone    = ImGui::IsItemDeactivatedAfterEdit();
                if ( bOffsetChanged || bYawChanged )
                    reportEdit( _model.setRoleOffset( roleIndex, offset, yawDegrees, true ) );
                if ( bOffsetDone || bYawDone )
                    _model.endContinuousEdit();
                EditorSelfTestMarks::record( ( string( "pairedanim.role." ) + role._name.c_str() + ".offset" ).c_str() );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "AnchorRelativeRootPositionAndYawAt" ) );
            }
            if ( role._bAnchor == false && role._align == PairedAnimationAlign::Warp )
            {
                float32 warpEnd = role._warpEndSeconds;
                EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "WarpEndS" ) );
                EditorChrome::setNextItemWidthColumns( 16.0f );
                if ( ImGui::DragFloat( "##Warpends", &warpEnd, 0.01f, 0.0f, 10.0f, "%.3f" ) )
                    reportEdit( _model.setRoleWarpEnd( roleIndex, warpEnd, true ) );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    _model.endContinuousEdit();
            }
            ImGui::PopID();
        }
    }

    void PairedAnimationPanel::drawNotifyRows()
    {
        const PairedAnimationContentDef& content     = _model.getContent();
        uint32                           removeIndex = constant::invalid_index::kUint32;
        for ( uint32 notifyIndex = 0; notifyIndex < static_cast<uint32>( content._listNotify.size() ); ++notifyIndex )
        {
            const PairedAnimationNotifyDef& notify = content._listNotify[notifyIndex];
            ImGui::PushID( static_cast<int32>( notifyIndex ) );
            ImGui::Text( "%s", notify._name.c_str() );
            ImGui::SameLine();
            float32 time = notify._time;
            EditorChrome::setNextItemWidthColumns( 27.0f );
            if ( ImGui::SliderFloat( "##Time", &time, 0.0f, content._duration, "%.3f s" ) )
                reportEdit( _model.setNotifyTime( notifyIndex, time, true ) );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                _model.endContinuousEdit();
            EditorSelfTestMarks::record( ( string( "pairedanim.notify." ) + notify._name.c_str() + ".time" ).c_str() );
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 19.0f );
            if ( ImGui::BeginCombo( "##Role", notify._role.c_str() ) )
            {
                for ( const PairedAnimationRoleDef& role : content._listRole )
                {
                    if ( ImGui::Selectable( role._name.c_str(), role._name == notify._role ) )
                        reportEdit( _model.setNotifyRole( notifyIndex, role._name ) );
                }
                ImGui::EndCombo();
            }
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) )
                removeIndex = notifyIndex;
            ImGui::PopID();
        }
        if ( removeIndex != constant::invalid_index::kUint32 )
            reportEdit( _model.removeNotify( removeIndex ) );

        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputText( "##NewNotify", _newNotifyName.data(), _newNotifyName.max_size() );
        ImGui::SameLine();
        const bool bCanAdd = content._listRole.empty() == false;
        ImGui::BeginDisabled( bCanAdd == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "AddNotifyAtT" ) ) )
        {
            // 새 알림은 첫 역할로 받는다 — 역할은 콤보로 바꾼다.
            reportEdit( _model.addNotify( hashed_string( _newNotifyName.c_str() ), content._listRole[0]._name, _model.getScrubTime() ) );
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "pairedanim.addNotify" );
    }

    void PairedAnimationPanel::syncPreviewRoles()
    {
        GameObjectManager* pManager = findPreviewManager();
        if ( pManager == nullptr )
            return;
        const vector<PairedAnimationRoleDef>& listRole = _model.getContent()._listRole;
        _listSlot.resize( listRole.size() );
        const float3  anchorPosition{};
        const float32 anchorYaw = 0.0f;
        for ( uint32 roleIndex = 0; roleIndex < static_cast<uint32>( listRole.size() ); ++roleIndex )
        {
            ensureRoleObject( *pManager, roleIndex );
            RoleSlot&       slot    = _listSlot[roleIndex];
            GameObject*     pObject = pManager->resolveGameObject( slot._object );
            SceneComponent* pRoot   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pRoot == nullptr )
                continue;

            // 기즈모가 정렬 역할을 옮겼으면 그 자리를 오프셋으로 넣는다.
            const PairedAnimationRoleDef& role     = listRole[roleIndex];
            const bool                    bAligned = role._bAnchor == false && role._align != PairedAnimationAlign::None;
            const float3                  current  = pRoot->getWorldPosition();
            const float32                 yaw      = OrientationUtil::computeYawFromWorld( pRoot->getWorldMatrix() );
            const bool                    bMoved   = ( current - slot._placedPosition ).getLength() > PairedAnimationPanelInternal::kMovedToleranceMeters ||
                                MathUtil::abs( MathUtil::wrapAngle( yaw - slot._placedYaw ) ) > PairedAnimationPanelInternal::kMovedToleranceRadians;
            if ( bAligned && bMoved )
                reportEdit( _model.setRoleOffsetFromWorld( roleIndex, anchorPosition, anchorYaw, current, yaw, true ) );
            else if ( bMoved && _gizmo.hasMouse() == false )
                _model.endContinuousEdit();

            float3  target{};
            float32 targetYaw = 0.0f;
            _model.computeRoleTarget( roleIndex, anchorPosition, anchorYaw, target, targetYaw );
            pRoot->setWorldTransform( PairedAnimationPanelInternal::makeWorld( target, targetYaw ) );
            slot._placedPosition = pRoot->getWorldPosition();
            slot._placedYaw      = OrientationUtil::computeYawFromWorld( pRoot->getWorldMatrix() );

            SkeletalAnimatorComponent* pAnimator = pObject->getComponent<SkeletalAnimatorComponent>();
            if ( pAnimator != nullptr )
                poseRoleObject( *pAnimator, roleIndex );
        }
    }

    void PairedAnimationPanel::ensureRoleObject( GameObjectManager& manager, uint32 roleIndex )
    {
        RoleSlot&    slot       = _listSlot[roleIndex];
        const string prefabPath = string( slot._prefabPath.c_str() );
        GameObject*  pExisting  = manager.resolveGameObject( slot._object );
        if ( pExisting != nullptr && slot._spawnedPath == prefabPath )
            return;
        if ( pExisting != nullptr )
            manager.destroyGameObject( pExisting );
        slot._object = GameObjectHandle{};
        slot._spawnedPath.clear();
        slot._clipSeconds = 0.0f;
        if ( prefabPath.empty() )
            return;

        AssetManager* pAssets  = manager.findAssetManager();
        GameObject*   pSpawned = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, prefabPath ) : nullptr;
        slot._spawnedPath      = prefabPath; // 실패해도 같은 경로로 매 프레임 다시 놓지 않는다(경로를 고치면 다시 한다)
        if ( pSpawned == nullptr )
        {
            _status = string( "Prefab '" ) + prefabPath + "' could not be spawned - see the log";
            return;
        }
        slot._object                            = pSpawned->getHandle();
        slot._placedPosition                    = float3{};
        slot._placedYaw                         = 0.0f;
        SkeletalAnimatorComponent*    pAnimator = pSpawned->getComponent<SkeletalAnimatorComponent>();
        const PairedAnimationRoleDef& role      = _model.getContent()._listRole[roleIndex];
        if ( pAnimator == nullptr )
        {
            _status = string( "Prefab '" ) + prefabPath + "' has no skeletal animator on its root - the role cannot play";
            return;
        }
        pAnimator->setPlayRate( 0.0f ); // 창이 시각을 정한다
        const AnimationClip* pClip = pAnimator->preloadClip( role._clip ) ? pAnimator->findClip( role._clip ) : nullptr;
        if ( pClip == nullptr )
        {
            _status = string( "Role clip '" ) + role._clip.c_str() + "' is not in the prefab's clip folder";
            return;
        }
        slot._clipSeconds = pClip->getDuration();
    }

    void PairedAnimationPanel::poseRoleObject( SkeletalAnimatorComponent& animator, uint32 roleIndex )
    {
        const PairedAnimationRoleDef&    role    = _model.getContent()._listRole[roleIndex];
        const PairedAnimationRolePreview preview = _model.computeRolePreview( roleIndex, _listSlot[roleIndex]._clipSeconds );
        if ( preview._stage == PairedAnimationPreviewStage::Exited )
        {
            if ( animator.getCurrentStateName() != role._exitState )
                (void)animator.play( role._exitState, true, 0.0f ); // 못 찾은 종료 상태는 `play` 가 오류로 남긴다
            return;
        }
        if ( _listSlot[roleIndex]._clipSeconds <= 0.0f )
            return; // 클립을 읽지 못했다 — 상태 줄이 알렸다
        if ( animator.getCurrentStateName() != role._clip )
            (void)animator.play( role._clip, false, 0.0f ); // 클립은 ensureRoleObject 에서 읽었다
        // 기다리는 역할은 클립 첫 자세를 보인다(세션에서는 아직 다른 것을 재생하지만, 저작에는 진입 자세가 쓸모 있다).
        const float32 clipTime = preview._stage == PairedAnimationPreviewStage::Playing ? preview._clipSeconds : 0.0f;
        (void)animator.seekCurrentTime( clipTime ); // 범위 밖은 모델이 자른다 — 실패는 seek 가 오류로 남긴다
    }

    void PairedAnimationPanel::reportEdit( const expected<void>& result )
    {
        if ( result.has_value() == false )
        {
            _editError = EditorWidgets::makeStatusText( result.error() );
            return;
        }
        _editError.clear();
        if ( _model.isDirty() )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    GameObjectManager* PairedAnimationPanel::findPreviewManager() const
    {
        Scene* pScene = getPreviewScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    bool PairedAnimationPanel::findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        outCenter = float3{ 0.0f, 1.0f, 0.5f };
        outRadius = PairedAnimationPanelInternal::kFrameRadius;
        return true;
    }

    bool PairedAnimationPanel::readPreviewTimeline( float32& outTime, float32& outDuration ) const
    {
        if ( _model.getContent()._duration <= 0.0f )
            return false;
        outTime     = _model.getScrubTime();
        outDuration = _model.getContent()._duration;
        return true;
    }

    void PairedAnimationPanel::seekPreviewTimeline( float32 seconds )
    {
        _model.setScrubTime( seconds );
    }
} // namespace sw::editor
