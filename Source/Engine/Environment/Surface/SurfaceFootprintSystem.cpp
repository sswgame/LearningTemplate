#include "pch.h"

#include "Engine/Environment/Surface/SurfaceFootprintSystem.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

namespace sw
{
    namespace
    {
        struct SurfaceFootprintSystemInternal
        {
            /** @brief 발자국을 바닥에서 띄우는 높이(m)입니다 — 바닥과 같은 깊이에서 깜박이지 않게(z-fighting). */
            static constexpr float32 kLiftAboveGround = 0.004f;

            /** @brief 감시 값 `Surface.FootprintCount` 입니다. 지금 살아 있는 발자국 수입니다. */
            [[nodiscard]] static bool readFootprintCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<SurfaceFootprintSystem>().getFootprintCount() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.FootprintObjects` 입니다. 링에 든 발자국 오브젝트 수입니다(용량을 넘지 않는다). */
            [[nodiscard]] static bool readFootprintObjects( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<SurfaceFootprintSystem>().getFootprintObjectCount() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.FootprintPlaced` 입니다. 이 씬에서 놓은 발자국 수(누적)입니다. */
            [[nodiscard]] static bool readFootprintPlaced( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<SurfaceFootprintSystem>().getPlacedCount() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.FootstepCount` 입니다. 종류 `key` 를 밟은 발걸음 수(누적)입니다. */
            [[nodiscard]] static bool readFootstepCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<SurfaceFootprintSystem>().getFootstepCount( hashed_string( query._key ) ) ) );
                return true;
            }

            /** @brief 감시 값 `Surface.LastFootstep` 입니다. 마지막 발걸음의 표면 종류 이름입니다(없으면 값이 없다). */
            [[nodiscard]] static bool readLastFootstep( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                const hashed_string& kind = query._pManager->getSubsystem<SurfaceFootprintSystem>().getLastKind();
                if ( kind.empty() )
                {
                    outValue.setText( "no footstep yet" );
                    return false;
                }
                outValue.setText( kind.c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceFootprintCount, "Surface.FootprintCount", "Footprints alive in the scene (lifetime left)", &SurfaceFootprintSystemInternal::readFootprintCount, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceFootprintObjects, "Surface.FootprintObjects", "Footprint objects in the ring (never above the capacity)", &SurfaceFootprintSystemInternal::readFootprintObjects,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceFootprintPlaced, "Surface.FootprintPlaced", "Footprints placed in this scene (cumulative, reused slots count)", &SurfaceFootprintSystemInternal::readFootprintPlaced,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceFootstepCount, "Surface.FootstepCount", "Footsteps on the surface kind named key (cumulative)", &SurfaceFootprintSystemInternal::readFootstepCount, false, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstep, "Surface.LastFootstep", "Surface kind of the last footstep", &SurfaceFootprintSystemInternal::readLastFootstep, false, false );
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( SurfaceFootprintSystem, FlushEnd )

    SurfaceFootprintSystem::FootprintRecord::FootprintRecord()
        : _visual{}
        , _baseScale{ 1.0f, 1.0f, 1.0f }
        , _age{ 0.0f }
        , _lifetime{ 0.0f }
        , _bAlive{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SurfaceFootprintSystem::SurfaceFootprintSystem( GameObjectManager& manager )
        : _pManager{ &manager }
        , _listFootprint{}
        , _mapFootstepCount{}
        , _lastKind{}
        , _capacity{ 0 }
        , _nextSlot{ 0 }
        , _placedCount{ 0 }
    {
    }

    void SurfaceFootprintSystem::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        for ( FootprintRecord& footprint : _listFootprint )
        {
            if ( footprint._bAlive == SW_FALSE )
                continue;
            footprint._age += deltaTime;
            GameObject*     pObject = _pManager->resolveGameObject( footprint._visual );
            SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( footprint._age >= footprint._lifetime )
            {
                footprint._bAlive = SW_FALSE;
                if ( pObject != nullptr )
                    pObject->setActive( false );
                continue;
            }
            // 남은 수명만큼 줄인다 — 오래된 자국일수록 작고 흐리다(데칼의 페이드 자리).
            if ( pScene != nullptr )
                pScene->setLocalScale( footprint._baseScale * ( 1.0f - footprint._age / footprint._lifetime ) );
        }
    }

    void SurfaceFootprintSystem::onSceneCleared()
    {
        _listFootprint.clear();
        _mapFootstepCount.clear();
        _lastKind    = hashed_string{};
        _capacity    = 0;
        _nextSlot    = 0;
        _placedCount = 0;
    }

    void SurfaceFootprintSystem::addFootstep( const SurfaceKindTable& table, const SurfaceKindDef& kind, const float3& point, float32 yaw )
    {
        ++_mapFootstepCount[kind._name];
        _lastKind = kind._name;
        if ( kind.hasFootprints() == false )
            return;
        if ( _capacity == 0 )
            _capacity = table._footprintCapacity;
        // 빈 칸(수명이 끝난 것)을 먼저 쓰고, 없으면 링을 키우고, 가득 찼으면 가장 오래된 칸을 다시 쓴다.
        FootprintRecord* pSlot = nullptr;
        for ( FootprintRecord& footprint : _listFootprint )
        {
            if ( footprint._bAlive == SW_FALSE )
            {
                pSlot = &footprint;
                break;
            }
        }
        if ( pSlot == nullptr && _listFootprint.size() < _capacity )
        {
            _listFootprint.emplace_back();
            pSlot = &_listFootprint.back();
        }
        if ( pSlot == nullptr )
        {
            pSlot     = &_listFootprint[_nextSlot];
            _nextSlot = ( _nextSlot + 1 ) % _capacity;
        }
        if ( placeVisual( *pSlot, kind, point, yaw ) == false )
            return;
        pSlot->_age      = 0.0f;
        pSlot->_lifetime = kind._footprintLifetime;
        pSlot->_bAlive   = SW_TRUE;
        ++_placedCount;
    }

    bool SurfaceFootprintSystem::placeVisual( FootprintRecord& slot, const SurfaceKindDef& kind, const float3& point, float32 yaw )
    {
        GameObject* pObject = _pManager->resolveGameObject( slot._visual );
        if ( pObject == nullptr || pObject->isPendingDestroy() )
        {
            AssetManager* pAssets = _pManager->findAssetManager();
            pObject               = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( _pManager, kind._footprintPrefab.get(), "Footprint" ) : nullptr;
            SceneComponent* pNew  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pNew == nullptr )
            {
                SW_LOG_ERROR_ONCE_FOR_ID( "Surface.FootprintPrefabFailed", kind._footprintPrefab.get(), "Footprint prefab '%#' of surface kind '%#' could not be spawned or has no scene component",
                                          kind._footprintPrefab.c_str(), kind._name.c_str() );
                return false;
            }
            slot._visual    = pObject->getHandle();
            slot._baseScale = pNew->getLocalScale();
        }
        pObject->setActive( true );
        SceneComponent* pScene = pObject->getPrimarySceneComponent();
        pScene->setLocalPosition( point + float3{ 0.0f, SurfaceFootprintSystemInternal::kLiftAboveGround, 0.0f } );
        pScene->setLocalRotation( float3{ 0.0f, yaw, 0.0f } );
        pScene->setLocalScale( slot._baseScale );
        return true;
    }

    uint32 SurfaceFootprintSystem::getFootprintCount() const
    {
        uint32 aliveCount = 0;
        for ( const FootprintRecord& footprint : _listFootprint )
        {
            aliveCount += footprint._bAlive == SW_TRUE ? 1u : 0u;
        }
        return aliveCount;
    }

    uint32 SurfaceFootprintSystem::getFootstepCount( const hashed_string& kind ) const
    {
        const auto found = _mapFootstepCount.find( kind );
        return found != _mapFootstepCount.end() ? found->second : 0;
    }
} // namespace sw
