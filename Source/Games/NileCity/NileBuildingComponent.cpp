#include "pch.h"

#include "Games/NileCity/NileBuildingComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "Games/NileCity/NileGameModeComponent.h"

namespace sw
{
    namespace
    {
        struct NileBuildingComponentInternal
        {
            static float32 computeBlockHeight( const CityBuildingDef& def, int32 level, bool bInhabited )
            {
                switch ( def._kind )
                {
                    case CityBuildingKind::House:
                        return bInhabited ? 0.5f + 0.2f * static_cast<float32>( level ) : 0.15f;
                    case CityBuildingKind::Service:
                        return 1.2f;
                    case CityBuildingKind::Producer:
                        return def._bRequiresTerrain != SW_FALSE ? 0.25f : 0.9f;
                    case CityBuildingKind::Storage:
                        return 1.3f;
                    case CityBuildingKind::Market:
                        return 0.8f;
                    case CityBuildingKind::Decoration:
                        return 0.4f;
                }
                return 1.0f;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NileBuildingComponent::NileBuildingComponent()
        : _buildingIndex{ -1 }
        , _shownKey{ -1 }
    {
        setCanEverTick( true );
    }

    void NileBuildingComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 시뮬레이션을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void NileBuildingComponent::assignBuilding( GameObjectHandle gameMode, int32 buildingIndex )
    {
        bindGameMode( gameMode );
        _buildingIndex = buildingIndex;
        _shownKey      = -1;
    }

    bool NileBuildingComponent::hasModel( const RuleViewCatalog& views, const CityBuildingDef& def )
    {
        return findView( views, def, 0, true ) != nullptr;
    }

    const RuleViewDef* NileBuildingComponent::findView( const RuleViewCatalog& views, const CityBuildingDef& def, int32 level, bool bInhabited )
    {
        if ( def._kind != CityBuildingKind::House )
            return views.find( def._id );
        // 집 단계마다 커지는 집 모델 — 작은 단층에서 넓은 이층까지. 빈 땅은 낮은 울타리다.
        string key( def._id.view() );
        if ( bInhabited )
        {
            key += ".";
            key += to_string( level );
        }
        else
        {
            key += ".empty";
        }
        return views.find( hashed_string( key ) );
    }

    void NileBuildingComponent::onTick( TickContext& context )
    {
        using Internal = NileBuildingComponentInternal;
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const NileGameModeComponent* pGameMode = findGameMode<NileGameModeComponent>();
        const RuleViewCatalog*       pViews    = pGameMode != nullptr ? pGameMode->getBuildingViews() : nullptr;
        if ( pViews == nullptr )
            return;
        // 같은 그룹의 다른 건물과 함께 읽는다 — 첨자 대신 포인터로(쓰기로 잡히지 않게).
        const vector<CityBuilding>& listBuilding         = pGameMode->getCity().getBuildings();
        const bool                  bInRange             = 0 <= _buildingIndex && _buildingIndex < static_cast<int32>( listBuilding.size() );
        const CityBuilding*         pBuilding            = bInRange ? listBuilding.data() + _buildingIndex : nullptr;
        const bool                  bBuildingUnavailable = pBuilding == nullptr || pBuilding->_bAlive == SW_FALSE || pBuilding->_pDef == nullptr;
        if ( bBuildingUnavailable )
            return;
        const bool  bInhabited = pBuilding->_population > 0;
        const int32 key        = pBuilding->_level * 2 + ( bInhabited ? 1 : 0 );
        if ( key == _shownKey )
            return;
        _shownKey = key;

        // 모델이 있는 건물은 바닥 가운데를 칸 묶음 가운데에, 정면을 카메라(남쪽 −Z) 쪽으로 돌려 둔다. 밭 · 조각상은 색 상자 그대로다.
        const CityBuildingDef& def    = *pBuilding->_pDef;
        const float32          size   = static_cast<float32>( def._size );
        const float3           center = float3{ static_cast<float32>( pBuilding->_origin._x ) + size * 0.5f, 0.0f, static_cast<float32>( pBuilding->_origin._y ) + size * 0.5f };
        const RuleViewDef*     pView  = findView( *pViews, def, pBuilding->_level, bInhabited );
        if ( pView != nullptr )
        {
            const float32 scale = size / pView->_width; // n 칸 건물은 한 칸 배율의 n 배
            pMesh->setMeshID( pView->_mesh.path() );
            pMesh->setLocalScale( float3{ scale } );
            pMesh->setLocalRotation( float3{ 0.0f, MathUtil::kPi, 0.0f } );
            pMesh->setLocalPosition( center );
        }
        else
        {
            const float32 height = Internal::computeBlockHeight( def, pBuilding->_level, bInhabited );
            pMesh->setMeshID( def._kind == CityBuildingKind::Decoration ? "Cylinder" : "Cube" );
            pMesh->setLocalScale( float3{ size - 0.15f, height, size - 0.15f } );
            pMesh->setLocalPosition( center + float3{ 0.0f, height * 0.5f, 0.0f } );
        }
        pMesh->setVisible( true );
    }
} // namespace sw
