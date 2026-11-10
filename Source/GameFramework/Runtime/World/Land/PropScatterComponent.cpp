#include "pch.h"

#include "GameFramework/Runtime/World/Land/PropScatterComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/Component/MeshSectionComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 자리 하나에 모델 · 크기 · 요를 정해 목록에 넣습니다. 제외 원 안이면 모델만 꺼내고 넣지 않는다. */
        void plant( const PropScatterParams& params, const float3& position, DeterministicRandom32& inoutRandom, vector<PropScatterPlacement>& outListPlacement )
        {
            const float32 pick = inoutRandom.nextFloat();
            for ( const PropScatterExclusion& exclusion : params._listExclusion )
            {
                const float32 deltaX = position._x - exclusion._center._x;
                const float32 deltaZ = position._z - exclusion._center._z;
                if ( deltaX * deltaX + deltaZ * deltaZ < exclusion._radius * exclusion._radius )
                    return;
            }
            const optional<int32> modelIndex = PropScatterPlacement::pickModel( params._listModel, pick );
            PropScatterPlacement  placement;
            placement._position = position;
            placement._scale    = params._scaleMin + inoutRandom.nextFloat() * ( params._scaleMax - params._scaleMin );
            placement._yaw      = inoutRandom.nextFloat() * MathUtil::kTwoPi;
            if ( modelIndex.has_value() == false )
                return;
            placement._modelIndex = *modelIndex;
            outListPlacement.push_back( placement );
        }
    } // namespace
} // namespace sw

