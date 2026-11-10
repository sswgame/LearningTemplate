#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Weather/DecalRainWashSystem.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/DecalComponent.h"

#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

namespace sw
{
    namespace
    {
        struct DecalRainWashSystemInternal
        {
            static constexpr uint32 kAllLayers = 0xFFFFFFFFu;
            /** @brief 강수 날씨 값 이름 — 캐릭터 노출(`SurfaceExposureComponent::_precipitationValue` 기본)과 같은 칸이다. 이름은 생성자가 만든다(정적 초기화에는 이름 풀이 없다). */
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
        // 이 키로는 이 타입만 붙인다(`ensureFor`).
        return static_cast<DecalRainWashSystem*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    DecalRainWashSystem& DecalRainWashSystem::ensureFor( GameObjectManager& manager )
    {
        DecalRainWashSystem* pSystem = find( manager );
        if ( pSystem != nullptr )
            return *pSystem;
        unique_ptr<DecalRainWashSystem> system = make_unique<DecalRainWashSystem>();
        pSystem                                = system.get();
        const bool bAdded                      = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( system ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았고, 씬 데이터를 건드리지 않는다고 선언해 접근 충돌도 없다
        (void)bAdded;
        return *pSystem;
    }

    void DecalRainWashSystem::release( GameObjectManager& manager )
    {
        manager.removeFrameSystem( hashed_string( kFrameSystemKey ) );
    }

    void DecalRainWashSystem::runBeforeTick( GameObjectManager& manager, float32 deltaTime )
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
        const SessionStateComponent* pSession      = SessionStateComponent::findFirstOpen( &manager );
        const float32                precipitation = pSession != nullptr ? MathUtil::max( 0.0f, pSession->getWeather().computeValue( _precipitationValue ) ) : 0.0f;
        if ( precipitation <= 0.0f || seconds <= 0.0f )
            return;
        // 씻김이 데칼을 지울 수 있다 — 도는 목록은 핸들 사본이라 지워진 데칼을 다시 찾지 않는다.
        manager.forEachActiveRegistered<DecalComponent>( _listHandleScratch, [&manager, precipitation, seconds]( DecalComponent& decal )
        {
            if ( decal.getRainWashPerSecond() <= 0.0f )
                return; // 비가 씻지 않는 데칼(칠한 선 · 그을음)
            const GameObject* const pOwner = decal.getOwner();
            CharacterRayHit         roof;
            const bool              bSheltered = CharacterRayHit::raycast3D( manager, decal.getWorldPosition(), float3{ 0.0f, 1.0f, 0.0f }, kShelterDistance, DecalRainWashSystemInternal::kAllLayers,
                                                                pOwner != nullptr ? pOwner->getObjectID() : 0u, roof );
            if ( bSheltered == false )
                decal.applyRainWash( precipitation, seconds );
        } );
    }
} // namespace sw
