#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/PairedAnimationPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/YawUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourcePaths.h"
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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "PairedAnimationPanel" );
    SW_EDITOR_PANEL( PairedAnimationPanel, PairedAnimationPanel::kPanelID, EditorPanelCategory::Custom, 9030 );

    PairedAnimationPanel::PairedAnimationPanel()
        : EditorPreviewPanel{ EditorAssetType::Data, false }
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
            EditorWidgets::drawEmptyHint( "Open a .pairedanim.xml to scrub its roles on one clock, move alignment offsets and edit shared notifies." );
            return;
        }
        ImGui::TextDisabled( "%s%s", getLoadedAssetPath().c_str(), _model.isDirty() ? "  (modified)" : "" );
        drawHistoryRow();
        drawTimeline();
        if ( ImGui::CollapsingHeader( "Roles", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawRoleRows();
        if ( ImGui::CollapsingHeader( "Shared notifies", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawNotifyRows();

        syncPreviewRoles();
        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = PairedAnimationPanelInternal::kMinViewportHeight * EditorThemeUtil::getDpiScale();
        const float2  viewSize{ available._x, available._y > minHeight ? available._y : minHeight };
        drawPreviewViewport( viewSize, _gizmo.hasMouse() );
        _gizmo.draw( *this );
        EditorSelfTestMarks::note( "pairedanim.previewScene" );
        EditorWidgets::drawPanelStatus( _editError.empty() ? _status.c_str() : _editError.c_str() );
    }

    bool PairedAnimationPanel::saveDocument()
    {
        const string writePath = ResourcePaths::getWritePath( string_view( getLoadedAssetPath() ) );
        string       error;
        if ( writePath.empty() || _model.saveToFile( writePath, error ) == false )
        {
            _status = writePath.empty() ? string( "No write path for this asset" ) : error;
            return false;
        }
        SW_LOG_INFO( "Paired animation saved to %#", writePath.c_str() );
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
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();

        const string readPath = ResourcePaths::findLooseFile( string_view( path ) ).getDiskPath();
        if ( readPath.empty() || _model.loadFromFile( readPath ) == false )
        {
            _status = "The paired animation could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        // 역할 수가 바뀌었을 수 있다 — 미리보기 오브젝트를 새로 놓는다(프리팹 경로는 같은 인덱스면 남긴다).
        GameObjectManager* pManager = findPreviewManager();
        for ( RoleSlot& slot : _listSlot )
        {
            GameObject* pObject = pManager != nullptr ? pManager->resolveGameObject( slot._object ) : nullptr;
            if ( pObject != nullptr )
                pManager->destroyObject( pObject );
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
        getPreviewViewport().frameSphere( float3{ 0.0f, 1.0f, 0.5f }, PairedAnimationPanelInternal::kFrameRadius );
        for ( RoleSlot& slot : _listSlot )
        {
            slot._object = GameObjectHandle{};
            slot._spawnedPath.clear();
        }
    }

    void PairedAnimationPanel::drawHistoryRow()
    {
        ImGui::BeginDisabled( _model.canUndo() == false );
        if ( ImGui::Button( "Undo" ) )
            reportEdit( _model.undo() );
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "pairedanim.undo" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Redo" ) );
        ImGui::BeginDisabled( _model.canRedo() == false );
        if ( ImGui::Button( "Redo" ) )
            reportEdit( _model.redo() );
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "pairedanim.redo" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Check" ) );
        if ( ImGui::Button( "Check" ) )
            _status = _model.isValid() ? "The paired animation follows every rule" : "The paired animation breaks a rule - see the log";
        EditorSelfTestMarks::note( "pairedanim.check" );
        EditorWidgets::drawTooltip( "Read the edited content with the runtime loader to check every rule" );
    }

    void PairedAnimationPanel::drawTimeline()
    {
        float32 duration = _model.getContent()._duration;
        EditorChrome::setNextItemWidthColumns( 16.0f );
        if ( ImGui::DragFloat( "Duration (s)", &duration, 0.01f, 0.01f, 60.0f, "%.3f" ) )
            reportEdit( _model.setDuration( duration, true, _editError ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            _model.endContinuousEdit();
        EditorSelfTestMarks::note( "pairedanim.duration" );

        float32 scrub = _model.getScrubTime();
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::SliderFloat( "##Scrub", &scrub, 0.0f, _model.getContent()._duration, "t = %.3f s" ) )
            _model.setScrubTime( scrub );
        EditorSelfTestMarks::note( "pairedanim.scrub" );
        EditorWidgets::drawTooltip( "Shared timeline time - every role follows the anchor clock" );

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
                EditorThemeUtil::pushTextColor( EditorThemeUtil::getWarningColor() );
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
            ImGui::Text( "%s  clip %s  %s %.3f s%s", role._bAnchor ? "anchor" : ( role._bRequired ? "required" : "optional" ), role._clip.c_str(), pStage, static_cast<float64>( preview._clipSeconds ),
                         _listSlot[roleIndex]._clipSeconds > 0.0f ? "" : "  (clip length unknown - pick a prefab)" );

            RoleSlot& slot = _listSlot[roleIndex];
            ImGui::SetNextItemWidth( -1.0f );
            ImGui::InputTextWithHint( "##Prefab", "Preview prefab (resource id, not saved)", slot._prefabPath.data(), slot._prefabPath.max_size() );
            EditorSelfTestMarks::note( ( string( "pairedanim.role." ) + role._name.c_str() + ".prefab" ).c_str() );

            if ( role._bAnchor == false )
            {
                float32 start = role._startSeconds;
                EditorChrome::setNextItemWidthColumns( 16.0f );
                if ( ImGui::DragFloat( "Start (s)", &start, 0.01f, 0.0f, _model.getContent()._duration, "%.3f" ) )
                    reportEdit( _model.setRoleStart( roleIndex, start, true, _editError ) );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    _model.endContinuousEdit();
                ImGui::SameLine();
            }
            float32 arrBlend[2] = { role._enterBlendSeconds, role._exitBlendSeconds };
            EditorChrome::setNextItemWidthColumns( 27.0f );
            if ( ImGui::DragFloat2( "Enter / exit blend (s)", arrBlend, 0.01f, 0.0f, 5.0f, "%.3f" ) )
                reportEdit( _model.setRoleBlend( roleIndex, arrBlend[0], arrBlend[1], true, _editError ) );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                _model.endContinuousEdit();

            const bool bAligned = role._bAnchor == false && role._align != PairedAnimationAlign::None;
            if ( bAligned )
            {
                float3  offset     = role._offset;
                float32 yawDegrees = role._yawRadians * MathUtil::kRadianToDegree;
                EditorChrome::setNextItemWidthColumns( 32.0f );
                const bool bOffsetChanged = ImGui::DragFloat3( "Offset (m)", offset.data(), 0.01f, -10.0f, 10.0f, "%.3f" );
                const bool bOffsetDone    = ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                EditorChrome::setNextItemWidthColumns( 13.0f );
                const bool bYawChanged = ImGui::DragFloat( "Yaw (deg)", &yawDegrees, 0.5f, -180.0f, 180.0f, "%.1f" );
                const bool bYawDone    = ImGui::IsItemDeactivatedAfterEdit();
                if ( bOffsetChanged || bYawChanged )
                    reportEdit( _model.setRoleOffset( roleIndex, offset, yawDegrees, true, _editError ) );
                if ( bOffsetDone || bYawDone )
                    _model.endContinuousEdit();
                EditorSelfTestMarks::note( ( string( "pairedanim.role." ) + role._name.c_str() + ".offset" ).c_str() );
                EditorWidgets::drawTooltip( "Anchor-relative root position and yaw at alignment time; select the role object in the viewport to move it with the gizmo" );
            }
            if ( role._bAnchor == false && role._align == PairedAnimationAlign::Warp )
            {
                float32 warpEnd = role._warpEndSeconds;
                EditorChrome::setNextItemWidthColumns( 16.0f );
                if ( ImGui::DragFloat( "Warp end (s)", &warpEnd, 0.01f, 0.0f, 10.0f, "%.3f" ) )
                    reportEdit( _model.setRoleWarpEnd( roleIndex, warpEnd, true, _editError ) );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    _model.endContinuousEdit();
            }
            ImGui::PopID();
        }
    }

    void PairedAnimationPanel::drawNotifyRows()
    {
        const PairedAnimationContentDef& content     = _model.getContent();
        uint32                           removeIndex = invalid_index::kUint32;
        for ( uint32 notifyIndex = 0; notifyIndex < static_cast<uint32>( content._listNotify.size() ); ++notifyIndex )
        {
            const PairedAnimationNotifyDef& notify = content._listNotify[notifyIndex];
            ImGui::PushID( static_cast<int32>( notifyIndex ) );
            ImGui::Text( "%s", notify._name.c_str() );
            ImGui::SameLine();
            float32 time = notify._time;
            EditorChrome::setNextItemWidthColumns( 27.0f );
            if ( ImGui::SliderFloat( "##Time", &time, 0.0f, content._duration, "%.3f s" ) )
                reportEdit( _model.setNotifyTime( notifyIndex, time, true, _editError ) );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                _model.endContinuousEdit();
            EditorSelfTestMarks::note( ( string( "pairedanim.notify." ) + notify._name.c_str() + ".time" ).c_str() );
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 19.0f );
            if ( ImGui::BeginCombo( "##Role", notify._role.c_str() ) )
            {
                for ( const PairedAnimationRoleDef& role : content._listRole )
                {
                    if ( ImGui::Selectable( role._name.c_str(), role._name == notify._role ) )
                        reportEdit( _model.setNotifyRole( notifyIndex, role._name, _editError ) );
                }
                ImGui::EndCombo();
            }
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove" ) );
            if ( ImGui::SmallButton( "Remove" ) )
                removeIndex = notifyIndex;
            ImGui::PopID();
        }
        if ( removeIndex != invalid_index::kUint32 )
            reportEdit( _model.removeNotify( removeIndex, _editError ) );

        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputText( "##NewNotify", _newNotifyName.data(), _newNotifyName.max_size() );
        ImGui::SameLine();
        const bool bCanAdd = content._listRole.empty() == false;
        ImGui::BeginDisabled( bCanAdd == false );
        if ( ImGui::Button( "Add notify at t" ) )
        {
            // 새 알림은 첫 역할로 받는다 — 역할은 콤보로 바꾼다.
            reportEdit( _model.addNotify( hashed_string( _newNotifyName.c_str() ), content._listRole[0]._name, _model.getScrubTime(), _editError ) );
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "pairedanim.addNotify" );
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
            const float32                 yaw      = YawUtil::fromWorld( pRoot->getWorldMatrix() );
            const bool                    bMoved   = ( current - slot._placedPosition ).getLength() > PairedAnimationPanelInternal::kMovedToleranceMeters ||
                                MathUtil::abs( MathUtil::wrapAngle( yaw - slot._placedYaw ) ) > PairedAnimationPanelInternal::kMovedToleranceRadians;
            if ( bAligned && bMoved )
                reportEdit( _model.setRoleOffsetFromWorld( roleIndex, anchorPosition, anchorYaw, current, yaw, true, _editError ) );
            else if ( bMoved && _gizmo.hasMouse() == false )
                _model.endContinuousEdit();

            float3  target{};
            float32 targetYaw = 0.0f;
            _model.computeRoleTarget( roleIndex, anchorPosition, anchorYaw, target, targetYaw );
            pRoot->setWorldTransform( PairedAnimationPanelInternal::makeWorld( target, targetYaw ) );
            slot._placedPosition = pRoot->getWorldPosition();
            slot._placedYaw      = YawUtil::fromWorld( pRoot->getWorldMatrix() );

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
            manager.destroyObject( pExisting );
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

    void PairedAnimationPanel::reportEdit( bool bSucceeded )
    {
        if ( bSucceeded )
        {
            _editError.clear();
            if ( _model.isDirty() )
                markDocumentDirty();
            else
                clearDocumentDirty();
        }
    }

    GameObjectManager* PairedAnimationPanel::findPreviewManager() const
    {
        Scene* pScene = getPreviewScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }
} // namespace sw::editor
