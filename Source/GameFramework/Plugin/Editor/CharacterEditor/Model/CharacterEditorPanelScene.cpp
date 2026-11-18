// 캐릭터 편집 창의 문서 · 미리보기 씬 동기화(몸 · 기즈모 대리 · 제약 리그) · 썸네일 · 감시 값입니다(ImGui 없음). 그리기는 `CharacterEditorPanel.cpp`(View)다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPreviewViewport.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/CharacterEditorPanel.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterBodyDynamicsComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPanelSceneInternal
        {
            static constexpr float32 kPreviewFrameRadius = 1.4f;    ///< 미리보기 카메라가 담는 반지름(m) — 사람 키 하나
            static constexpr float32 kGizmoTolerance     = 1.0e-4f; ///< 놓은 월드 변환과 이만큼 다르면 기즈모가 옮긴 것이다(행렬 성분)

            /** @brief 감시 값이 읽는 값 하나입니다 — 패널 인스턴스가 게시한 값 중 @p pField 를 @p outValue 에 씁니다. 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readPublished( uint32 CharacterEditorPanel::Published::* pField, float64& outValue )
            {
                const EditorPanelManager*   pManager = editor::findSubsystem<EditorPanelManager>();
                const CharacterEditorPanel* pPanel   = pManager != nullptr ? pManager->findPanel<CharacterEditorPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPublished().*pField );
                return true;
            }

            /** @brief 열린 Character Editor 가 요청한 썸네일 캡처를 아직 거두지 않았으면 진행 중입니다(캡처 뒤 저장소 쓰기는 `LocalStoreRequests` 가 본다). */
            [[nodiscard]] static AutomationReadiness readThumbnailCapture()
            {
                const EditorPanelManager*   pManager = editor::findSubsystem<EditorPanelManager>();
                const CharacterEditorPanel* pPanel   = pManager != nullptr ? pManager->findPanel<CharacterEditorPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return AutomationReadiness::notApplicable();
                return pPanel->isThumbnailCapturePending() ? AutomationReadiness::waiting( 0, "the Character Editor thumbnail capture is not collected yet" ) : AutomationReadiness::ready();
            }

            [[nodiscard]] static bool readBoneGizmoPlaced( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_boneGizmoPlaced, outValue );
            }
            [[nodiscard]] static bool readSocketGizmoPlaced( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_socketGizmoPlaced, outValue );
            }
            /** @brief 고른 소켓의 로컬 위치 x 입니다(실수라 `readPublished` 가 아니라 따로 읽는다). 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readSelectedSocketX( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager*   pManager = editor::findSubsystem<EditorPanelManager>();
                const CharacterEditorPanel* pPanel   = pManager != nullptr ? pManager->findPanel<CharacterEditorPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPublished()._selectedSocketX );
                return true;
            }
            [[nodiscard]] static bool readPreviewPickVertexCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_previewPickVertexCount, outValue );
            }
            [[nodiscard]] static bool readBodyPickCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_bodyPickCount, outValue );
            }
            [[nodiscard]] static bool readPenetrationMarkCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_penetrationMarkCount, outValue );
            }
            [[nodiscard]] static bool readBodyLayerCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_bodyLayerCount, outValue );
            }
            [[nodiscard]] static bool readBodyHiddenSlotCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_bodyHiddenSlotCount, outValue );
            }
            [[nodiscard]] static bool readBodyDataDirty( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_bodyDataDirty, outValue );
            }

            [[nodiscard]] static bool readBoneCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_boneCount, outValue );
            }
            [[nodiscard]] static bool readSocketCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_socketCount, outValue );
            }
            [[nodiscard]] static bool readStatusSet( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_statusSet, outValue );
            }
            [[nodiscard]] static bool readEquippedSlotCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_equippedSlotCount, outValue );
            }
            [[nodiscard]] static bool readExportCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_exportCount, outValue );
            }
            [[nodiscard]] static bool readPresetCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_presetCount, outValue );
            }
            [[nodiscard]] static bool readPreviewAssembleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_previewAssembleCount, outValue );
            }
            [[nodiscard]] static bool readClipPlaying( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_clipPlaying, outValue );
            }
            /** @brief 미리보기 뷰포트의 오빗 거리입니다. 미리보기 시나리오가 휠 줌(`EditorWheel`)을 단언하는 데 쓴다. 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readPreviewOrbitDistance( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager*   pManager = editor::findSubsystem<EditorPanelManager>();
                const CharacterEditorPanel* pPanel   = pManager != nullptr ? pManager->findPanel<CharacterEditorPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPreviewViewport().getOrbitDistance() );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_PANEL( CharacterEditorPanel, CharacterEditorPanel::kPanelID, EditorPanelCategory::Custom, 9020 );

    SW_AUTOMATION_FINISH_BLOCKER( characterEditorThumbnail, "CharacterEditorThumbnail", &CharacterEditorPanelSceneInternal::readThumbnailCapture );

    SW_AUTOMATION_WATCH( editorCharacterEditorBodyLayerCount, "Editor.CharacterEditorBodyLayerCount", "Skin layers in the Character Editor appearance selection (published while the Body tab draws)",
                         &CharacterEditorPanelSceneInternal::readBodyLayerCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorBodyHiddenSlots, "Editor.CharacterEditorBodyHiddenSlots", "Slots the player hid in the Character Editor selection (published while the Body tab draws)",
                         &CharacterEditorPanelSceneInternal::readBodyHiddenSlotCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorBodyDataDirty, "Editor.CharacterEditorBodyDataDirty", "1 while the body data files (channel mapping, body profiles, sections) have unsaved edits",
                         &CharacterEditorPanelSceneInternal::readBodyDataDirty );
    SW_AUTOMATION_WATCH( editorCharacterEditorBoneCount, "Editor.CharacterEditorBoneCount", "Bones of the skeleton the Character Editor has loaded", &CharacterEditorPanelSceneInternal::readBoneCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorStatusSet, "Editor.CharacterEditorStatusSet", "1 while the Character Editor status line holds a message (an edit error or progress)", &CharacterEditorPanelSceneInternal::readStatusSet );
    SW_AUTOMATION_WATCH( editorCharacterEditorSocketCount, "Editor.CharacterEditorSocketCount", "Sockets in the socket file the Character Editor has loaded", &CharacterEditorPanelSceneInternal::readSocketCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorEquippedSlotCount, "Editor.CharacterEditorEquippedSlotCount", "Slots that hold an item in the appearance selection",
                         &CharacterEditorPanelSceneInternal::readEquippedSlotCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorExportCount, "Editor.CharacterEditorExportCount", "Data presets the Character Editor exported in this run",
                         &CharacterEditorPanelSceneInternal::readExportCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPresetCount, "Editor.CharacterEditorPresetCount", "Presets in the Character Editor's preset store", &CharacterEditorPanelSceneInternal::readPresetCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPreviewAssembleCount, "Editor.CharacterEditorPreviewAssembleCount",
                         "Times the Character Editor preview body was assembled (published while the Preview tab draws)", &CharacterEditorPanelSceneInternal::readPreviewAssembleCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPenetrationMarks, "Editor.CharacterEditorPenetrationMarks", "Penetration points the Character Editor preview drew last frame (red crosses on the body)",
                         &CharacterEditorPanelSceneInternal::readPenetrationMarkCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPreviewPickVertices, "Editor.CharacterEditorPreviewPickVertices",
                         "Mesh vertices of the Character Editor preview body and its part objects (published while the Preview tab draws)", &CharacterEditorPanelSceneInternal::readPreviewPickVertexCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorBodyPickCount, "Editor.CharacterEditorBodyPickCount", "Surface picks (skin layer placement or section) the Character Editor hit on the posed body or a part in the preview viewport",
                         &CharacterEditorPanelSceneInternal::readBodyPickCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorBoneGizmoPlaced, "Editor.CharacterEditorBoneGizmoPlaced", "1 while the Character Editor preview holds a gizmo marker on the selected bone",
                         &CharacterEditorPanelSceneInternal::readBoneGizmoPlaced );
    SW_AUTOMATION_WATCH( editorCharacterEditorSocketGizmoPlaced, "Editor.CharacterEditorSocketGizmoPlaced", "1 while the Character Editor preview holds the gizmo on the selected socket",
                         &CharacterEditorPanelSceneInternal::readSocketGizmoPlaced );
    SW_AUTOMATION_WATCH( editorCharacterEditorSelectedSocketX, "Editor.CharacterEditorSelectedSocketX", "Local translation x (m) of the socket selected in the Character Editor",
                         &CharacterEditorPanelSceneInternal::readSelectedSocketX );
    SW_AUTOMATION_WATCH( editorCharacterEditorPreviewClipPlaying, "Editor.CharacterEditorPreviewClipPlaying", "1 while the Character Editor preview body plays the selected clip (published while the Preview tab draws)",
                         &CharacterEditorPanelSceneInternal::readClipPlaying );
    SW_AUTOMATION_WATCH( editorPreviewViewportOrbitDistance, "Editor.PreviewViewportOrbitDistance",
                         "Orbit distance of the Character Editor preview viewport camera (wheel zoom shrinks it)", &CharacterEditorPanelSceneInternal::readPreviewOrbitDistance );
} // namespace sw::editor

namespace sw::editor
{
    float64 CharacterEditorPanel::readNowSeconds()
    {
        return static_cast<float64>( MonotonicClock::nowNanoseconds() ) / static_cast<float64>( constant::time::kNanosecondsPerSecond );
    }

    CharacterEditorPanel::CharacterEditorPanel()
        : EditorPreviewPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, true }
        , _document{}
        , _newName{}
        , _renameBuffer{}
        , _bClipLoop{ true }
        , _previewBody{}
        , _previewBodyRevision{ 0 }
        , _previewDatabaseRevision{ 0 }
        , _bPreviewBodyDressed{ SW_FALSE }
        , _bModelsWereDirty{ SW_FALSE }
        , _bCustomizationOpen{ SW_TRUE }
        , _bCustomizationActive{ SW_FALSE }
        , _bGizmoPlaced{ SW_FALSE }
        , _bBodyPicking{ SW_FALSE }
        , _bBodyPickDrag{ SW_FALSE }
        , _bBodyPickShown{ SW_FALSE }
        , _seenFitRevision{ 0 }
        , _gizmo{}
        , _gizmoProxy{}
        , _placedGizmoWorld{}
        , _placedGizmoTarget{ CharacterGizmoTarget::None }
        , _bRigPreview{ false }
        , _published{}
        , _bodyState{}
        , _bodyHighlightDrive{ constant::invalid_index::kUint32 }
        , _bodySelectedSection{}
        , _bodyPickLayer{ constant::invalid_index::kUint32 }
        , _bodyPickMarker{}
        , _bodyStepTimer{ 0.0f }
        , _thumbnailCapturePreset{}
        , _thumbnailCaptureTicket{ 0 }
    {
    }

    bool CharacterEditorPanel::saveDocument()
    {
        if ( _document.saveFiles( readNowSeconds() ) == false )
            return false; // 까닭은 상태 줄에 남았다
        acknowledgePreviewEdits();
        return true;
    }

    void CharacterEditorPanel::collectFilesToSave( vector<string>& outListPath ) const
    {
        vector<string> listAssetID;
        _document.collectFilesToSave( listAssetID );
        for ( const string& assetID : listAssetID )
            appendFileToSave( assetID, outListPath );
    }

    void CharacterEditorPanel::syncDocumentDirty()
    {
        // 소켓 · 포즈 편집은 모델 안의 기록이고, 미리보기 씬 편집(인스펙터로 몸을 고친 것)은 창 스택이다 — 둘 다 문서의 미저장이다.
        // 모델이 되돌리기로 저장 상태에 돌아오면 dirty 를 내리고, 그 뒤 기반 클래스가 창 스택의 새 편집을 다시 올린다.
        const bool bModelsDirty = _document.isDirty();
        if ( bModelsDirty )
            markDocumentDirty();
        else if ( _bModelsWereDirty )
            clearDocumentDirty();
        _bModelsWereDirty = bModelsDirty;
        EditorPreviewPanel::syncDocumentDirty();
    }

    bool CharacterEditorPanel::canAcceptDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, SocketSet::kExtension );
    }

    ToolAssetLoadResult CharacterEditorPanel::loadDocument()
    {
        // 콘텐츠 브라우저에서 고른 소켓 파일이 있으면 그것을, 없으면 경로 칸(팩 데이터의 소켓 파일)을 연다.
        const string_view focused = getMatchingFocusedPath();
        if ( focused.empty() == false )
        {
            acceptFocusedDocument();
            _document.editPaths()._socketPath = focused;
        }
        const CharacterEditorPaths& paths = _document.getPaths();
        if ( paths._socketPath.empty() )
            return ToolAssetLoadResult::Missing;
        if ( editor::getSubsystem<AssetPaths>().findLooseFile( string_view( paths._socketPath.c_str() ) ).has_value() == false )
            return ToolAssetLoadResult::Missing; // 새 소켓 파일 — 저장하면 만든다
        return _document.loadSocketFile() ? ToolAssetLoadResult::Loaded : ToolAssetLoadResult::Malformed;
    }

    ScenePreviewPolicy CharacterEditorPanel::getPreviewPolicy() const
    {
        ScenePreviewPolicy policy = EditorPreviewPanel::getPreviewPolicy();
        policy._bPlay             = true; // 몸의 애니메이터 · 외형 조립은 게임과 같이 onBeginPlay 와 틱으로 돈다(언리얼 FPreviewScene)
        return policy;
    }

    void CharacterEditorPanel::populatePreviewScene( Scene& scene )
    {
        (void)scene; // 몸은 외형 데이터를 읽은 뒤에 세운다(`syncPreviewBody`)
        _previewBody         = GameObjectHandle{};
        _bPreviewBodyDressed = SW_FALSE;
        _gizmoProxy          = GameObjectHandle{}; // 창을 닫았다 다시 열면 새 씬이다 — 앞 씬의 표시 오브젝트 핸들이 새 씬의 다른 오브젝트로 풀리지 않게
        _bGizmoPlaced        = SW_FALSE;
    }

    void CharacterEditorPanel::syncPreviewGizmo()
    {
        GameObjectManager*         pManager   = findPreviewManager();
        GameObject*                pBody      = pManager != nullptr ? pManager->resolveGameObject( _previewBody ) : nullptr;
        GameObject*                pProxy     = pManager != nullptr ? pManager->resolveGameObject( _gizmoProxy ) : nullptr;
        const SceneComponent*      pBodyScene = pBody != nullptr ? pBody->getPrimarySceneComponent() : nullptr;
        const CharacterGizmoTarget target     = _document.getGizmoTarget();
        float4x4                   desiredWorld{};
        float4x4                   parentWorld{};
        bool                       bFrame = false;
        if ( pBodyScene != nullptr && target == CharacterGizmoTarget::Bone )
            bFrame = findBoneGizmoFrame( pBodyScene->getWorldMatrix(), desiredWorld, parentWorld );
        else if ( pBodyScene != nullptr && target == CharacterGizmoTarget::Socket )
            bFrame = findSocketGizmoFrame( *pBody, desiredWorld, parentWorld );
        if ( bFrame == false )
        {
            if ( pProxy != nullptr )
                pManager->destroyGameObject( pProxy );
            _gizmoProxy                   = GameObjectHandle{};
            _bGizmoPlaced                 = SW_FALSE;
            _published._boneGizmoPlaced   = 0;
            _published._socketGizmoPlaced = 0;
            return;
        }
        if ( pProxy == nullptr )
        {
            pProxy = pManager->createGameObject( hashed_string( "GizmoProxy" ) );
            if ( pProxy == nullptr || pProxy->addComponent<SceneComponent>() == nullptr )
            {
                _document.setStatus( "The gizmo target could not be created" );
                return;
            }
            _gizmoProxy   = pProxy->getHandle();
            _bGizmoPlaced = SW_FALSE;
        }
        SceneComponent* pScene = pProxy->getPrimarySceneComponent();
        if ( pScene == nullptr )
            return;
        if ( getPreviewSelection().getPrimaryObject() != pProxy )
            getPreviewSelection().selectObject( pProxy );
        // 주인이 바뀌었으면 지난 자리와 견주지 않는다 — 다른 본 · 소켓의 자리를 "끌었다" 로 읽어 옮기면 안 된다.
        if ( target != _placedGizmoTarget )
        {
            _bGizmoPlaced = SW_FALSE;
            _document.getPoseModel().endContinuousEdit();
            _document.getSocketModel().endContinuousEdit();
        }
        // 지난 그리기에 놓은 자리와 달라졌으면 기즈모가 옮긴 것이다 — 주인의 데이터(포즈 덮어쓰기 · 소켓 로컬 변환)로 되돌린다.
        bool           bMoved       = false;
        const float4x4 currentWorld = pScene->getWorldMatrix();
        if ( _bGizmoPlaced )
        {
            for ( uint32 element = 0; element < 16 && bMoved == false; ++element )
            {
                bMoved = MathUtil::abs( currentWorld.data()[element] - _placedGizmoWorld.data()[element] ) > CharacterEditorPanelSceneInternal::kGizmoTolerance;
            }
        }
        if ( bMoved )
        {
            applyGizmoWorld( target, currentWorld, parentWorld );
        }
        else if ( _bGizmoPlaced && _gizmo.hasMouse() == false )
        {
            // 끌기가 끝났다 — 다음 편집은 새 되돌리기 단계
            _document.getPoseModel().endContinuousEdit();
            _document.getSocketModel().endContinuousEdit();
        }
        if ( bMoved == false )
        {
            pScene->setWorldTransform( desiredWorld );
            _placedGizmoWorld = pScene->getWorldMatrix();
        }
        else
        {
            _placedGizmoWorld = currentWorld; // 다음 그리기는 모델 값으로 다시 놓는다(위 `bMoved == false` 경로가 desired 로 맞춘다)
        }
        _bGizmoPlaced                 = SW_TRUE;
        _placedGizmoTarget            = target;
        _published._boneGizmoPlaced   = target == CharacterGizmoTarget::Bone ? 1u : 0u;
        _published._socketGizmoPlaced = target == CharacterGizmoTarget::Socket ? 1u : 0u;
    }

    bool CharacterEditorPanel::findBoneGizmoFrame( const float4x4& bodyWorld, float4x4& outWorld, float4x4& outParentWorld ) const
    {
        const BonePoseEditModel& pose      = _document.getPoseModel();
        const Skeleton&          skeleton  = pose.getSkeleton();
        const optional<uint32>   boneIndex = skeleton.findBoneIndex( _document.getSelectedBone() );
        if ( boneIndex.has_value() == false )
            return false; // 고른 본이 없다(고르지 않았거나 다른 스켈레톤) — 기즈모를 그리지 않는다
        // 덮어쓴 값을 얹은 모델 변환 — 부모까지 줄을 올라가 루트부터 곱한다(행벡터: 자식 × 부모).
        vector<int32> listChain;
        for ( int32 index = static_cast<int32>( *boneIndex ); index >= 0; index = skeleton.getBone( static_cast<uint32>( index ) )._parentIndex )
        {
            listChain.push_back( index );
        }
        float4x4 parentModel = float4x4::Identity;
        float4x4 boneModel   = float4x4::Identity;
        for ( size_t chainIndex = listChain.size(); chainIndex > 0; --chainIndex )
        {
            BoneTransform local{};
            if ( pose.getEffectiveTransform( skeleton.getBone( static_cast<uint32>( listChain[chainIndex - 1] ) )._name, local ) == false )
                return false;
            parentModel = boneModel;
            boneModel   = local.toMatrix() * boneModel;
        }
        outWorld       = boneModel * bodyWorld;
        outParentWorld = parentModel * bodyWorld;
        return true;
    }

    bool CharacterEditorPanel::findSocketGizmoFrame( const GameObject& body, float4x4& outWorld, float4x4& outParentWorld ) const
    {
        // 편집 중(저장 전) 소켓 정의를 몸의 지금 포즈에 얹는다 — 소켓 축(`drawSocketAxes`)과 같은 뼈 배열이다. 부모 해석은 런타임의 것(`SocketSet::computeParentTransform`)이다.
        const SkeletalMeshComponent* pUnit      = body.getComponent<SkeletalMeshComponent>();
        const SceneComponent*        pBodyScene = body.getPrimarySceneComponent();
        const SocketSet&             sockets    = _document.getSocketModel().getSockets();
        const SocketDef*             pSocket    = sockets.findSocket( _document.getSelectedSocket() );
        if ( pUnit == nullptr || pBodyScene == nullptr || pSocket == nullptr )
            return false;
        CharacterBoneArray poseBones;
        CharacterPoseUtil::makeBindBones( pUnit->getSkeleton(), poseBones );
        (void)CharacterPoseUtil::copyUnitPose( *pUnit, poseBones ); // 포즈가 아직 없으면 바인드 포즈 그대로 둔다(정상)
        float4x4 parentUnit{};
        if ( sockets.computeParentTransform( *pSocket, poseBones, parentUnit ) == false )
            return false; // 모르는 부모 본 — 소켓 축이 상태 줄에 이유를 남긴다
        outParentWorld = parentUnit * pBodyScene->getWorldMatrix();
        outWorld       = pSocket->makeLocalTransform() * outParentWorld;
        return true;
    }

    void CharacterEditorPanel::applyGizmoWorld( CharacterGizmoTarget target, const float4x4& world, const float4x4& parentWorld )
    {
        switch ( target )
        {
            case CharacterGizmoTarget::None:
                break;
            case CharacterGizmoTarget::Bone:
            {
                // 부모 월드의 역으로 본의 로컬을 구해 레퍼런스 포즈 덮어쓰기에 넣는다(끄는 동안은 한 되돌리기 단계).
                BonePoseEditModel&   pose    = _document.getPoseModel();
                const BoneTransform  local   = BoneTransform::makeFromMatrix( world * parentWorld.invert() );
                const float3         degrees = local._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
                const expected<bool> moved   = pose.setTranslation( _document.getSelectedBone(), local._translation, true );
                const expected<bool> turned  = pose.setRotationDegrees( _document.getSelectedBone(), degrees, true );
                _document.reportEdit( moved.has_value() ? turned : moved ); // 같은 값이면 false 다 — 오류만 상태 줄에 남는다
                break;
            }
            case CharacterGizmoTarget::Socket:
            {
                // 월드 두 행렬에 몸의 월드가 함께 곱혀 있어 나누면 유닛 공간과 같은 로컬이다 — 소켓 파일의 부모 기준 변환으로 쓴다.
                _document.reportEdit( _document.getSocketModel().setUnitTransform( _document.getSelectedSocket(), world, parentWorld, true ) );
                break;
            }
        }
    }

    void CharacterEditorPanel::syncRigPreview( GameObject& body )
    {
        PoseModifierComponent* pRig = body.getComponent<PoseModifierComponent>();
        if ( _bRigPreview == false )
        {
            if ( pRig != nullptr && pRig->isRigEnabled() )
                pRig->setRigEnabled( false );
            return;
        }
        if ( pRig == nullptr )
        {
            pRig = body.addComponent<PoseModifierComponent>();
            if ( pRig == nullptr )
            {
                _document.setStatus( "The preview body could not take the constraint rig" );
                _bRigPreview = false;
                return;
            }
        }
        const string_view rigPath( _document.getPaths()._rigPath.c_str() );
        if ( string_view( pRig->getRigPath() ) != rigPath )
            pRig->setRigPath( rigPath );
        if ( pRig->isRigEnabled() == false )
            pRig->setRigEnabled( true );
    }

    void CharacterEditorPanel::syncPreviewBody()
    {
        GameObjectManager* pManager = findPreviewManager();
        if ( pManager == nullptr )
            return;
        GameObject* pBody = pManager->resolveGameObject( _previewBody );
        if ( pBody == nullptr )
        {
            pBody = pManager->createGameObject( hashed_string( "PreviewCharacter" ) );
            if ( pBody == nullptr )
            {
                _document.setStatus( "The preview body could not be created" );
                return;
            }
            // 애니메이터는 클립 폴더만 가진다(그래프 없이 클립 하나를 재생). 포즈는 같은 오브젝트의 유닛이 외형 조립으로 받은 스켈레톤으로 낸다.
            // 유닛(`SkeletalMeshComponent`)을 먼저 둔다 — 애니메이터는 시작할 때 같은 오브젝트의 유닛에 붙는다.
            const bool                 bUnitAndLook = pBody->addComponent<SkeletalMeshComponent>() != nullptr && pBody->addComponent<CharacterAppearanceComponent>() != nullptr;
            SkeletalAnimatorComponent* pAnimator    = bUnitAndLook ? pBody->addComponent<SkeletalAnimatorComponent>() : nullptr;
            const bool                 bComponents  = bUnitAndLook && pAnimator != nullptr;
            if ( bComponents == false )
            {
                pManager->destroyGameObject( pBody );
                _document.setStatus( "The preview body could not take its components" );
                return;
            }
            pAnimator->setAnimationSetPath( string_view( _document.getPaths()._animationSet.c_str() ) );
            // 몸 동작(굽 · 지글 · 동공 · 체액)은 몸 프로필이 있는 외형에서만 일한다 — 없는 몸에서는 쉰다. 몸 탭의 미리보기 단추가 이것을 부른다.
            // 이름 칸은 팩 데이터(charactereditor.xml 의 wipeEvent)에서 온다 — 코드로 붙인 컴포넌트는 데이터 파일이 칸을 채우지 못한다.
            CharacterBodyDynamicsComponent* pDynamics = pBody->addComponent<CharacterBodyDynamicsComponent>();
            if ( pDynamics != nullptr )
                pDynamics->setWipeEvent( _document.getPreviewWipeEvent() );
            _previewBody         = pBody->getHandle();
            _bPreviewBodyDressed = SW_FALSE;
        }
        CharacterAppearanceComponent* pAppearance = pBody->getComponent<CharacterAppearanceComponent>();
        const AppearanceEditModel&    appearance  = _document.getAppearanceModel();
        const AppearanceDatabase&     database    = _document.getDatabase();
        const bool                    bChanged    = _bPreviewBodyDressed == false || _previewBodyRevision != appearance.getRevision() || _previewDatabaseRevision != database.getRevision();
        // 슬라이더를 끄는 동안은 다시 입히지 않는다 — 피팅이 무거워 놓을 때 한 번 푼다(첫 입히기는 기다리지 않는다).
        const bool bDeferred = _bCustomizationActive && _bPreviewBodyDressed;
        const bool bSkip     = pAppearance == nullptr || bChanged == false;
        if ( bSkip || bDeferred )
            return;
        // 기준 프리셋이 없는 선택은 팩 데이터의 미리보기 몸 위에 입힌다(`makePreviewSelection`).
        pAppearance->previewSelection( _document.makePreviewSelection(), database );
        _previewBodyRevision     = appearance.getRevision();
        _previewDatabaseRevision = database.getRevision();
        _bPreviewBodyDressed     = SW_TRUE;
    }

    GameObjectManager* CharacterEditorPanel::findPreviewManager() const
    {
        Scene* pScene = getPreviewScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    void CharacterEditorPanel::revertDocument()
    {
        _document.revert();
    }

    void CharacterEditorPanel::collectPickObjects( const GameObject& body, vector<const GameObject*>& outListObject ) const
    {
        outListObject.clear();
        outListObject.push_back( &body ); // 몸이 첫 오브젝트다 — 피부 층 배치는 이것만 본다
        const CharacterAppearanceComponent* pAppearance = body.getComponent<CharacterAppearanceComponent>();
        const GameObjectManager*            pManager    = findPreviewManager();
        if ( pAppearance == nullptr || pManager == nullptr )
            return;
        vector<GameObjectHandle> listPart;
        pAppearance->collectPartObjects( listPart );
        for ( const GameObjectHandle& handle : listPart )
        {
            const GameObject* pPart = pManager->resolveGameObject( handle );
            if ( pPart != nullptr )
                outListObject.push_back( pPart );
        }
    }

    void CharacterEditorPanel::saveThumbnail()
    {
        if ( _thumbnailCaptureTicket != 0 )
        {
            _document.setStatus( "A thumbnail capture is still running" );
            return;
        }
        if ( _document.canAttachThumbnail() == false )
            return; // 까닭은 상태 줄에 남았다
        const string presetName  = string{ _document.getPaths()._presetName.c_str() };
        const string requestName = "charactereditor.thumbnail." + presetName;
        const uint32 ticket      = getPreviewViewport().requestImagePNGBytes( requestName );
        if ( ticket == 0 )
        {
            _document.setStatus( "The thumbnail needs the Preview tab drawn at least once" );
            return;
        }
        _thumbnailCaptureTicket = ticket;
        _thumbnailCapturePreset = presetName;
        SW_LOG_INFO_ID( "CharacterEditorPanel.CharacterEditorThumbnailPreset", "Character Editor: thumbnail of preset '%#' requested", _thumbnailCapturePreset.c_str() );
    }

    void CharacterEditorPanel::pollThumbnailCapture()
    {
        if ( _thumbnailCaptureTicket == 0 )
            return;
        // 제 표의 결과만 읽는다 — 같은 실행의 다른 요청(개발 명령 screenshot · 에디터 버튼)이 끝나도 이 요청의 끝으로 읽히지 않는다.
        if ( EditorPreviewViewport::findImageFileResult( _thumbnailCaptureTicket ) == ScreenshotTicketState::Pending )
            return;
        const uint32 ticket     = _thumbnailCaptureTicket;
        _thumbnailCaptureTicket = 0;
        vector<uint8> pngBytes;
        // 인코딩에 실패했거나 결과 기록 창을 벗어났으면(`Unknown`) 받지 못한다.
        if ( EditorPreviewViewport::takeImagePNGBytes( ticket, pngBytes ) == false )
        {
            _document.setStatus( "The thumbnail capture failed - try again" );
            return;
        }
        _document.attachThumbnail( _thumbnailCapturePreset, std::move( pngBytes ) );
    }

    bool CharacterEditorPanel::findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        outCenter = float3{ 0.0f, 1.0f, 0.0f };
        outRadius = CharacterEditorPanelSceneInternal::kPreviewFrameRadius;
        return true;
    }
} // namespace sw::editor
