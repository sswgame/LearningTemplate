#include "pch.h"

#include "GameFramework/Base/World/PropScatterComponent.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct PropScatterComponentInternal
        {

            /** @brief 자리 하나에 모델 · 크기 · 요를 정해 목록에 넣습니다. 제외 원 안이면 모델만 꺼내고 넣지 않는다. */
            static void plant( const PropScatterParams& params, const float3& position, uint32& inoutState, vector<PropScatterPlacement>& outListPlacement )
            {
                const float32 pick = PropScatterMath::nextUnit( inoutState );
                for ( const PropScatterExclusion& exclusion : params._listExclusion )
                {
                    const float32 deltaX = position._x - exclusion._center._x;
                    const float32 deltaZ = position._z - exclusion._center._z;
                    if ( deltaX * deltaX + deltaZ * deltaZ < exclusion._radius * exclusion._radius )
                        return;
                }
                PropScatterPlacement placement;
                placement._position   = position;
                placement._modelIndex = PropScatterMath::pickModel( params._listModel, pick );
                placement._scale      = params._scaleMin + PropScatterMath::nextUnit( inoutState ) * ( params._scaleMax - params._scaleMin );
                placement._yaw        = PropScatterMath::nextUnit( inoutState ) * MathUtil::kTwoPi;
                if ( placement._modelIndex >= 0 )
                    outListPlacement.push_back( placement );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 PropScatterMath::nextUnit( uint32& inoutState )
    {
        if ( inoutState == 0u )
            inoutState = 1u;
        inoutState ^= inoutState << 13;
        inoutState ^= inoutState >> 17;
        inoutState ^= inoutState << 5;
        return static_cast<float32>( inoutState & 0xFFFFu ) / 65535.0f;
    }

    int32 PropScatterMath::pickModel( const vector<PropScatterModel>& listModel, float32 unit )
    {
        float32 totalWeight = 0.0f;
        for ( const PropScatterModel& model : listModel )
            totalWeight += model._weight > 0.0f ? model._weight : 0.0f;
        if ( totalWeight <= 0.0f )
            return -1;
        // 누적 비중의 경계보다 작으면 그 모델 — 비중 0.45 · 0.35 · 0.2 면 0.45 · 0.8 이 경계다.
        float32 cumulative = 0.0f;
        int32   lastIndex  = -1;
        for ( int32 modelIndex = 0; modelIndex < static_cast<int32>( listModel.size() ); ++modelIndex )
        {
            const float32 weight = listModel[static_cast<size_t>( modelIndex )]._weight;
            if ( weight <= 0.0f )
                continue;
            cumulative += weight;
            lastIndex = modelIndex;
            if ( unit < cumulative / totalWeight )
                return modelIndex;
        }
        return lastIndex;
    }

    void PropScatterMath::computePlacements( const PropScatterParams& params, vector<PropScatterPlacement>& outListPlacement )
    {
        outListPlacement.clear();
        if ( params._mode == PropScatterMode::Rules )
        {
            computeRulePlacements( params, nullptr, outListPlacement );
            return;
        }
        if ( params._spacing <= 0.0f || params._listModel.empty() )
            return;
        using Internal         = PropScatterComponentInternal;
        uint32       state     = params._seed;
        const float3 regionMin = params._regionMin;
        const float3 regionMax = params._regionMax;
        if ( params._mode == PropScatterMode::Fill )
        {
            for ( float32 z = regionMin._z; z <= regionMax._z; z += params._spacing )
            {
                for ( float32 x = regionMin._x; x <= regionMax._x; x += params._spacing )
                {
                    const float32 offsetX = PropScatterMath::nextUnit( state ) * params._inwardJitter;
                    const float32 offsetZ = PropScatterMath::nextUnit( state ) * params._inwardJitter;
                    Internal::plant( params, float3{ x + offsetX, regionMin._y, z + offsetZ }, state, outListPlacement );
                }
            }
            return;
        }
        // 가장자리 — 아래 · 위 변을 x 로 걷고, 왼쪽 · 오른쪽 변을 z 로 걷는다(모서리는 아래 · 위 변이 맡는다). 안쪽으로만 흔든다.
        for ( float32 x = regionMin._x; x <= regionMax._x; x += params._spacing )
        {
            const float32 bottomX = x + PropScatterMath::nextUnit( state ) * params._alongJitter;
            const float32 bottomZ = regionMin._z + PropScatterMath::nextUnit( state ) * params._inwardJitter;
            Internal::plant( params, float3{ bottomX, regionMin._y, bottomZ }, state, outListPlacement );
            const float32 topX = x + PropScatterMath::nextUnit( state ) * params._alongJitter;
            const float32 topZ = regionMax._z - PropScatterMath::nextUnit( state ) * params._inwardJitter;
            Internal::plant( params, float3{ topX, regionMin._y, topZ }, state, outListPlacement );
        }
        for ( float32 z = regionMin._z + params._spacing; z < regionMax._z; z += params._spacing )
        {
            const float32 leftX = regionMin._x + PropScatterMath::nextUnit( state ) * params._inwardJitter;
            const float32 leftZ = z + PropScatterMath::nextUnit( state ) * params._alongJitter;
            Internal::plant( params, float3{ leftX, regionMin._y, leftZ }, state, outListPlacement );
            const float32 rightX = regionMax._x - PropScatterMath::nextUnit( state ) * params._inwardJitter;
            const float32 rightZ = z + PropScatterMath::nextUnit( state ) * params._alongJitter;
            Internal::plant( params, float3{ rightX, regionMin._y, rightZ }, state, outListPlacement );
        }
    }

    void PropScatterMath::computeRulePlacements( const PropScatterParams& params, const IPlacementSurface* pSurface, vector<PropScatterPlacement>& outListPlacement )
    {
        outListPlacement.clear();
        if ( params._listModel.empty() )
            return;
        vector<float32> listWeight;
        listWeight.reserve( params._listModel.size() );
        for ( const PropScatterModel& model : params._listModel )
            listWeight.push_back( model._weight );
        vector<PlacementExclusion> listExclusion;
        listExclusion.reserve( params._listExclusion.size() );
        for ( const PropScatterExclusion& exclusion : params._listExclusion )
        {
            PlacementExclusion circle;
            circle._center = float2{ exclusion._center._x, exclusion._center._z };
            circle._radius = exclusion._radius;
            listExclusion.push_back( circle );
        }
        PlacementRegion region;
        region._min = float2{ params._regionMin._x, params._regionMin._z };
        region._max = float2{ params._regionMax._x, params._regionMax._z };
        vector<PlacementInstance> listInstance;
        PlacementScatter::scatter( params._rule, region, pSurface, listExclusion, listWeight, listInstance );
        outListPlacement.reserve( listInstance.size() );
        for ( const PlacementInstance& instance : listInstance )
        {
            PropScatterPlacement placement;
            // 표면이 없으면 높이 0 평면이다 — 영역의 y 에 얹는다.
            placement._position   = float3{ instance._planePosition._x, instance._height + ( pSurface == nullptr ? params._regionMin._y : 0.0f ), instance._planePosition._y };
            placement._scale      = instance._scale;
            placement._yaw        = instance._yaw;
            placement._modelIndex = instance._entryIndex;
            outListPlacement.push_back( placement );
        }
    }

    PropScatterComponent::PropScatterComponent()
        : _listModel{}
        , _listExclusion{}
        , _materialPath{}
        , _propName{ "ScatterProp" }
        , _regionMin{ -10.0f, 0.0f, -10.0f }
        , _regionMax{ 10.0f, 0.0f, 10.0f }
        , _spacing{ 5.0f }
        , _alongJitter{ 1.0f }
        , _inwardJitter{ 1.0f }
        , _scaleMin{ 1.0f }
        , _scaleMax{ 1.0f }
        , _seed{ HashUtil::kGoldenRatio32 }
        , _mode{ PropScatterMode::Edge }
        , _rule{}
        , _listSpawned{}
    {
    }

    void PropScatterComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 시작이 틱 안이어도(플레이 중에 붙었다) 틱 뒤에 세운다. 그 사이에 컴포넌트가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            PropScatterComponent* pScatter = static_cast<PropScatterComponent*>( pManager->resolveComponent( self ) );
            // 핫 리로드가 되살린 것이 있으면 그대로 쓴다.
            if ( pScatter != nullptr && pScatter->areSpawnedPropsAlive() == false )
                pScatter->spawnProps();
        } );
    }

    void PropScatterComponent::onEndPlay()
    {
        despawnProps();
        Component::onEndPlay();
    }

    PropScatterParams PropScatterComponent::makeParams() const
    {
        PropScatterParams params;
        params._listModel     = _listModel;
        params._listExclusion = _listExclusion;
        params._regionMin     = _regionMin;
        params._regionMax     = _regionMax;
        params._spacing       = _spacing;
        params._alongJitter   = _alongJitter;
        params._inwardJitter  = _inwardJitter;
        params._scaleMin      = _scaleMin;
        params._scaleMax      = _scaleMax;
        params._seed          = _seed;
        params._mode          = _mode;
        params._rule          = _rule;
        return params;
    }

    void PropScatterComponent::spawnProps()
    {
        despawnProps();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        vector<PropScatterPlacement> listPlacement;
        const PropScatterParams      params = makeParams();
        if ( params._mode == PropScatterMode::Rules )
        {
            // 규칙 모드는 영역 가운데 아래의 지형을 표면으로 읽는다(없으면 영역 y 의 평면).
            const float32           centerX  = 0.5f * ( params._regionMin._x + params._regionMax._x );
            const float32           centerZ  = 0.5f * ( params._regionMin._z + params._regionMax._z );
            const TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( *pManager, centerX, centerZ );
            if ( pTerrain != nullptr )
            {
                const TerrainPlacementSurface surface( pTerrain->getHeightfield() );
                PropScatterMath::computeRulePlacements( params, &surface, listPlacement );
            }
            else
            {
                PropScatterMath::computeRulePlacements( params, nullptr, listPlacement );
            }
        }
        else
        {
            PropScatterMath::computePlacements( params, listPlacement );
        }
        _listSpawned.reserve( listPlacement.size() );
        const hashed_string propName( _propName.c_str() );
        for ( const PropScatterPlacement& placement : listPlacement )
        {
            GameObject* pProp = pManager->createGameObject( propName );
            if ( pProp == nullptr )
                continue;
            MeshComponent* pMesh = pProp->addComponent<MeshComponent>();
            if ( pMesh == nullptr )
            {
                pManager->destroyObject( pProp );
                continue;
            }
            pMesh->setMeshId( _listModel[static_cast<size_t>( placement._modelIndex )]._meshId );
            if ( _materialPath.empty() == false )
                pMesh->setMaterialPath( _materialPath );
            pMesh->setLocalPosition( placement._position );
            pMesh->setLocalRotation( float3{ 0.0f, placement._yaw, 0.0f } );
            pMesh->setLocalScale( float3{ placement._scale, placement._scale, placement._scale } );
            _listSpawned.push_back( pProp->getHandle() );
        }
    }

    bool PropScatterComponent::areSpawnedPropsAlive() const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _listSpawned.empty() )
            return false;
        for ( const GameObjectHandle& handle : _listSpawned )
        {
            if ( pManager->resolveGameObject( handle ) == nullptr )
                return false;
        }
        return true;
    }

    void PropScatterComponent::despawnProps()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
        {
            for ( const GameObjectHandle& handle : _listSpawned )
            {
                GameObject* pProp = pManager->resolveGameObject( handle );
                if ( pProp != nullptr )
                    pManager->destroyObject( pProp );
            }
        }
        _listSpawned.clear();
    }
} // namespace sw
