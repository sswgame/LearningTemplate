#include "pch.h"

#include "Editor/Panels/Physics/Model/RagdollPreviewModel.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct RagdollPreviewModelInternal
        {
            /// @brief 충격의 방향입니다(월드, 길이 1). 옆으로 밀면서 조금 띄운다.
            static constexpr float3 kImpulseDirection{ 0.8f, 0.3f, 0.52f };
            /// @brief 바닥 바디의 반 크기(미터)입니다. 위 면이 y = 0 이다.
            static constexpr float3 kFloorHalfExtents{ 3.0f, 0.5f, 3.0f };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    RagdollPreviewModel::RagdollPreviewModel()
        : _listBodyRow{}
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

    ToolAssetLoadResult RagdollPreviewModel::loadAsset( string_view assetPath )
    {
        _listBodyRow.clear();
        _selectedBody = -1;
        // 미리 보기 표는 래그돌과 같은 공유 캐시의 한 벌을 읽는다(사본을 읽지 않는다).
        const shared_ptr<const PhysicsAsset> pAsset = SharedAssetCache<PhysicsAsset>::acquire( assetPath );
        if ( pAsset == nullptr )
        {
            _status = "The physics asset could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        const PhysicsAsset& asset = *pAsset;
        for ( const PhysicsAssetBodyDef& body : asset._listBody )
        {
            RagdollPreviewBodyRow& row = _listBodyRow.emplace_back();
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
        _status = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void RagdollPreviewModel::populateScene( Scene& scene )
    {
        GameObjectManager*  pManager = scene.getObjectManager();
        GameObject*         pFloor   = pManager != nullptr ? pManager->createGameObject( hashed_string( "PreviewFloorBody" ) ) : nullptr;
        RigidBodyComponent* pBody    = pFloor != nullptr ? pFloor->addComponent<RigidBodyComponent>() : nullptr;
        if ( pBody != nullptr )
        {
            // 보이는 바닥(환경)과 같은 높이의 정적 바디다 — 몸이 그 위에 쓰러진다.
            PhysicsShapeDesc3D floorBox;
            floorBox._halfExtents   = RagdollPreviewModelInternal::kFloorHalfExtents;
            floorBox._localPosition = float3{ 0.0f, -RagdollPreviewModelInternal::kFloorHalfExtents._y, 0.0f };
            pBody->setShape( floorBox );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLayer( hashed_string( "Static" ) );
            pBody->setMaterial( hashed_string( "Stone" ) );
        }
        _spawnedPrefabPath.clear(); // 새 씬에는 몸이 없다
        _body = GameObjectHandle{};
    }

    void RagdollPreviewModel::spawnFromField()
    {
        _activePrefabPath = _bodyPrefabPath;
        _spawnedPrefabPath.clear(); // 다음 그리기가 몸을 다시 놓는다
    }

    RagdollComponent* RagdollPreviewModel::findRagdoll( Scene* pScene ) const
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pBody    = pManager != nullptr ? pManager->resolveGameObject( _body ) : nullptr;
        return pBody != nullptr ? pBody->getComponent<RagdollComponent>() : nullptr;
    }

    void RagdollPreviewModel::applyImpulse( RagdollComponent& ragdoll ) const
    {
        const uint32 impulseBody = _selectedBody >= 0 ? static_cast<uint32>( _selectedBody ) : 0u;
        ragdoll.startRagdoll( RagdollPreviewModelInternal::kImpulseDirection * _impulse, impulseBody );
    }

    void RagdollPreviewModel::syncBody( Scene* pScene, string_view assetPath )
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
            _status = string( "Prefab '" ) + _activePrefabPath + "' could not be spawned - see the log";
            return;
        }
        _body = pSpawned->getHandle();
        if ( pSpawned->getComponent<SkeletalMeshComponent>() == nullptr )
        {
            _status = string( "Prefab '" ) + _activePrefabPath + "' has no skeletal mesh on its root - the ragdoll cannot stand";
            return;
        }
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

    void RagdollPreviewModel::gatherRagdoll( Scene* pScene, RagdollPreviewState& outState ) const
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

    void RagdollPreviewModel::startRagdoll( Scene* pScene ) const
    {
        RagdollComponent* const pRagdoll = findRagdoll( pScene );
        if ( pRagdoll != nullptr )
            pRagdoll->startRagdoll( float3{}, nullopt );
    }

    void RagdollPreviewModel::applyImpulse( Scene* pScene ) const
    {
        RagdollComponent* const pRagdoll = findRagdoll( pScene );
        if ( pRagdoll != nullptr )
            applyImpulse( *pRagdoll );
    }

    void RagdollPreviewModel::startPartialRagdoll( Scene* pScene ) const
    {
        RagdollComponent* const pRagdoll = findRagdoll( pScene );
        if ( pRagdoll != nullptr )
            pRagdoll->startPartialRagdoll( 1.0f );
    }

    void RagdollPreviewModel::stopPartialRagdoll( Scene* pScene ) const
    {
        RagdollComponent* const pRagdoll = findRagdoll( pScene );
        if ( pRagdoll != nullptr )
            pRagdoll->stopPartialRagdoll();
    }

    void RagdollPreviewModel::drawOverlay( Scene* pScene, IPhysicsDebugRenderer& renderer, const RagdollPreviewOverlayColors& colors ) const
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
