#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneOccupantIndex.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

namespace sw
{
    ZoneOccupantIndex::ZoneOccupantIndex()
        : _listCandidate{}
        , _grid{}
        , _listHandleScratch{}
        , _buildCount{ 0 }
        , _bBuilt{ SW_FALSE }
    {
    }

    ZoneOccupantIndex* ZoneOccupantIndex::find( const GameObjectManager& manager )
    {
        // 이 키로는 이 타입만 붙인다(`ensureFor`).
        return static_cast<ZoneOccupantIndex*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    ZoneOccupantIndex& ZoneOccupantIndex::ensureFor( GameObjectManager& manager )
    {
        ZoneOccupantIndex* pIndex = find( manager );
        if ( pIndex != nullptr )
            return *pIndex;
        unique_ptr<ZoneOccupantIndex> index = make_unique<ZoneOccupantIndex>();
        pIndex                              = index.get();
        const bool bAdded                   = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( index ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았고, 씬 데이터를 건드리지 않는다고 선언해 접근 충돌도 없다
        (void)bAdded;
        return *pIndex;
    }

    void ZoneOccupantIndex::releaseIfUnused( GameObjectManager& manager )
    {
        // 모듈을 내리면 컴포넌트가 먼저 지워진다 — 마지막 볼륨이 떠날 때 떼므로 매니저에 이 타입이 남지 않는다(`ISceneFrameSystem` 의 약속).
        if ( manager.getComponentRegistry().getAll<ZoneVolumeComponent>().empty() )
            manager.removeFrameSystem( hashed_string( kFrameSystemKey ) );
    }

    void ZoneOccupantIndex::runLane( SceneJobContext& context )
    {
        (void)context;
        _bBuilt = SW_FALSE;
    }

    SceneFrameAccess ZoneOccupantIndex::getFrameAccess() const
    {
        SceneFrameAccess access{};
        // 자기 깃발 하나만 내린다 — 레인 시스템이라 워커에서 돈다.
        return access;
    }

    void ZoneOccupantIndex::queryCandidates( GameObjectManager& manager, float32 minX, float32 minZ, float32 maxX, float32 maxZ, vector<ZoneOccupantCandidate>& outListCandidate )
    {
        outListCandidate.clear();
        if ( _bBuilt == SW_FALSE )
            rebuild( manager );
        _grid.queryAABB( minX, minZ, maxX, maxZ, _listHandleScratch ); // 핸들 순서 = 후보 번호 순서(세대가 모두 1 이다)
        outListCandidate.reserve( _listHandleScratch.size() );
        for ( const SlotHandle handle : _listHandleScratch )
        {
            outListCandidate.push_back( _listCandidate[handle.index()] );
        }
    }

    void ZoneOccupantIndex::rebuild( GameObjectManager& manager )
    {
        _bBuilt = SW_TRUE;
        ++_buildCount;
        _listCandidate.clear();
        // 셀은 볼륨들의 가장 큰 수평 반 크기의 두 배다. 볼륨의 반 크기는 0.01 m 이상이다(속성 하한).
        float32 maxReach = 0.0f;
        for ( const ZoneVolumeComponent* pZone : manager.getComponentRegistry().getAll<ZoneVolumeComponent>() )
        {
            maxReach = MathUtil::max( maxReach, pZone->getHorizontalReach() );
        }
        const float32 cellSize = MathUtil::max( maxReach, 0.01f ) * 2.0f;
        if ( _grid.getCellSize() != cellSize )
            _grid = SpatialHashGrid2D{ cellSize };
        else
            _grid.clear();

        // 예전 볼륨마다의 순회와 같은 순서 · 같은 거르기다 — 캐릭터는 발, 움직이는 강체는 중심. 한 오브젝트에 둘이 있으면 캐릭터 쪽 하나다.
        vector<float3> listPosition;
        manager.forEachComponentOfType<CharacterControllerComponent>( [this, &listPosition]( CharacterControllerComponent* pController )
        {
            GameObject* pObject = pController->getOwner();
            if ( pObject == nullptr )
                return;
            _listCandidate.push_back( ZoneOccupantCandidate{ pObject, pController, nullptr } );
            listPosition.push_back( pController->getWorldPosition() );
        } );
        manager.forEachComponentOfType<RigidBodyComponent>( [this, &listPosition]( RigidBodyComponent* pBody )
        {
            GameObject* pObject  = pBody->getOwner();
            const bool  bDynamic = pBody->getBodyType() == PhysicsBodyType::Dynamic && pBody->isTrigger() == false;
            if ( bDynamic == false || pObject == nullptr || pObject->getComponent<CharacterControllerComponent>() != nullptr )
                return;
            _listCandidate.push_back( ZoneOccupantCandidate{ pObject, nullptr, pBody } );
            listPosition.push_back( pBody->getWorldPosition() );
        } );
        for ( size_t candidateIndex = 0; candidateIndex < _listCandidate.size(); ++candidateIndex )
        {
            const float3& position = listPosition[candidateIndex];
            // 세대 0 은 무효라 1 로 둔다. 인덱스가 `_listCandidate` 의 번호다.
            _grid.insert( SlotHandle::make( static_cast<uint32>( candidateIndex ), 1u ), position._x, position._z, position._x, position._z );
        }
    }
} // namespace sw
