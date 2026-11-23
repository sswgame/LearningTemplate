// 캐릭터 편집 창 미리보기 탭의 값 모으기와 명령입니다(ImGui 없음) — 소켓 축 · 관통 점 · 몸 집기 윤곽 선, 애니메이션 재생, 몸 표면 집기.
// 그리기는 `CharacterEditorPanel.cpp`(View)이고 `PreviewViewState` 값만 본다(언리얼 컴포넌트 비주얼라이저가 FPrimitiveDrawInterface 에 선 값을 넘기는 가름).
#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/CharacterEditorPanel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodySurfacePick.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPanelPreviewInternal
        {
            static constexpr float32 kSocketAxisLength         = 0.08f; ///< 미리보기 소켓 축 길이(m)
            static constexpr float32 kSelectedSocketAxisLength = 0.18f; ///< 고른 소켓의 축 길이(m)
            static constexpr float32 kSocketAxisAlpha          = 0.6f;  ///< 고르지 않은 소켓 축의 알파
            static constexpr float32 kPenetrationMarkLength    = 0.01f; ///< 관통 점 십자의 한 팔 길이(m)
            static constexpr float32 kPickMarkLength           = 0.02f; ///< 집은 자리 십자의 한 팔 길이(m)
            static constexpr uint32  kHighlightTriangleBudget  = 4000;  ///< 강조 구간 윤곽으로 긋는 삼각형 상한(넘으면 고르게 건너뛴다 — 큰 몸에서 프레임을 지키려고)
            static constexpr float4  kPenetrationColor{ 1.0f, 0.2f, 0.2f, 1.0f };

            /** @brief 축 셋(X · Y · Z)과 그 테마 색 역할입니다. */
            static constexpr EditorVisualizerColorRole kArrAxisRole[] = { EditorVisualizerColorRole::AxisX, EditorVisualizerColorRole::AxisY, EditorVisualizerColorRole::AxisZ };

            static float3 getAxis( uint32 axis ) { return float3{ axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f }; }

            /** @brief 가운데 @p center 에 팔 길이 @p arm 의 세 축 십자를 더합니다. */
            static void addCross( vector<EditorVisualizerLine>& outListLine, const float3& center, float32 arm, const EditorVisualizerLine& prototype )
            {
                for ( uint32 axis = 0; axis < 3; ++axis )
                {
                    EditorVisualizerLine line = prototype;
                    line._from                = center - getAxis( axis ) * arm;
                    line._to                  = center + getAxis( axis ) * arm;
                    outListLine.push_back( line );
                }
            }

            /** @brief 오브젝트가 그리는 메시입니다(메시 컴포넌트 — 스켈레탈이면 그 사본). 없으면 nullptr 입니다. */
            static const Mesh* findPickMesh( const GameObject& object )
            {
                const MeshComponent* pMesh = object.getComponent<MeshComponent>();
                return pMesh != nullptr ? pMesh->getRawMesh() : nullptr;
            }
            /** @brief 집기 오브젝트들의 메시 정점 수 합입니다(감시 값 — 부품이 빠지거나 몸이 다시 구워지면 바뀐다). */
            static uint32 countPickVertices( const vector<const GameObject*>& listObject )
            {
                uint32 count = 0;
                for ( const GameObject* pObject : listObject )
                {
                    const Mesh* pMesh = findPickMesh( *pObject );
                    count += pMesh != nullptr ? pMesh->getVertexCount() : 0u;
                }
                return count;
            }
            /**
             * @brief 집기 오브젝트(첫 것이 몸)의 메시를 지금 포즈로 월드에 옮긴 표면을 짓습니다(`BodyPickSurface`). 스켈레탈 유닛이면 그 스킨 팔레트로 스키닝한다.
             * @details 몸 메시가 없으면(조립 전) 첫 표면이 몸이 아니다 — 쓰는 쪽이 `_bSkin` 을 본다.
             */
            static void buildPickSurfaces( const vector<const GameObject*>& listObject, vector<BodyPickSurface>& outListSurface )
            {
                outListSurface.clear();
                const vector<float4x4> listNoPalette{}; // 단단한 부품 — 스키닝하지 않는다
                const vector<float32>  listNoWeight{};  // 유닛이 없으면 모프 가중치가 없다
                for ( size_t index = 0; index < listObject.size(); ++index )
                {
                    const GameObject&            object = *listObject[index];
                    const Mesh*                  pMesh  = findPickMesh( object );
                    const SkeletalMeshComponent* pUnit  = object.getComponent<SkeletalMeshComponent>();
                    const SceneComponent*        pScene = object.getPrimarySceneComponent();
                    // 스킨 없는 부품(그리기용으로 읽은 파일)은 CPU 정점을 놓는다 — 집으려면 쥐도록 요청한다(놓았으면 파일을 다시 읽는다, 그 뒤로는 쥔다).
                    shared_ptr<Mesh> pKeptMesh{};
                    if ( pMesh != nullptr && pMesh->getCPUAccess() != MeshCPUAccess::Keep )
                    {
                        pKeptMesh = MeshCache::acquire( pMesh->getSourceAsset().str(), MeshCPUAccess::Keep );
                        pMesh     = pKeptMesh.get();
                    }
                    if ( pMesh == nullptr || pScene == nullptr || pMesh->getVertexCount() == 0 )
                        continue;
                    BodyPickSurface& surface = outListSurface.emplace_back();
                    BodyPickSurface::build( pMesh->getCPUView().getVertices(), pMesh->getMorphTargets(), pUnit != nullptr ? pUnit->getMorphWeights() : listNoWeight, pMesh->getSkinVertices(),
                                            pUnit != nullptr ? pUnit->getSkinPalette() : listNoPalette, pScene->getWorldMatrix(), surface );
                    surface._listIndex   = pMesh->getIndices();
                    surface._listSection = pMesh->getSections();
                    surface._bSkin       = index == 0 ? SW_TRUE : SW_FALSE;
                }
            }
        };
    } // namespace

    GameObject* CharacterEditorPanel::resolvePreviewBody() const
    {
        GameObjectManager* pManager = findPreviewManager();
        return pManager != nullptr ? pManager->resolveGameObject( _previewBody ) : nullptr;
    }

    void CharacterEditorPanel::postBodyPreview( const BodyCustomizationViewModel::PreviewCommand& command )
    {
        BodyCustomizationViewModel::applyPreview( resolvePreviewBody(), command );
    }

    void CharacterEditorPanel::gatherPreview()
    {
        PreviewViewState& state = _previewState;
        state._listSocketAxisLine.clear();
        state._listPenetrationLine.clear();
        state._listPickLine.clear();
        _published._previewPickVertexCount = 0;

        GameObject*                         pBody       = resolvePreviewBody();
        const CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
        const SkeletalAnimatorComponent*    pAnimator   = pBody != nullptr ? pBody->getComponent<SkeletalAnimatorComponent>() : nullptr;
        const hashed_string&                clip        = _document.getSelectedClip();
        state._bHasBody                                 = pBody != nullptr;
        state._bHasAnimator                             = pAnimator != nullptr;
        state._bClipPlaying                             = pAnimator != nullptr && clip.empty() == false && pAnimator->getCurrentStateName() == clip && pAnimator->isAnimationActive();
        state._assembleCount                            = pAppearance != nullptr ? pAppearance->getAssembleCount() : 0u;
        _published._previewAssembleCount                = state._assembleCount;
        _published._clipPlaying                         = state._bClipPlaying ? 1u : 0u;
        if ( pBody == nullptr )
            return;
        if ( _document.getPaths()._rigPath.empty() == false )
            syncRigPreview( *pBody );
        gatherBodyPickOutline( *pBody );
        if ( pAppearance != nullptr )
            gatherSocketAxes( *pBody );
        gatherPenetrationPoints( *pBody );
        // 미리보기 조립이 푼 피팅 결과를 확인 표시에 올린다(세대가 바뀔 때만).
        const bool bNewFit = pAppearance != nullptr && pAppearance->getLastFitResult() != nullptr && pAppearance->getFitResultRevision() != _seenFitRevision;
        if ( bNewFit )
        {
            _document.setFitResult( *pAppearance->getLastFitResult() );
            _seenFitRevision = pAppearance->getFitResultRevision();
        }
    }

    bool CharacterEditorPanel::playPreviewClip()
    {
        GameObject*                pBody     = resolvePreviewBody();
        SkeletalAnimatorComponent* pAnimator = pBody != nullptr ? pBody->getComponent<SkeletalAnimatorComponent>() : nullptr;
        const hashed_string&       clip      = _document.getSelectedClip();
        return pAnimator != nullptr && pAnimator->preloadClip( clip ) && pAnimator->play( clip, _bClipLoop, 0.0f );
    }

    void CharacterEditorPanel::stopPreviewClip()
    {
        GameObject*                pBody     = resolvePreviewBody();
        SkeletalAnimatorComponent* pAnimator = pBody != nullptr ? pBody->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator != nullptr )
            pAnimator->stop();
    }

    void CharacterEditorPanel::gatherBodyTab()
    {
        BodyCustomizationViewModel::gather( _document.getAppearanceModel(), _document.getDatabase(), resolvePreviewBody(), _bodyHighlightDrive, _bodyState );
    }

    void CharacterEditorPanel::gatherSocketAxes( const GameObject& body )
    {
        using Internal                          = CharacterEditorPanelPreviewInternal;
        const SkeletalMeshComponent* pUnit      = body.getComponent<SkeletalMeshComponent>();
        const SceneComponent*        pBodyScene = body.getPrimarySceneComponent();
        if ( pUnit == nullptr || pBodyScene == nullptr )
            return;
        // 편집 중(저장 전) 소켓 정의를 몸의 지금 포즈에 얹어 자리를 구한다 — 저장하지 않아도 끄는 대로 축이 따라온다.
        CharacterBoneArray bindBones;
        CharacterPoseUtil::makeBindBones( pUnit->getSkeleton(), bindBones );
        CharacterBoneArray poseBones = bindBones;
        (void)CharacterPoseUtil::copyUnitPose( *pUnit, poseBones ); // 포즈가 아직 없으면 바인드 포즈 그대로 둔다(정상)
        ResolvedSocketTable table;
        CharacterIssueList  resolveIssues;
        table.beginResolve();
        const SocketSet& sockets   = _document.getSocketModel().getSockets();
        const bool       bResolved = table.addUnit( hashed_string{}, 0, sockets, bindBones, nullptr, resolveIssues );
        table.endResolve();
        if ( bResolved == false )
        {
            const string resolveError = resolveIssues.joined();
            _document.setStatus( resolveError ); // 모르는 부모 본 같은 편집 오류 — 축을 그리지 않고 알린다
            _socketResolveError = resolveError;
            return;
        }
        // 프레임마다 다시 푸는 오류라 풀리면 지운다 — 몸이 외형을 입기 전(뼈 하나짜리 스켈레톤) 프레임의 오류가 상태 줄에 남아 있었다. 다른 일이 남긴 글은 건드리지 않는다.
        if ( _socketResolveError.empty() == false && _document.getStatus() == _socketResolveError )
            _document.setStatus( string_view{} );
        _socketResolveError.clear();
        const float4x4 bodyWorld = pBodyScene->getWorldMatrix();
        for ( const SocketDef& socket : sockets.getSockets() )
        {
            const SocketID socketID = table.findSocket( socket._name );
            float4x4       unitTransform{};
            if ( socketID == kInvalidSocketID || table.computeUnitTransform( socketID, poseBones, unitTransform ) == false )
                continue;
            const float4x4   world     = unitTransform * bodyWorld;
            const bool       bSelected = socket._name == _document.getSelectedSocket();
            const float32    length    = bSelected ? Internal::kSelectedSocketAxisLength : Internal::kSocketAxisLength;
            const float3     origin    = world.getTranslation();
            const quaternion rotation  = world.getRotation();
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                EditorVisualizerLine line{};
                line._from      = origin;
                line._to        = origin + float3::transform( Internal::getAxis( axis ), rotation ) * length;
                line._color     = float4{ 1.0f, 1.0f, 1.0f, bSelected ? 1.0f : Internal::kSocketAxisAlpha };
                line._colorRole = Internal::kArrAxisRole[axis];
                _previewState._listSocketAxisLine.push_back( line );
            }
        }
    }

    void CharacterEditorPanel::gatherPenetrationPoints( const GameObject& body )
    {
        using Internal                = CharacterEditorPanelPreviewInternal;
        const SceneComponent*  pScene = body.getPrimarySceneComponent();
        const FitCheckSummary& fit    = _document.getFit();
        if ( _document.hasFit() == false || fit.hasPenetration() == false || pScene == nullptr )
            return;
        // 피팅은 바인드 공간에서 풀므로 몸 오브젝트의 월드 변환으로 옮긴다. 십자 표시 하나가 정점 하나다(앞쪽 일부만 들고 있다).
        const float4x4       world = pScene->getWorldMatrix();
        EditorVisualizerLine prototype{};
        prototype._color = Internal::kPenetrationColor;
        for ( const FitPenetrationSummary& penetration : fit._listPenetration )
        {
            for ( const float3& point : penetration._listPoint )
                Internal::addCross( _previewState._listPenetrationLine, float3::transform( point, world ), Internal::kPenetrationMarkLength, prototype );
        }
    }

    void CharacterEditorPanel::gatherBodyPickOutline( const GameObject& body )
    {
        using Internal = CharacterEditorPanelPreviewInternal;
        vector<const GameObject*> listObject;
        collectPickObjects( body, listObject );
        _published._previewPickVertexCount = Internal::countPickVertices( listObject );
        EditorVisualizerLine highlight{};
        highlight._colorRole = EditorVisualizerColorRole::SelectionOutline;
        // 드라이브가 고른 구간 · 고른 구간을 윤곽으로 긋는다(지금 포즈 — 집기와 같다). 표면 스키닝은 윤곽이 있을 때만 한다(CPU 스키닝이 공짜가 아니다).
        bool bOutline = _bodySelectedSection.has_value() && *_bodySelectedSection < _bodyState._listSection.size();
        for ( const BodyCustomizationViewState::SectionRow& row : _bodyState._listSection )
            bOutline = bOutline || row._bHighlighted == SW_TRUE;
        vector<BodyPickSurface> listSurface;
        if ( bOutline )
            Internal::buildPickSurfaces( listObject, listSurface );
        for ( const BodyPickSurface& surface : listSurface )
        {
            for ( const MeshSection& section : surface._listSection )
            {
                const optional<uint32> row = BodyCustomizationViewModel::findSectionRow( _bodyState, section._name );
                if ( row.has_value() == false || *row >= _bodyState._listSection.size() ||
                     ( _bodyState._listSection[*row]._bHighlighted == SW_FALSE && _bodySelectedSection != *row ) )
                    continue;
                // 큰 메시는 삼각형을 고르게 건너뛴다.
                const uint32 triangleCount = section._indexCount / 3u;
                const uint32 stride        = triangleCount > Internal::kHighlightTriangleBudget ? triangleCount / Internal::kHighlightTriangleBudget + 1u : 1u;
                for ( uint32 triangle = 0; triangle < triangleCount; triangle += stride )
                {
                    const uint32 first = section._firstIndex + triangle * 3u;
                    if ( first + 2u >= surface._listIndex.size() )
                        break;
                    const float3 arrCorner[] = { surface._listPosition[surface._listIndex[first]], surface._listPosition[surface._listIndex[first + 1u]],
                                                 surface._listPosition[surface._listIndex[first + 2u]] };
                    for ( uint32 edge = 0; edge < 3; ++edge )
                    {
                        EditorVisualizerLine line = highlight;
                        line._from                = arrCorner[edge];
                        line._to                  = arrCorner[( edge + 1u ) % 3u];
                        _previewState._listPickLine.push_back( line );
                    }
                }
            }
        }
        if ( _bBodyPickShown == SW_TRUE )
            Internal::addCross( _previewState._listPickLine, _bodyPickMarker, Internal::kPickMarkLength, highlight );
    }

    void CharacterEditorPanel::pickBodySurface( const Ray& ray )
    {
        using Internal          = CharacterEditorPanelPreviewInternal;
        const GameObject* pBody = resolvePreviewBody();
        if ( pBody == nullptr )
            return;
        vector<const GameObject*> listObject;
        collectPickObjects( *pBody, listObject );
        vector<BodyPickSurface> listSurface;
        Internal::buildPickSurfaces( listObject, listSurface );
        if ( listSurface.empty() || listSurface[0]._bSkin == SW_FALSE )
            return; // 몸 메시가 아직 없다(조립 전)
        // 층 배치는 피부(몸 표면)만 본다 — 옷 아래 피부에도 놓는다(층 UV 는 몸 UV 다). 구간 고르기는 부품까지 가장 가까운 것.
        const bool                                    bPlacingLayer = _bodyPickLayer != constant::invalid_index::kUint32;
        const vector_reference<const BodyPickSurface> listCandidate = bPlacingLayer ? vector_reference<const BodyPickSurface>{ listSurface.data(), 1 } : vector_reference<const BodyPickSurface>{ listSurface };
        BodySurfaceHit                                hit{};
        if ( BodySurfaceHit::pick( listCandidate, ray._origin, ray._direction, hit ) == false )
            return;
        _bodyPickMarker = hit._position;
        _bBodyPickShown = SW_TRUE;
        ++_published._bodyPickCount; // 층 배치 · 구간 고르기 모두 같은 집기다
        if ( bPlacingLayer )
        {
            // 배치 — 끄는 동안 같은 층의 가운데를 맞은 UV 로 옮긴다(한 되돌리기 단계).
            (void)BodyCustomizationViewModel::placeSkinLayer( _document.getAppearanceModel(), _bodyPickLayer, hit._uv, true );
            _bBodyPickDrag = SW_TRUE;
            return;
        }
        const BodyPickSurface& surface = listSurface[hit._surface];
        if ( hit._section.has_value() && *hit._section < surface._listSection.size() )
            _bodySelectedSection = BodyCustomizationViewModel::findSectionRow( _bodyState, surface._listSection[*hit._section]._name );
    }
} // namespace sw::editor
