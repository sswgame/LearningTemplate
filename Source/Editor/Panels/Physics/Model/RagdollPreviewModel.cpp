#include "pch.h"

#include "Editor/Panels/Physics/Model/RagdollPreviewModel.h"

#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/FileSystem/AssetID.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
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
    SW_LOG_CALLER( "RagdollPreviewPanel" );

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
        PhysicsAsset asset;
        if ( asset.loadFromAsset( AssetID{ assetPath } ) == false )
        {
            _status = "The physics asset could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
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
        const int32 impulseBody = _selectedBody >= 0 ? _selectedBody : 0;
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
            pManager->destroyObject( pOld );
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
        SW_LOG_INFO( "Ragdoll Preview hangs '%#' on '%#'", assetPath, _activePrefabPath );
    }
} // namespace sw::editor
