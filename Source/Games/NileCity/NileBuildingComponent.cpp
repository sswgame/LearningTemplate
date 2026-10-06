#include "pch.h"

#include "Games/NileCity/NileBuildingComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/NileCity/NileDirectorComponent.h"

namespace sw
{
    namespace
    {
        struct NileBuildingComponentInternal
        {
            /**
             * @brief Kenney City Kit Suburban 모델의 칸당 배율입니다. 가장 넓은 집(`building_type_b`, 폭 1.82)이 한 칸(1 m) 안에 들게 0.5 이고,
             *        n×n 건물은 n 배다.
             */

            static constexpr const utf8* kArrHouseModel[] = { "building_type_h", "building_type_a", "building_type_g", "building_type_c",
                                                              "building_type_e", "building_type_f", "building_type_b", "building_type_d" };
            static constexpr const utf8* kArrOtherModel[] = { "fence_low", "tree_large", "planter" };

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
        : _director{}
        , _buildingIndex{ -1 }
        , _modelScale{ 0.5f }
        , _decorationScale{ 3.0f }
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

    void NileBuildingComponent::assignBuilding( GameObjectHandle director, int32 buildingIndex )
    {
        _director      = director;
        _buildingIndex = buildingIndex;
        _shownKey      = -1;
    }

    bool NileBuildingComponent::hasModel( const CityBuildingDef& def )
    {
        return findModel( def, 0, true ) != nullptr;
    }

    const utf8* NileBuildingComponent::findModel( const CityBuildingDef& def, int32 level, bool bInhabited )
    {
        switch ( def._kind )
        {
            case CityBuildingKind::House:
            {
                // 집 단계(0..7)마다 커지는 집 모델 — 작은 단층에서 넓은 이층까지. 빈 땅은 낮은 울타리다.
                if ( bInhabited == false )
                    return "fence_low";
                return NileBuildingComponentInternal::kArrHouseModel[MathUtil::clamp( level, 0, 7 )];
            }
            case CityBuildingKind::Service:
            {
                return def._size == 1 ? "building_type_h" : "building_type_c";
            }
            case CityBuildingKind::Producer:
            {
                return def._bRequiresTerrain != SW_FALSE ? nullptr : "building_type_g";
            }
            case CityBuildingKind::Storage:
            {
                return "building_type_d";
            }
            case CityBuildingKind::Market:
            {
                return "building_type_f";
            }
            case CityBuildingKind::Decoration:
            {
                if ( def._id == hashed_string( "garden" ) )
                    return "tree_large";
                return def._id == hashed_string( "plaza" ) ? "planter" : nullptr;
            }
        }
        return nullptr;
    }

    void NileBuildingComponent::collectModelPaths( vector<string>& outListPath )
    {
        for ( const utf8* pName : NileBuildingComponentInternal::kArrHouseModel )
            outListPath.push_back( makeModelPath( pName ) );
        for ( const utf8* pName : NileBuildingComponentInternal::kArrOtherModel )
            outListPath.push_back( makeModelPath( pName ) );
    }

    string NileBuildingComponent::makeModelPath( const utf8* pName )
    {
        return string( "game/nilecity/models/" ) + pName + string( MeshAssetFormat::kExtension );
    }

    void NileBuildingComponent::onTick( float32 deltaTime )
    {
        using Internal = NileBuildingComponentInternal;
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const NileDirectorComponent* pDirector = GameDirectorComponent::resolve<NileDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr )
            return;
        // 같은 그룹의 다른 건물과 함께 읽는다 — 첨자 대신 포인터로(쓰기로 잡히지 않게).
        const vector<CityBuilding>& listBuilding = pDirector->getCity().getBuildings();
        const bool                  bInRange     = 0 <= _buildingIndex && _buildingIndex < static_cast<int32>( listBuilding.size() );
        const CityBuilding*         pBuilding    = bInRange ? listBuilding.data() + _buildingIndex : nullptr;
        if ( pBuilding == nullptr || pBuilding->_bAlive == SW_FALSE || pBuilding->_pDef == nullptr )
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
        const utf8*            pModel = findModel( def, pBuilding->_level, bInhabited );
        if ( pModel != nullptr )
        {
            const float32 scale = _modelScale * size * ( def._kind == CityBuildingKind::Decoration ? _decorationScale : 1.0f );
            pMesh->setMeshId( makeModelPath( pModel ) );
            pMesh->setLocalScale( float3{ scale } );
            pMesh->setLocalRotation( float3{ 0.0f, MathUtil::kPi, 0.0f } );
            pMesh->setLocalPosition( center );
        }
        else
        {
            const float32 height = Internal::computeBlockHeight( def, pBuilding->_level, bInhabited );
            pMesh->setMeshId( def._kind == CityBuildingKind::Decoration ? "Cylinder" : "Cube" );
            pMesh->setLocalScale( float3{ size - 0.15f, height, size - 0.15f } );
            pMesh->setLocalPosition( center + float3{ 0.0f, height * 0.5f, 0.0f } );
        }
        pMesh->setVisible( true );
    }
} // namespace sw
