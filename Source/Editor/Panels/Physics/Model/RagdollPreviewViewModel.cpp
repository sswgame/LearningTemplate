#include "pch.h"

#include "Editor/Panels/Physics/Model/RagdollPreviewViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Asset/AssetWriter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Catalog/Model/ReflectedXMLDocument.h"
#include "Editor/Viewport/Model/EditorColliderHandleMath.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct RagdollPreviewViewModelInternal
        {
            /// @brief 에셋 문서의 바디 목록 칸과 바디의 열쇠 칸입니다(`PhysicsAsset::_listBody` · `PhysicsAssetBodyDef::_bone`).
            static constexpr string_view kBodyList = "_listBody";
            static constexpr string_view kBodyKey  = "_bone";
            /// @brief 바디 셰이프의 배율입니다 — 래그돌은 본의 배율을 버리고 세운다.
            static constexpr float3 kBodyScale{ 1.0f, 1.0f, 1.0f };

            static void fillRows( const PhysicsAsset& asset, vector<RagdollPreviewBodyRow>& outListRow )
            {
                outListRow.clear();
                for ( const PhysicsAssetBodyDef& body : asset._listBody )
                {
                    RagdollPreviewBodyRow& row = outListRow.emplace_back();
                    row._bone                  = body._bone.c_str();
                    row._hitZone               = body._hitZone._name.c_str();
                    row._mass                  = body._mass;
                    row._jointType             = body._joint._type;
                    for ( const PhysicsShapeDesc3D& shape : body._listShape )
                    {
                        if ( row._shapes.empty() == false )
                            row._shapes += ", ";
                        row._shapes += getEnumName( shape._type );
                    }
                }
            }

            /// @brief 충격의 방향입니다(월드, 길이 1). 옆으로 밀면서 조금 띄운다.
            static constexpr float3 kImpulseDirection{ 0.8f, 0.3f, 0.52f };
            /// @brief 바닥 바디의 반 크기(미터)입니다. 위 면이 y = 0 이다.
            static constexpr float3 kFloorHalfExtents{ 3.0f, 0.5f, 3.0f };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    RagdollPreviewViewModel::RagdollPreviewViewModel()
        : _pDocument{ make_unique<ReflectedXMLDocument>() }
        , _loadedDiskPath{}
        , _listBodyRow{}
        , _bodyPrefabPath{}
        , _activePrefabPath{}
        , _spawnedPrefabPath{}
        , _spawnedAssetPath{}
        , _status{}
        , _body{}
        , _impulse{ kDefaultImpulse }
        , _selectedBody{ -1 }
        , _bDrawSkeleton{ true }
        , _bDrawBodies{ true }
        , _bDrawJoints{ true }
    {
    }

    RagdollPreviewViewModel::~RagdollPreviewViewModel() = default;

    void RagdollPreviewViewModel::bindDocumentEditRecorder( const DocumentEditRecorder& recorder )
    {
        _pDocument->bindDocumentEditRecorder( recorder );
    }

    string RagdollPreviewViewModel::captureText() const
    {
        return _pDocument->getText();
    }

    void RagdollPreviewViewModel::applyText( string_view text )
    {
        _pDocument->applyText( text );
        refreshBodyRows();
    }

    bool RagdollPreviewViewModel::saveDocument( AssetWriter& writer )
    {
        if ( _loadedDiskPath.empty() || writer.writeText( _loadedDiskPath, _pDocument->getText() ) == false )
        {
            _status = "The physics asset could not be saved - see the log";
            return false;
        }
        _pDocument->markSaved();
        requestRespawn(); // 저장 뒤 공유 캐시가 다시 읽은 에셋으로 몸을 다시 놓는다
        _status = "Saved";
        return true;
    }

    bool RagdollPreviewViewModel::isEditable() const
    {
        return _pDocument->isEditable();
    }

    const string& RagdollPreviewViewModel::getBaseID() const
    {
        return _pDocument->getBaseID();
    }

    vector<hashed_string> RagdollPreviewViewModel::collectHiddenProperties() const
    {
        vector<hashed_string> listHidden;
        if ( _selectedBody >= 0 && _pDocument->isInheritedItem( RagdollPreviewViewModelInternal::kBodyList, static_cast<uint32>( _selectedBody ) ) )
            listHidden.push_back( hashed_string( RagdollPreviewViewModelInternal::kBodyKey ) );
        return listHidden;
    }

    void* RagdollPreviewViewModel::editSelectedBody()
    {
        PhysicsAsset* pAsset = static_cast<PhysicsAsset*>( _pDocument->getInstance() );
        return pAsset != nullptr && _selectedBody >= 0 && static_cast<size_t>( _selectedBody ) < pAsset->_listBody.size() ? &pAsset->_listBody[static_cast<size_t>( _selectedBody )] : nullptr;
    }

    const TypeInfo* RagdollPreviewViewModel::getBodyType()
    {
        return PhysicsAssetBodyDef::StaticType();
    }

    void RagdollPreviewViewModel::commitBodyProperty( const PropertyInfo& property )
    {
        const void* pBody = editSelectedBody();
        if ( pBody == nullptr )
            return;
        (void)_pDocument->commitItemProperty( RagdollPreviewViewModelInternal::kBodyList, static_cast<uint32>( _selectedBody ), pBody, *getBodyType(), property ); // 같은 글이면 바뀐 것이 없다
        refreshBodyRows();
    }

    void RagdollPreviewViewModel::duplicateSelectedBody()
    {
        if ( editSelectedBody() == nullptr ||
             _pDocument->duplicateItem( RagdollPreviewViewModelInternal::kBodyList, static_cast<uint32>( _selectedBody ), RagdollPreviewViewModelInternal::kBodyKey ) == false )
            return;
        ++_selectedBody;
        refreshBodyRows();
    }

    void RagdollPreviewViewModel::removeSelectedBody()
    {
        if ( editSelectedBody() == nullptr || _listBodyRow.size() < 2 )
            return;
        if ( _pDocument->removeItem( RagdollPreviewViewModelInternal::kBodyList, static_cast<uint32>( _selectedBody ) ) == false )
            return;
        _selectedBody = kNoBody;
        refreshBodyRows();
    }

    void RagdollPreviewViewModel::refreshBodyRows()
    {
        const PhysicsAsset* pAsset = static_cast<const PhysicsAsset*>( _pDocument->getInstance() );
        if ( pAsset != nullptr )
            RagdollPreviewViewModelInternal::fillRows( *pAsset, _listBodyRow );
    }

    bool RagdollPreviewViewModel::findSelectedBodyPose( Scene* pScene, float3& outPosition, quaternion& outRotation )
    {
        const PhysicsAssetBodyDef* pBody    = static_cast<const PhysicsAssetBodyDef*>( editSelectedBody() );
        const RagdollComponent*    pRagdoll = findRagdoll( pScene );
        GameObject*                pOwner   = pRagdoll != nullptr ? pRagdoll->getOwner() : nullptr;
        SkeletalMeshComponent*     pUnit    = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pBody == nullptr || pUnit == nullptr )
            return false;
        const optional<uint32>  boneIndex = pUnit->getSkeleton().findBoneIndex( pBody->_bone );
        const vector<float4x4>& listModel = pUnit->getModelSpaceTransforms();
        if ( boneIndex.has_value() == false || *boneIndex >= listModel.size() )
            return false;
        // 래그돌이 바디를 놓는 식과 같다 — 본의 월드 행렬에서 위치 · 회전만 쓰고 배율은 버린다.
        const float4x4 world = listModel[*boneIndex] * pUnit->getWorldMatrix();
        float3         scale{};
        return world.decompose( scale, outRotation, outPosition );
    }

    void RagdollPreviewViewModel::gatherShapeHandles( Scene* pScene, vector<ColliderHandle>& outListHandle )
    {
        outListHandle.clear();
        float3     position{};
        quaternion rotation{};
        if ( isEditable() == false || findSelectedBodyPose( pScene, position, rotation ) == false )
            return;
        const vector<PhysicsShapeDesc3D>& listShape = static_cast<const PhysicsAssetBodyDef*>( editSelectedBody() )->_listShape;
        for ( uint32 shapeIndex = 0; shapeIndex < static_cast<uint32>( listShape.size() ); ++shapeIndex )
        {
            EditorColliderHandleMath::collectHandles( listShape[shapeIndex], shapeIndex, position, rotation, RagdollPreviewViewModelInternal::kBodyScale, outListHandle );
        }
    }

    void RagdollPreviewViewModel::dragShapeHandle( const ColliderHandle& handle, const Ray& ray )
    {
        PhysicsAssetBodyDef* pBody = static_cast<PhysicsAssetBodyDef*>( editSelectedBody() );
        if ( pBody == nullptr || handle._shapeIndex >= pBody->_listShape.size() )
            return;
        PhysicsShapeDesc3D& shape    = pBody->_listShape[handle._shapeIndex];
        const float32       distance = Ray::computeClosestAxisParameter( ray, handle._center, handle._direction );
        shape                        = EditorColliderHandleMath::applyDrag( shape, handle, distance, RagdollPreviewViewModelInternal::kBodyScale );
        refreshBodyRows();
    }

    void RagdollPreviewViewModel::commitShapeDrag()
    {
        static const hashed_string s_shapeList( "_listShape" );
        const PropertyInfo*        pProp = getBodyType()->findPropertyInHierarchy( s_shapeList );
        if ( pProp != nullptr )
            commitBodyProperty( *pProp );
    }

    ToolAssetLoadResult RagdollPreviewViewModel::loadAsset( string_view assetPath )
    {
        _listBodyRow.clear();
        _selectedBody = -1;
        // 미리 보기 표는 래그돌과 같은 공유 캐시의 한 벌을 읽는다(사본을 읽지 않는다).
        const shared_ptr<const PhysicsAsset> pAsset = SharedAssetCache<PhysicsAsset>::acquire( assetPath ).snapshot();
        if ( pAsset == nullptr )
        {
            _status = "The physics asset could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        RagdollPreviewViewModelInternal::fillRows( *pAsset, _listBodyRow );
        // 편집 사본 — 읽지 못하면 공유 캐시가 이미 거절했다.
        const optional<DiskPath> diskPath = AbsolutePath::isStrictAbsolute( assetPath ) ? optional<DiskPath>{ FileUtil::toDiskPath( assetPath ) }
                                                                                        : editor::getSubsystem<AssetPaths>().toDiskPath( assetPath );
        _loadedDiskPath                   = diskPath.has_value() ? *diskPath : DiskPath{};
        _pDocument->clear();
        string text;
        if ( diskPath.has_value() && FileUtil::readTextFile( *diskPath, text ) )
        {
            const expected<void> opened = _pDocument->load( *PhysicsAsset::StaticType(), text, assetPath );
            if ( opened.has_value() == false )
                SW_LOG_WARNING_ID( "RagdollPreview.EditCopyNotRead", "Ragdoll Preview cannot edit '%#': %# (%#)", assetPath, opened.error().message(), opened.error().detail() );
        }
        _status = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void RagdollPreviewViewModel::populateScene( Scene& scene )
    {
        GameObjectManager*  pManager = scene.getObjectManager();
        GameObject*         pFloor   = pManager != nullptr ? pManager->createGameObject( hashed_string( "PreviewFloorBody" ) ) : nullptr;
        RigidBodyComponent* pBody    = pFloor != nullptr ? pFloor->addComponent<RigidBodyComponent>() : nullptr;
        if ( pBody != nullptr )
        {
            // 보이는 바닥(환경)과 같은 높이의 정적 바디다 — 몸이 그 위에 쓰러진다.
            PhysicsShapeDesc3D floorBox;
            floorBox._halfExtents   = RagdollPreviewViewModelInternal::kFloorHalfExtents;
            floorBox._localPosition = float3{ 0.0f, -RagdollPreviewViewModelInternal::kFloorHalfExtents._y, 0.0f };
            pBody->setShape( floorBox );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLayer( hashed_string( "Static" ) );
            pBody->setMaterial( hashed_string( "Stone" ) );
        }
        _spawnedPrefabPath.clear(); // 새 씬에는 몸이 없다
        _body = GameObjectHandle{};
    }

    void RagdollPreviewViewModel::spawnFromField()
    {
        _activePrefabPath = _bodyPrefabPath;
        _spawnedPrefabPath.clear(); // 다음 그리기가 몸을 다시 놓는다
    }

    RagdollComponent* RagdollPreviewViewModel::findRagdoll( Scene* pScene ) const
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pBody    = pManager != nullptr ? pManager->resolveGameObject( _body ) : nullptr;
        return pBody != nullptr ? pBody->getComponent<RagdollComponent>() : nullptr;
    }

    void RagdollPreviewViewModel::applyImpulse( RagdollComponent& ragdoll ) const
    {
        const uint32 impulseBody = _selectedBody >= 0 ? static_cast<uint32>( _selectedBody ) : 0u;
        ragdoll.startRagdoll( RagdollPreviewViewModelInternal::kImpulseDirection * _impulse, impulseBody );
    }

    void RagdollPreviewViewModel::syncBody( Scene* pScene, string_view assetPath )
    {
        if ( pScene == nullptr || _activePrefabPath.empty() )
            return;
        GameObjectManager* pManager = pScene->getObjectManager();
        if ( pManager == nullptr )
            return;
        if ( _spawnedPrefabPath == _activePrefabPath && string_view{ _spawnedAssetPath } == assetPath )
            return;

        GameObject* pOld = pManager->resolveGameObject( _body );
        if ( pOld != nullptr )
            pManager->destroyGameObject( pOld );
        _body                  = GameObjectHandle{};
        _spawnedPrefabPath     = _activePrefabPath; // 실패해도 같은 경로로 매 프레임 다시 놓지 않는다(경로를 고치고 Spawn 을 다시 누른다)
        _spawnedAssetPath      = assetPath;
        AssetManager* pAssets  = pManager->findAssetManager();
        GameObject*   pSpawned = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( pManager, _activePrefabPath ) : nullptr;
        if ( pSpawned == nullptr )
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder;
            statusBuilder.appendFormat( "Prefab '%#' could not be spawned - see the log", _activePrefabPath );
            _status = string( statusBuilder.view() );
            return;
        }
        _body                            = pSpawned->getHandle();
        SkeletalMeshComponent* pBodyMesh = pSpawned->getComponent<SkeletalMeshComponent>();
        if ( pBodyMesh == nullptr )
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder;
            statusBuilder.appendFormat( "Prefab '%#' has no skeletal mesh on its root - the ragdoll cannot stand", _activePrefabPath );
            _status = string( statusBuilder.view() );
            return;
        }
        // 래그돌 히트 존은 화면 밖에서도 본을 요구한다(`requireBonesWhenNotRendered`) — 미리보기 몸은 고른 프리팹과 상관없이 맞춘다.
        pBodyMesh->setVisibilityBasedTickOption( VisibilityBasedAnimationTickOption::AlwaysTickPoseAndRefreshBones );
        RagdollComponent* pRagdoll = pSpawned->getComponent<RagdollComponent>();
        if ( pRagdoll == nullptr )
            pRagdoll = pSpawned->addComponent<RagdollComponent>();
        if ( pRagdoll == nullptr )
        {
            _status = "The ragdoll could not be added to the body - see the log";
            return;
        }
        pRagdoll->setPhysicsAssetPath( assetPath ); // 미리보는 것은 열린 에셋이다(프리팹이 건 경로가 아니라)
        _status = "Body placed - bodies stand once the scene ticks (a mismatch between the asset's bones and the skeleton is logged)";
        SW_LOG_INFO_ID( "RagdollPreviewPanel.RagdollPreviewHangs", "Ragdoll Preview hangs '%#' on '%#'", assetPath, _activePrefabPath );
    }

    void RagdollPreviewViewModel::gatherRagdoll( Scene* pScene, RagdollPreviewState& outState ) const
    {
        outState._listBuiltBody.clear();
        outState._pStateName                   = "";
        outState._state                        = 0;
        outState._bAsleep                      = SW_FALSE;
        const RagdollComponent* const pRagdoll = findRagdoll( pScene );
        outState._bHasRagdoll                  = pRagdoll != nullptr ? SW_TRUE : SW_FALSE;
        if ( pRagdoll == nullptr )
            return;
        outState._pStateName    = getEnumName( pRagdoll->getState() );
        outState._state         = static_cast<uint32>( pRagdoll->getState() );
        outState._bAsleep       = pRagdoll->isAsleep() ? SW_TRUE : SW_FALSE;
        const size_t builtCount = pRagdoll->getRagdoll()._listBody.size();
        for ( size_t bodyIndex = 0; bodyIndex < builtCount; ++bodyIndex )
        {
            RagdollPreviewBuiltBody& body = outState._listBuiltBody.emplace_back();
            body._weight                  = pRagdoll->getBodyWeight( static_cast<uint32>( bodyIndex ) );
            body._bDynamic                = pRagdoll->isBodyDynamic( static_cast<uint32>( bodyIndex ) ) ? SW_TRUE : SW_FALSE;
        }
    }

    void RagdollPreviewViewModel::startRagdoll( Scene* pScene ) const
    {
        RagdollComponent* const pRagdoll = findRagdoll( pScene );
        if ( pRagdoll != nullptr )
            pRagdoll->startRagdoll( float3{}, nullopt );
    }

    void RagdollPreviewViewModel::applyImpulse( Scene* pScene ) const
    {
        RagdollComponent* const pRagdoll = findRagdoll( pScene );
        if ( pRagdoll != nullptr )
            applyImpulse( *pRagdoll );
    }

    void RagdollPreviewViewModel::startPartialRagdoll( Scene* pScene ) const
    {
        RagdollComponent* const pRagdoll = findRagdoll( pScene );
        if ( pRagdoll != nullptr )
            pRagdoll->startPartialRagdoll( 1.0f );
    }

    void RagdollPreviewViewModel::stopPartialRagdoll( Scene* pScene ) const
    {
        RagdollComponent* const pRagdoll = findRagdoll( pScene );
        if ( pRagdoll != nullptr )
            pRagdoll->stopPartialRagdoll();
    }

    void RagdollPreviewViewModel::drawOverlay( Scene* pScene, IPhysicsDebugRenderer& renderer, const RagdollPreviewOverlayColors& colors )
    {
        constexpr float32             kJointMarkRadius = 0.03f; // 관절 표시 십자의 반지름(m)
        const RagdollComponent* const pRagdoll         = findRagdoll( pScene );
        if ( pRagdoll == nullptr || pScene->getObjectManager() == nullptr )
            return;
        GameObject* const            pOwner        = pRagdoll->getOwner();
        SkeletalMeshComponent* const pUnit         = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        IPhysicsScene3D* const       pPhysicsScene = pScene->getObjectManager()->getSubsystem<ScenePhysics>().findScene3D();

        if ( _bDrawSkeleton && pUnit != nullptr )
        {
            const vector<float4x4>& listModel  = pUnit->getModelSpaceTransforms();
            const vector<int32>&    listParent = pUnit->getSkeleton().getParentIndices();
            const float4x4          world      = pUnit->getWorldMatrix();
            const size_t            boneCount  = listModel.size() < listParent.size() ? listModel.size() : listParent.size();
            for ( size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                const int32 parentIndex = listParent[boneIndex];
                if ( parentIndex < 0 || static_cast<size_t>( parentIndex ) >= boneCount )
                    continue;
                renderer.drawLine( float3::transform( listModel[static_cast<size_t>( parentIndex )].getTranslation(), world ), float3::transform( listModel[boneIndex].getTranslation(), world ), colors._bone );
            }
        }

        // 고른 바디의 편집 중인 셰이프 — 저장하기 전에도 고친 크기가 보인다(선 바디는 저장한 에셋이다).
        float3     editPosition{};
        quaternion editRotation{};
        if ( isEditable() && findSelectedBodyPose( pScene, editPosition, editRotation ) )
        {
            for ( const PhysicsShapeDesc3D& shape : static_cast<const PhysicsAssetBodyDef*>( editSelectedBody() )->_listShape )
            {
                IPhysicsDebugRenderer::drawShape3D( renderer, shape, editPosition, editRotation, colors._selectedJoint );
            }
        }

        if ( pPhysicsScene == nullptr )
            return;
        const PhysicsRagdoll& ragdoll = pRagdoll->getRagdoll();
        for ( size_t bodyIndex = 0; bodyIndex < ragdoll._listBody.size(); ++bodyIndex )
        {
            if ( pPhysicsScene->isBodyValid( ragdoll._listBody[bodyIndex] ) == false )
                continue;
            if ( _bDrawBodies )
                (void)pPhysicsScene->drawBodyDebug( ragdoll._listBody[bodyIndex], renderer );

            float3     position{};
            quaternion rotation{};
            if ( _bDrawJoints == false || pPhysicsScene->getBodyTransform( ragdoll._listBody[bodyIndex], position, rotation ) == false )
                continue;
            const float4& color = _selectedBody == static_cast<int32>( bodyIndex ) ? colors._selectedJoint : colors._joint;
            renderer.drawLine( position - float3{ kJointMarkRadius, 0.0f, 0.0f }, position + float3{ kJointMarkRadius, 0.0f, 0.0f }, color );
            renderer.drawLine( position - float3{ 0.0f, kJointMarkRadius, 0.0f }, position + float3{ 0.0f, kJointMarkRadius, 0.0f }, color );
            renderer.drawLine( position - float3{ 0.0f, 0.0f, kJointMarkRadius }, position + float3{ 0.0f, 0.0f, kJointMarkRadius }, color );
            const optional<uint32> parentBody = bodyIndex < ragdoll._listParentBody.size() ? ragdoll._listParentBody[bodyIndex] : nullopt;
            if ( parentBody.has_value() == false || *parentBody >= ragdoll._listBody.size() )
                continue;
            float3     parentPosition{};
            quaternion parentRotation{};
            if ( pPhysicsScene->getBodyTransform( ragdoll._listBody[*parentBody], parentPosition, parentRotation ) )
                renderer.drawLine( parentPosition, position, color );
        }
    }
} // namespace sw::editor
