#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Weather/DecalRainWashSystem.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Visual/Component/DecalComponent.h"

#include "GameFramework/Runtime/Foundation/Service/FrameSystemUtil.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/World/Environment/WeatherShelter.h"

namespace sw
{
    namespace
    {
        struct DecalRainWashSystemInternal
        {
            /** @brief 강수 날씨 값 이름 — 캐릭터 노출(`CharacterWetnessComponent::_precipitationValue` 기본)과 같은 칸이다. 이름은 생성자가 만든다(정적 초기화에는 이름 풀이 없다). */
            static constexpr const utf8* kPrecipitationValue = "wetness";
        };
    } // namespace

    DecalRainWashSystem::DecalRainWashSystem()
        : _listHandleScratch{}
        , _pendingSeconds{ 0.0f }
        , _precipitationValue{ DecalRainWashSystemInternal::kPrecipitationValue }
    {
    }

    DecalRainWashSystem* DecalRainWashSystem::find( const GameObjectManager& manager )
    {
        return FrameSystemUtil::find<DecalRainWashSystem>( manager );
    }

    DecalRainWashSystem& DecalRainWashSystem::getOrCreate( GameObjectManager& manager )
    {
        return FrameSystemUtil::getOrCreate<DecalRainWashSystem>( manager );
    }

    void DecalRainWashSystem::release( GameObjectManager& manager )
    {
        FrameSystemUtil::remove<DecalRainWashSystem>( manager );
    }

    void DecalRainWashSystem::runMain( GameObjectManager& manager, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        _pendingSeconds += deltaTime;
        if ( _pendingSeconds < kWashIntervalSeconds )
            return;
        const float32 seconds = _pendingSeconds;
        _pendingSeconds       = 0.0f;
        // 일은 시스템을 붙잡지 않고 매니저에서 다시 찾는다 — 큐가 돌기 전에 날씨가 닫혀 시스템이 떨어졌을 수 있다.
        GameObjectManager* pManager = &manager;
        manager.deferPostTick( [pManager, seconds]()
        {
            DecalRainWashSystem* pSystem = find( *pManager );
            if ( pSystem != nullptr )
                pSystem->runWashPass( *pManager, seconds );
        } );
    }

    SceneFrameAccess DecalRainWashSystem::getFrameAccess() const
    {
        // 감사: 틱 뒤 큐에 일을 넣을 뿐이다. 붙인 순서를 지키려고 게임 스레드에 둔다(레인 안전을 선언하지 않는다).
        return SceneFrameAccess{};
    }

    void DecalRainWashSystem::runWashPass( GameObjectManager& manager, float32 seconds )
    {
        // 비: 날씨 값 × 하늘이 열렸는지. 날씨가 없는 씬은 비가 없다(판 상태가 날씨를 열지 않았다).
        const float32 precipitation = MathUtil::max( 0.0f, GameStateComponent::readWeather( manager, _precipitationValue ) );
        if ( precipitation <= 0.0f || seconds <= 0.0f )
            return;
        // 씻김이 데칼을 지울 수 있다 — 도는 목록은 핸들 사본이라 지워진 데칼을 다시 찾지 않는다.
        manager.forEachActiveRegistered<DecalComponent>( _listHandleScratch, [&manager, precipitation, seconds]( DecalComponent& decal )
        {
            if ( decal.getRainWashPerSecond() <= 0.0f )
                return; // 비가 씻지 않는 데칼(칠한 선 · 그을음)
            const GameObject* const pOwner = decal.getOwner();
            if ( WeatherShelter::isSheltered( manager, decal.getWorldPosition(), WeatherShelter::kShelterDistance, pOwner != nullptr ? pOwner->getObjectID() : 0u ) == false )
                decal.applyRainWash( precipitation, seconds );
        } );
    }
} // namespace sw
