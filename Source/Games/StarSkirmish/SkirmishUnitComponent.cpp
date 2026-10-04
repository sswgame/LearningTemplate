#include "pch.h"

#include "Games/StarSkirmish/SkirmishUnitComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/StarSkirmish/SkirmishDirectorComponent.h"

namespace sw
{
    namespace
    {
        struct SkirmishUnitComponentInternal
        {
            /**
             * @brief Kenney Space Kit 모델을 맵 칸에 맞추는 비율입니다. 키트 모델은 크기가 제각각이라(안테나 0.75 · 격납고 3.3) 모델마다 가로 폭을
             *        표에 두고, 건물은 차지하는 칸(footprint)의 0.9 배, 유닛은 충돌 지름의 1.6 배(위에서 내려다볼 때 원보다 조금 커야 배 모양이 읽힌다)로 늘린다.
             */
            static constexpr float32 kFootprintFill = 0.9f;
            static constexpr float32 kUnitFill      = 1.6f;

            static constexpr SkirmishUnitModel kArrUnitModel[] = {
                {      "minerals",     "rock_crystals", 0.85f},
                { "rich_minerals",     "rock_crystals", 0.85f},
                {        "geyser",            "crater", 0.83f},
                {"command_center",    "hangar_large_a",  3.0f},
                {  "supply_depot", "machine_generator",  0.7f},
                {      "refinery",         "structure",  1.0f},
                {      "barracks",    "hangar_small_a",  2.0f},
                {       "academy",  "machine_wireless", 0.75f},
                {       "factory",    "hangar_round_a", 3.27f},
                {      "starport",    "platform_large",  2.0f},
                {        "bunker",     "turret_double",  0.9f},
                {"missile_turret",     "turret_single", 0.72f},
                {        "worker",       "craft_miner",  2.6f},
                {        "marine",   "craft_speeder_a",  2.1f},
                {       "firebat",   "craft_speeder_b", 2.03f},
                {       "vulture",       "craft_racer", 2.03f},
                {          "tank",     "craft_cargo_a", 2.45f},
                {       "goliath",   "craft_speeder_b", 2.03f},
                {        "wraith",       "craft_racer", 2.03f},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SkirmishUnitComponent::SkirmishUnitComponent()
        : _director{}
        , _airHeight{ 2.5f }
        , _lastPosition{ 0.0f, 0.0f, 0.0f }
        , _unitId{}
        , _pShownLook{ nullptr }
        , _modelWidth{ 1.0f }
        , _yaw{ MathUtil::Pi }
        , _bPlaced{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void SkirmishUnitComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 판을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void SkirmishUnitComponent::assignUnit( GameObjectHandle director, SlotHandle unitId, float32 modelWidth )
    {
        _director   = director;
        _unitId     = unitId;
        _modelWidth = modelWidth > 0.0f ? modelWidth : 1.0f;
        _pShownLook = nullptr;
        _yaw        = MathUtil::Pi; // 처음엔 카메라(남쪽) 쪽을 본다
        _bPlaced    = SW_FALSE;
    }

    const SkirmishUnitModel* SkirmishUnitComponent::findUnitModel( const hashed_string& unitId )
    {
        for ( const SkirmishUnitModel& entry : SkirmishUnitComponentInternal::kArrUnitModel )
        {
            if ( unitId == hashed_string( entry._pUnitId ) )
                return &entry;
        }
        return nullptr;
    }

    string SkirmishUnitComponent::makeModelPath( const utf8* pName )
    {
        return string( "game/starskirmish/models/" ) + pName + ".mesh";
    }

    void SkirmishUnitComponent::onTick( float32 deltaTime )
    {
        using Internal = SkirmishUnitComponentInternal;
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const SkirmishDirectorComponent* pDirector = SkirmishDirectorComponent::resolveDirector( *pManager, _director );
        const RtsUnit*                   pUnit     = pDirector != nullptr ? pDirector->getWorld().findUnit( _unitId ) : nullptr;
        if ( pUnit == nullptr )
            return;

        // 크기 · 자리 — 모델 바닥이 원점이다. 건물은 지은 만큼 솟고, 광물은 남은 만큼 낮아진다. 공중 유닛은 떠 있다.
        const RtsUnitDef& def = *pUnit->_pDef;
        float3            position{};
        float3            scale{};
        if ( pUnit->isResource() || pUnit->isBuilding() )
        {
            const float32 size   = static_cast<float32>( def._footprint ) * Internal::kFootprintFill / _modelWidth;
            float32       height = 1.0f;
            if ( pUnit->isResource() && def._resourceType != RtsResourceType::Gas )
                height = 0.4f + 0.6f * ( def._resourceAmount > 0 ? static_cast<float32>( pUnit->_resourceLeft ) / static_cast<float32>( def._resourceAmount ) : 1.0f );
            else if ( pUnit->isBuilding() )
                height = 0.2f + 0.8f * MathUtil::clamp( pUnit->_buildProgress, 0.0f, 1.0f );
            scale    = float3{ size, size * height, size };
            position = float3{ pUnit->_position._x, 0.0f, pUnit->_position._z };
        }
        else
        {
            const float32 size = def._radius * 2.0f * Internal::kUnitFill / _modelWidth;
            scale              = float3{ size, size, size };
            position           = float3{ pUnit->_position._x, def._bAir != SW_FALSE ? _airHeight : 0.0f, pUnit->_position._z };
        }
        // 움직이는 유닛은 움직인 쪽을 본다(키트 모델의 앞이 +Z). 멈추면 마지막 방향을 지킨다.
        const float32 moveX       = position._x - _lastPosition._x;
        const float32 moveZ       = position._z - _lastPosition._z;
        const bool    bMovingUnit = _bPlaced == SW_TRUE && pUnit->isBuilding() == false && pUnit->isResource() == false;
        if ( bMovingUnit && moveX * moveX + moveZ * moveZ > 1.0e-6f )
            _yaw = MathUtil::atan2( moveX, moveZ );
        _lastPosition = position;
        pMesh->setLocalPosition( position );
        pMesh->setLocalScale( scale );
        pMesh->setLocalRotation( float3{ 0.0f, _yaw, 0.0f } );
        const shared_ptr<MaterialInstance>& look = pDirector->findUnitLook( *pUnit, pDirector->isSelected( _unitId ) );
        if ( look != nullptr && look.get() != _pShownLook )
        {
            pMesh->setMaterialInstance( look );
            _pShownLook = look.get();
        }
        if ( _bPlaced == SW_FALSE )
        {
            pMesh->setVisible( true );
            _bPlaced = SW_TRUE;
        }
    }
} // namespace sw
