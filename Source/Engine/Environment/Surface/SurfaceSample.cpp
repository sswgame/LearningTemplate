#include "pch.h"

#include "Engine/Environment/Surface/SurfaceSample.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

namespace sw
{
    namespace
    {
        struct SurfaceQueryInternal
        {
            static constexpr uint32 kAllLayers = 0xFFFFFFFFu;

            /** @brief 셰이프가 이름을 적지 않았을 때의 재질 이름 — 물리 씬이 만드는 규칙(첫 재질)과 같다. 설정이 없으면 비어 있다. */
            static hashed_string findUnnamedMaterial( const GameObjectManager& manager )
            {
                const PhysicsSettings* pSettings = manager.getSubsystem<sw::ScenePhysics>().findSettings();
                return pSettings != nullptr && pSettings->_listMaterial.empty() == false ? pSettings->_listMaterial.front()._name : hashed_string{};
            }

            /** @brief 맞은 바디가 지형이면 그 점의 가장 무거운 레이어가 적은 재질을 @p inoutSample 에 씁니다. 지형이 아니거나 레이어가 재질을 적지 않았으면 그대로 둡니다. */
            static void applyTerrainLayer( const GameObjectManager& manager, const CharacterRayHit& hit, SurfaceSample& inoutSample )
            {
                const TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( manager, hit._point._x, hit._point._z );
                if ( pTerrain == nullptr || hit._pObject == nullptr || pTerrain->getOwner() != hit._pObject )
                    return;
                const SurfaceLayerWeights weights   = pTerrain->computeLayerWeightsAt( hit._point._x, hit._point._z );
                uint32                    heaviest  = 0;
                float32                   bestValue = weights.get( 0 );
                for ( uint32 layer = 1; layer < SurfaceLayerWeights::kLayerCount; ++layer )
                {
                    if ( weights.get( layer ) > bestValue )
                    {
                        bestValue = weights.get( layer );
                        heaviest  = layer;
                    }
                }
                const vector<TerrainLayer>& listLayer = pTerrain->getLayers();
                if ( heaviest >= listLayer.size() || listLayer[heaviest]._physicsMaterial.empty() )
                    return;
                inoutSample._material     = listLayer[heaviest]._physicsMaterial;
                inoutSample._terrainLayer = static_cast<uint8>( heaviest );
                inoutSample._source       = SurfaceSource::TerrainLayer;
            }

            /** @brief 맞은 바디의 선속도입니다(바디가 없거나 씬이 없으면 0). */
            static float3 findBodyVelocity( const GameObjectManager& manager, const CharacterRayHit& hit )
            {
                if ( hit._body.isValid() == false )
                    return float3{};
                const ScenePhysics& physics = manager.getSubsystem<sw::ScenePhysics>();
                if ( hit._bIs2D )
                {
                    const IPhysicsScene2D* pScene2D = physics.findScene2D();
                    const float2           velocity = pScene2D != nullptr ? pScene2D->getLinearVelocity( hit._body ) : float2{};
                    return float3{ velocity._x, velocity._y, 0.0f };
                }
                const IPhysicsScene3D* pScene3D = physics.findScene3D();
                return pScene3D != nullptr ? pScene3D->getLinearVelocity( hit._body ) : float3{};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SurfaceSample::sampleBelow( GameObjectManager& manager, const shared_ptr<const SurfaceKindTable>& pTable, const float3& from, const float3& to, uint64 ignoreObjectID, bool bIs2D,
                                     SurfaceSample& outSample )
    {
        const float3    direction = to - from;
        const float32   distance  = direction.getLength();
        CharacterRayHit hit;
        const bool      bHit = bIs2D ? CharacterRayHit::raycast2D( manager, from, direction, distance, SurfaceQueryInternal::kAllLayers, ignoreObjectID, hit )
                                     : CharacterRayHit::raycast3D( manager, from, direction, distance, SurfaceQueryInternal::kAllLayers, ignoreObjectID, hit );
        return bHit && resolveHit( manager, pTable, hit, outSample );
    }

    bool SurfaceSample::resolveHit( GameObjectManager& manager, const shared_ptr<const SurfaceKindTable>& pTable, const CharacterRayHit& hit, SurfaceSample& outSample )
    {
        SW_ASSERT( pTable != nullptr );
        const SurfaceKindTable& table = *pTable;
        SurfaceSample           sample;
        sample._point           = hit._point;
        sample._normal          = hit._normal;
        sample._objectID        = hit._pObject != nullptr ? hit._pObject->getObjectID() : 0;
        sample._surfaceVelocity = SurfaceQueryInternal::findBodyVelocity( manager, hit );
        sample._material        = hit._material.empty() ? SurfaceQueryInternal::findUnnamedMaterial( manager ) : hit._material;
        if ( hit._bIs2D == false )
        {
            SurfaceQueryInternal::applyTerrainLayer( manager, hit, sample );
            // 물은 바닥 위에 있다 — 바닥 점이 수면 아래면 발은 물속이다(깊이로 침수 정도를 낸다).
            const WaterBodyComponent* pWater = WaterBodyComponent::findWaterAt( manager, hit._point._x, hit._point._z );
            float32                   depth  = 0.0f;
            if ( pWater != nullptr && pWater->isUnderwater( hit._point, depth ) && depth > 0.0f )
            {
                sample._material   = pWater->getSurfaceKind().empty() ? table._waterKind : pWater->getSurfaceKind(); // 용암 호수는 같은 물 질의에 제 종류를 낸다
                sample._waterDepth = depth;
                sample._source     = SurfaceSource::Water;
            }
        }
        sample._pKind = shared_ptr<const SurfaceKindDef>( pTable, table.findKind( sample._material ) ); // 같은 수명(aliasing) — 표가 결과와 함께 산다
        if ( sample._pKind == nullptr )
        {
            const string_view name( sample._material.c_str() );
            SW_LOG_ERROR_ONCE_FOR_ID( "Surface.KindMissing", name, "Surface '%#' (%#) has no surface kind in the surface kinds table - add one (footsteps and footprints skip it)",
                                      sample._material.c_str(), getSourceName( sample._source ) );
            return false;
        }
        outSample = sample;
        return true;
    }

    const utf8* SurfaceSample::getSourceName( SurfaceSource source )
    {
        switch ( source )
        {
            case SurfaceSource::Collider:
                return "collider";
            case SurfaceSource::TerrainLayer:
                return "terrain layer";
            case SurfaceSource::Water:
                return "water";
        }
        return "?";
    }
} // namespace sw
