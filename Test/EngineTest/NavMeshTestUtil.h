/**
 * @file NavMeshTestUtil.h
 * @brief 내비메시 시험들이 같이 쓰는 도우미 — 시험용 설정 표, 상자 기하, 베이크입니다.
 */
#pragma once
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace navtest
{
    /** @brief 반지름 0.4 · 높이 1.8 · 계단 0.45 · 경사 45 도 · 셀 0.2 · 타일 32 셀(6.4 m) 의 종류 하나와 영역 넷(Default · Grass · Mud · Water). */
    inline sw::NavMeshSettings makeSettings()
    {
        sw::NavMeshSettings settings;
        sw::NavAgentTypeDef agentType;
        agentType._name       = sw::hashed_string( "TestHumanoid" );
        agentType._radius     = 0.4f;
        agentType._height     = 1.8f;
        agentType._maxClimb   = 0.45f;
        agentType._maxSlope   = 0.785398f;
        agentType._cellSize   = 0.2f;
        agentType._cellHeight = 0.1f;
        agentType._tileSize   = 32;
        settings._listAgentType.push_back( agentType );
        const utf8*   arrAreaName[4] = { "Default", "Grass", "Mud", "Water" };
        const float32 arrAreaCost[4] = { 1.0f, 1.5f, 4.0f, 10.0f };
        for ( uint32 areaIndex = 0; areaIndex < 4; ++areaIndex )
        {
            sw::NavAreaDef area;
            area._name = sw::hashed_string( arrAreaName[areaIndex] );
            area._cost = arrAreaCost[areaIndex];
            settings._listArea.push_back( area );
        }
        return settings;
    }

    /** @brief 축 상자 하나를 [min, max] 로 더합니다. */
    inline void addBox( sw::NavMeshGeometry& geometry, const sw::float3& minPoint, const sw::float3& maxPoint, uint8 area = 0 )
    {
        const sw::float3 center = ( minPoint + maxPoint ) * 0.5f;
        const sw::float3 half   = ( maxPoint - minPoint ) * 0.5f;
        geometry.addBox( half, sw::float4x4::createTrs( center, sw::float3{}, sw::float3{ 1.0f, 1.0f, 1.0f } ), area );
    }

    /** @brief 윗면이 y = 0 인 바닥 상자([x0, x1] × [z0, z1], 두께 0.2)입니다. */
    inline void addFloor( sw::NavMeshGeometry& geometry, float32 x0, float32 z0, float32 x1, float32 z1, uint8 area = 0 )
    {
        addBox( geometry, sw::float3{ x0, -0.2f, z0 }, sw::float3{ x1, 0.0f, z1 }, area );
    }

    /** @brief 기하를 내비메시로 베이크합니다. 실패하면 nullptr 입니다. */
    inline sw::unique_ptr<sw::INavMesh> bake( const sw::NavMeshSettings& settings, sw::NavMeshGeometry& geometry,
                                              const sw::vector<sw::NavConvexVolume>* pExtraVolume = nullptr, sw::NavMeshBakeStats* pOutStats = nullptr )
    {
        sw::unique_ptr<sw::INavMesh> pNavMesh = sw::NavMeshBackend::createNavMesh();
        if ( pNavMesh->initialize( settings._listAgentType.front(), settings, sw::NavMeshBakeUtil::computeBakeBounds( geometry ) ) == false )
            return nullptr;
        if ( sw::NavMeshBakeUtil::bakeAllTiles( *pNavMesh, geometry, pExtraVolume, pOutStats ) == false )
            return nullptr;
        return pNavMesh;
    }

    /** @brief Static 강체 상자(반 크기 @p halfExtents)를 씬에 놓습니다. */
    inline sw::GameObject* spawnStaticBody( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& center, const sw::float3& halfExtents )
    {
        sw::GameObject*         pObject = manager.createGameObject( sw::hashed_string( pName ) );
        sw::RigidBodyComponent* pBody   = pObject->addComponent<sw::RigidBodyComponent>();
        sw::PhysicsShapeDesc3D  box;
        box._halfExtents = halfExtents;
        pBody->setShape( box );
        pBody->setBodyType( sw::PhysicsBodyType::Static );
        pBody->setLocalPosition( center );
        return pObject;
    }

    /** @brief 종류 "TestHumanoid" 를 @p source 기하로 베이크하는 내비메시 표면을 씬에 놓습니다. */
    inline sw::NavMeshSurfaceComponent* spawnSurface( sw::GameObjectManager& manager, sw::NavGeometrySource source )
    {
        sw::GameObject*              pObject  = manager.createGameObject( sw::hashed_string( "NavSurface" ) );
        sw::NavMeshSurfaceComponent* pSurface = pObject->addComponent<sw::NavMeshSurfaceComponent>();
        pSurface->setAgentTypes( { sw::hashed_string( "TestHumanoid" ) } );
        pSurface->setGeometrySource( source );
        return pSurface;
    }

    /**
     * @brief 강체 바닥(30 × 30, 윗면 y = 0) · 가운데 상자 벽(x −1..1, z −4..4)과 그것을 강체 기하로 베이크하는 표면입니다 — 에이전트가 벽을 돌아가는 장면.
     * @details 씬 내비게이션의 설정을 시험 표(`makeSettings`)로 바꿉니다.
     */
    inline void spawnPhysicsCrateScene( sw::GameObjectManager& manager )
    {
        manager.getSceneNavigation().setSettings( makeSettings() );
        spawnStaticBody( manager, "Floor", sw::float3{ 0.0f, -0.5f, 0.0f }, sw::float3{ 15.0f, 0.5f, 15.0f } );
        spawnStaticBody( manager, "Crates", sw::float3{ 0.0f, 1.0f, 0.0f }, sw::float3{ 1.0f, 1.0f, 4.0f } );
        spawnSurface( manager, sw::NavGeometrySource::PhysicsColliders );
    }

    /** @brief 가운데 상자 벽(x −1..1, z −4..4) 안이면 true 입니다. */
    inline bool isInsideCrates( const sw::float3& position )
    {
        return -1.0f < position._x && position._x < 1.0f && -4.0f < position._z && position._z < 4.0f;
    }

    /** @brief 찾는 범위(반지름 × 4, 높이)입니다. */
    inline sw::float3 makeExtent()
    {
        return sw::float3{ 1.6f, 1.8f, 1.6f };
    }
} // namespace navtest
