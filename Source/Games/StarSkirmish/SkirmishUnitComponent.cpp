#include "pch.h"

#include "Games/StarSkirmish/SkirmishUnitComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "Games/StarSkirmish/SkirmishGameModeComponent.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 감시 값 `Skirmish.ViewLagMax` 입니다. 모든 유닛 뷰 가운데 메시가 디렉터의 유닛 자리에서 가장 멀리 벗어난 거리입니다.
         *        측정한 뷰가 하나도 없으면(뷰가 없거나 모두 비교 불가) false 를 돌려 시나리오가 빈 측정으로 통과하지 못하게 합니다.
         */
        [[nodiscard]] bool readViewLagMax( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            float32 lagMax        = 0.0f;
            uint32  measuredCount = 0;
            for ( const SkirmishUnitComponent* pView : pManager->getComponentRegistry().getAll<SkirmishUnitComponent>() )
            {
                const float32 lag = pView->measureViewLag();
                if ( lag < 0.0f )
                    continue;
                lagMax = MathUtil::max( lagMax, lag );
                ++measuredCount;
            }
            if ( measuredCount == 0 )
                return false;
            outValue = static_cast<float64>( lagMax );
            return true;
        }
    } // namespace
} // namespace sw

namespace sw
{
    SkirmishUnitComponent::SkirmishUnitComponent()
        : _airHeight{ 2.5f }
        , _footprintFill{ 0.9f }
        , _unitFill{ 1.6f }
        , _lastPosition{ 0.0f, 0.0f, 0.0f }
        , _unitID{}
        , _pShownLook{ nullptr }
        , _modelWidth{ 1.0f }
        , _yaw{ MathUtil::kPi }
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

    SpawnStateReset SkirmishUnitComponent::resetToSpawnState()
    {
        if ( _bPlaced == SW_TRUE )
        {
            GameObject*    pOwner = getOwner();
            MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
            if ( pMesh != nullptr )
                pMesh->setVisible( false ); // 자리를 맞추며 보이게 한 것이다 — 맞추기 전에는 숨어 있다
        }
        _lastPosition = float3{ 0.0f, 0.0f, 0.0f };
        _unitID       = SlotHandle{};
        _pShownLook   = nullptr;
        _modelWidth   = 1.0f;
        _yaw          = MathUtil::kPi;
        _bPlaced      = SW_FALSE;
        return SpawnStateReset::Reset;
    }

    void SkirmishUnitComponent::assignUnit( GameObjectHandle gameMode, SlotHandle unitID, float32 modelWidth )
    {
        bindGameMode( gameMode );
        _unitID     = unitID;
        _modelWidth = modelWidth > 0.0f ? modelWidth : 1.0f;
        _pShownLook = nullptr;
        _yaw        = MathUtil::kPi; // 처음엔 카메라(남쪽) 쪽을 본다
        _bPlaced    = SW_FALSE;
    }

    float32 SkirmishUnitComponent::measureViewLag() const
    {
        const GameObject*        pOwner         = getOwner();
        const GameObjectManager* pManager       = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const MeshComponent*     pMesh          = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const bool               bNotMeasurable = pManager == nullptr || pMesh == nullptr || _bPlaced == SW_FALSE;
        if ( bNotMeasurable )
            return -1.0f;
        const SkirmishGameModeComponent* pGameMode = findGameMode<SkirmishGameModeComponent>();
        const RTSUnit*                   pUnit     = pGameMode != nullptr ? pGameMode->getWorld().findUnit( _unitID ) : nullptr;
        if ( pUnit == nullptr )
            return -1.0f;
        const float3  shown = pMesh->getLocalPosition();
        const float32 dx    = shown._x - pUnit->_position._x;
        const float32 dz    = shown._z - pUnit->_position._z;
        return MathUtil::sqrt( dx * dx + dz * dz );
    }

    void SkirmishUnitComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const SkirmishGameModeComponent* pGameMode = findGameMode<SkirmishGameModeComponent>();
        const RTSUnit*                   pUnit     = pGameMode != nullptr ? pGameMode->getWorld().findUnit( _unitID ) : nullptr;
        if ( pUnit == nullptr )
            return;

        // 크기 · 자리 — 모델 바닥이 원점이다. 건물은 지은 만큼 솟고, 광물은 남은 만큼 낮아진다. 공중 유닛은 떠 있다.
        const RTSUnitDef& def = *pUnit->_pDef;
        float3            position{};
        float3            scale{};
        if ( pUnit->isResource() || pUnit->isBuilding() )
        {
            const float32 size   = static_cast<float32>( def._footprint ) * _footprintFill / _modelWidth;
            float32       height = 1.0f;
            if ( pUnit->isResource() && def._resourceType != RTSResourceKind::Gas )
                height = 0.4f + 0.6f * ( def._resourceAmount > 0 ? static_cast<float32>( pUnit->_resourceLeft ) / static_cast<float32>( def._resourceAmount ) : 1.0f );
            else if ( pUnit->isBuilding() )
                height = 0.2f + 0.8f * MathUtil::clamp( pUnit->_buildProgress, 0.0f, 1.0f );
            scale    = float3{ size, size * height, size };
            position = float3{ pUnit->_position._x, 0.0f, pUnit->_position._z };
        }
        else
        {
            const float32 size = def._radius * 2.0f * _unitFill / _modelWidth;
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
        const shared_ptr<MaterialInstance>& look = pGameMode->findUnitLook( *pUnit, pGameMode->isSelected( _unitID ) );
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

namespace sw
{
    SW_AUTOMATION_WATCH( skirmishViewLagMax, "Skirmish.ViewLagMax", "Largest distance between a unit view mesh and the game mode's unit position", &readViewLagMax );
} // namespace sw

namespace sw
{

    void SkirmishUnitComponent::onRegister( GameObjectManager& manager )
    {
        GameModeBoundComponent::onRegister( manager );
        manager.getComponentRegistry().add<SkirmishUnitComponent>( this );
    }

    void SkirmishUnitComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SkirmishUnitComponent>( this );
        GameModeBoundComponent::onUnregister( manager );
    }
} // namespace sw
