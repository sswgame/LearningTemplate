#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityBuildingViewComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityViewComponent.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"

namespace sw
{
    CityBuildingViewComponent::CityBuildingViewComponent()
        : _buildingIndex{ -1 }
        , _blockInset{ 0.15f }
        , _shownKey{ -1 }
    {
        setCanEverTick( true );
    }

    void CityBuildingViewComponent::assignBuilding( GameObjectHandle gameMode, int32 buildingIndex )
    {
        bindGameMode( gameMode );
        _buildingIndex = buildingIndex;
        _shownKey      = -1;
    }

    bool CityBuildingViewComponent::hasModel( const RuleViewCatalog& views, const CityBuildingDef& def )
    {
        return findView( views, def, 0, true ) != nullptr;
    }

    const RuleViewDef* CityBuildingViewComponent::findView( const RuleViewCatalog& views, const CityBuildingDef& def, int32 level, bool bInhabited )
    {
        if ( def._kind != CityBuildingKind::House )
            return views.find( def._id );
        // 집 단계마다 커지는 집 모델 — 작은 단층에서 넓은 이층까지. 빈 땅은 낮은 울타리다.
        StringBuilder<constant::buffer::kMax128> key;
        if ( bInhabited )
            key.appendFormat( "%#.%#", def._id.view(), level );
        else
            key.appendFormat( "%#.empty", def._id.view() );
        return views.find( hashed_string( key.view() ) );
    }

    void CityBuildingViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        // 모델 표를 나눠 주는 도시 뷰는 디렉터와 같은 오브젝트다(도시 뷰가 건물을 세우며 그 오브젝트를 적는다).
        const CityBuilderGameModeComponent* pGameMode = findGameMode<CityBuilderGameModeComponent>();
        const CityViewComponent*            pCityView = findGameMode<CityViewComponent>();
        if ( pGameMode == nullptr || pCityView == nullptr )
            return;
        const RuleViewCatalog* pViews = pCityView->getBuildingViews(); // 표를 적지 않은 도시는 모두 색 상자다
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

        // 모델이 있는 건물은 바닥 가운데를 칸 묶음 가운데에, 정면을 카메라(남쪽 −Z) 쪽으로 돌려 둔다. 밭 · 조각상은 색 상자 그대로다.
        const CityBuildingDef& def    = *pBuilding->_pDef;
        const float32          size   = static_cast<float32>( def._size );
        const float3           center = float3{ static_cast<float32>( pBuilding->_origin._x ) + size * 0.5f, 0.0f, static_cast<float32>( pBuilding->_origin._y ) + size * 0.5f };
        const RuleViewDef*     pView  = pViews != nullptr ? findView( *pViews, def, pBuilding->_level, bInhabited ) : nullptr;
        const WorldOrigin&     origin = pOwner->getManager()->getWorldOrigin(); // 도시는 절대 자리다
        if ( pView != nullptr )
        {
            const float32 scale = size / pView->_width; // n 칸 건물은 한 칸 배율의 n 배
            pMesh->setMeshID( pView->_mesh.path() );
            pMesh->setLocalScale( float3{ scale } );
            pMesh->setLocalRotation( float3{ 0.0f, MathUtil::kPi, 0.0f } );
            pMesh->setLocalPosition( origin.toLocal( center ) );
        }
        else
        {
            const CityCatalog*       pCatalog     = pGameMode->findCatalog();
            const bool               bLevelHeight = def._kind == CityBuildingKind::House && bInhabited && pCatalog != nullptr;
            const CityHouseLevelDef* pLevel       = bLevelHeight ? pCatalog->findHouseLevel( pBuilding->_level ) : nullptr;
            const float32            height       = pLevel != nullptr ? pLevel->_blockHeight : def._blockHeight;
            pMesh->setMeshID( def._kind == CityBuildingKind::Decoration ? "Cylinder" : "Cube" );
            pMesh->setLocalScale( float3{ size - _blockInset, height, size - _blockInset } );
            pMesh->setLocalPosition( origin.toLocal( center + float3{ 0.0f, height * 0.5f, 0.0f } ) );
        }
        pMesh->setVisible( true );
        _shownKey = key; // 모습을 다 맞춘 뒤에 적는다
    }
} // namespace sw
