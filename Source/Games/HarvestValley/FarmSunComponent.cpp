#include "pch.h"

#include "Games/HarvestValley/FarmSunComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

namespace sw
{
    FarmSunComponent::FarmSunComponent()
        : _director{}
        , _rainDimming{ 0.55f }
        , _nightIntensity{ 0.2f }
    {
        setCanEverTick( true );
    }

    void FarmSunComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 시간을 흘린 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void FarmSunComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                pOwner   = getOwner();
        GameObjectManager*         pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        DirectionalLightComponent* pSun     = pOwner != nullptr ? pOwner->getComponent<DirectionalLightComponent>() : nullptr;
        if ( pManager == nullptr || pSun == nullptr )
            return;
        const FarmDirectorComponent* pDirector = GameDirectorComponent::resolve<FarmDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr )
            return;
        const float32 hour     = pDirector->getHourOfDay();
        const float32 dayRatio = MathUtil::clamp( ( hour - 6.0f ) / 14.0f, 0.0f, 1.0f );
        const float32 height   = MathUtil::sin( dayRatio * MathUtil::kPi );
        const float32 light    = ( 0.25f + 1.25f * height ) * ( pDirector->isRaining() ? _rainDimming : 1.0f );
        pSun->setIntensity( hour > 20.0f || hour < 5.0f ? _nightIntensity : light ); // 자정을 넘긴 밤(쓰러지기 전 2 시까지)도 밤
        pSun->setLocalRotation( float3{ 0.25f + 1.1f * height, -1.2f + 2.4f * dayRatio, 0.0f } );
    }
} // namespace sw