namespace sw
{
    optional<int32> PropScatterPlacement::pickModel( const vector<PropScatterModel>& listModel, float32 unit )
    {
        float32 totalWeight = 0.0f;
        for ( const PropScatterModel& model : listModel )
        {
            totalWeight += model._weight > 0.0f ? model._weight : 0.0f;
        }
        if ( totalWeight <= 0.0f )
            return nullopt;
        // 누적 비중의 경계보다 작으면 그 모델 — 비중 0.45 · 0.35 · 0.2 면 0.45 · 0.8 이 경계다.
        float32         cumulative = 0.0f;
        optional<int32> lastIndex;
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

    expected<void> PropScatterPlacement::validateModels( const vector<PropScatterModel>& listModel, string_view componentMaterial )
    {
        for ( size_t modelIndex = 0; modelIndex < listModel.size(); ++modelIndex )
        {
            const PropScatterModel& model = listModel[modelIndex];
            if ( model._meshID.empty() )
            {
                StringBuilder<constant::buffer::kMax128> errorBuilder;
                errorBuilder.appendFormat( "%#", modelIndex );
                return unexpected( Error{ ErrorKind::InvalidArgument, "the model has no mesh (model index)", errorBuilder.view() } );
            }
            if ( model._listSection.empty() )
                continue;
            if ( componentMaterial.empty() == false )
            {
                StringBuilder<constant::buffer::kMax128> errorBuilder;
                errorBuilder.appendFormat( "%#", model._meshID.c_str() );
                return unexpected( Error{ ErrorKind::InvalidArgument, "the model lists section materials and the component also sets a material - set one of them", errorBuilder.view() } );
            }
            for ( size_t sectionIndex = 0; sectionIndex < model._listSection.size(); ++sectionIndex )
            {
                const PropScatterSection& section = model._listSection[sectionIndex];
                if ( section._sectionName.empty() || section._materialPath.empty() )
                {
                    StringBuilder<constant::buffer::kMax128> errorBuilder;
                    errorBuilder.appendFormat( "%#, %#", model._meshID.c_str(), sectionIndex );
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the model section needs both a section name and a material (mesh, section index)", errorBuilder.view() } );
                }
                for ( size_t earlierIndex = 0; earlierIndex < sectionIndex; ++earlierIndex )
                {
                    if ( model._listSection[earlierIndex]._sectionName == section._sectionName )
                    {
                        StringBuilder<constant::buffer::kMax128> errorBuilder;
                        errorBuilder.appendFormat( "%#, %#", model._meshID.c_str(), section._sectionName.c_str() );
                        return unexpected( Error{ ErrorKind::InvalidArgument, "the model lists a section twice (mesh, section)", errorBuilder.view() } );
                    }
                }
            }
        }
        return {};
    }

    void PropScatterPlacement::computePlacements( const PropScatterParams& params, vector<PropScatterPlacement>& outListPlacement )
    {
        outListPlacement.clear();
        if ( params._mode == PropScatterMode::Rules )
        {
            computeRulePlacements( params, nullptr, outListPlacement );
            return;
        }
        if ( params._spacing <= 0.0f || params._listModel.empty() )
            return;
        DeterministicRandom32 random{ params._seed };
        const float3          regionMin = params._regionMin;
        const float3          regionMax = params._regionMax;
        if ( params._mode == PropScatterMode::Fill )
        {
            for ( float32 z = regionMin._z; z <= regionMax._z; z += params._spacing )
            {
                for ( float32 x = regionMin._x; x <= regionMax._x; x += params._spacing )
                {
                    const float32 offsetX = random.nextFloat() * params._inwardJitter;
                    const float32 offsetZ = random.nextFloat() * params._inwardJitter;
                    plant( params, float3{ x + offsetX, regionMin._y, z + offsetZ }, random, outListPlacement );
                }
            }
            return;
        }
        // 가장자리 — 아래 · 위 변을 x 로 걷고, 왼쪽 · 오른쪽 변을 z 로 걷는다(모서리는 아래 · 위 변이 맡는다). 안쪽으로만 흔든다.
        for ( float32 x = regionMin._x; x <= regionMax._x; x += params._spacing )
        {
            const float32 bottomX = x + random.nextFloat() * params._alongJitter;
            const float32 bottomZ = regionMin._z + random.nextFloat() * params._inwardJitter;
            plant( params, float3{ bottomX, regionMin._y, bottomZ }, random, outListPlacement );
            const float32 topX = x + random.nextFloat() * params._alongJitter;
            const float32 topZ = regionMax._z - random.nextFloat() * params._inwardJitter;
            plant( params, float3{ topX, regionMin._y, topZ }, random, outListPlacement );
        }
        for ( float32 z = regionMin._z + params._spacing; z < regionMax._z; z += params._spacing )
        {
            const float32 leftX = regionMin._x + random.nextFloat() * params._inwardJitter;
            const float32 leftZ = z + random.nextFloat() * params._alongJitter;
            plant( params, float3{ leftX, regionMin._y, leftZ }, random, outListPlacement );
            const float32 rightX = regionMax._x - random.nextFloat() * params._inwardJitter;
            const float32 rightZ = z + random.nextFloat() * params._alongJitter;
            plant( params, float3{ rightX, regionMin._y, rightZ }, random, outListPlacement );
        }
    }

    void PropScatterPlacement::computeRulePlacements( const PropScatterParams& params, const IPlacementSurface* pSurface, vector<PropScatterPlacement>& outListPlacement )
    {
        outListPlacement.clear();
        if ( params._listModel.empty() )
            return;
        vector<float32> listWeight;
        listWeight.reserve( params._listModel.size() );
        for ( const PropScatterModel& model : params._listModel )
        {
            listWeight.push_back( model._weight );
        }
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
        , _propName{}
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
        pManager->executeOrDeferPostTickOnComponent( *this, []( PropScatterComponent& scatter )
        {
            // 핫 리로드가 복원한 것이 있으면 그대로 쓴다.
            if ( scatter.areSpawnedPropsAlive() == false )
                scatter.spawnProps();
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
        if ( const expected<void> models = PropScatterPlacement::validateModels( _listModel, _materialPath ); models.has_value() == false )
        {
            SW_LOG_ERROR_ID( "PropScatter.InvalidModels", "Prop scatter '%#' is not spawned: %# (%#)", _propName.c_str(), models.error().message(), models.error().detail() );
            return;
        }
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
                PropScatterPlacement::computeRulePlacements( params, &surface, listPlacement );
            }
            else
            {
                PropScatterPlacement::computeRulePlacements( params, nullptr, listPlacement );
            }
        }
        else
        {
            PropScatterPlacement::computePlacements( params, listPlacement );
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
                pManager->destroyGameObject( pProp );
                continue;
            }
            const PropScatterModel& model = _listModel[static_cast<size_t>( placement._modelIndex )];
            pMesh->setMeshID( model._meshID );
            pMesh->setLocalPosition( placement._position );
            pMesh->setLocalRotation( float3{ 0.0f, placement._yaw, 0.0f } );
            pMesh->setLocalScale( float3{ placement._scale, placement._scale, placement._scale } );
            if ( model._listSection.empty() )
            {
                if ( _materialPath.empty() == false )
                    pMesh->setMaterialPath( _materialPath );
            }
            else
            {
                // 첫 구간은 주 메시 컴포넌트가, 나머지는 같은 메시를 빌리는 구간 컴포넌트가 자기 머티리얼로 그린다.
                pMesh->setSectionName( hashed_string( model._listSection[0]._sectionName.c_str() ) );
                pMesh->setMaterialPath( model._listSection[0]._materialPath );
                for ( size_t sectionIndex = 1; sectionIndex < model._listSection.size(); ++sectionIndex )
                {
                    const PropScatterSection&   section  = model._listSection[sectionIndex];
                    MeshSectionComponent* const pSection = pProp->addComponent<MeshSectionComponent>();
                    if ( pSection == nullptr )
                        continue;
                    pSection->setSectionName( hashed_string( section._sectionName.c_str() ) );
                    pSection->setMaterialPath( section._materialPath );
                    if ( pSection->attachToComponent( pMesh ) == false )
                        SW_LOG_ERROR_ID( "PropScatter.SectionNotAttached", "Prop scatter '%#' could not attach section '%#' to its mesh", _propName.c_str(), section._sectionName.c_str() );
                }
            }
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
                    pManager->destroyGameObject( pProp );
            }
        }
        _listSpawned.clear();
    }
} // namespace sw
