// GimmickSignalTest — 기믹의 연출 신호가 큐 표의 연출을 걸고 이벤트도 내는지, 소리가 AI 자극 색인에 걸렸다 다음 프레임에 거둬지는지 본다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Name/TagID.h"
#include "Core/Name/hashed_string.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/AI/AIPerception.h"
#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"
#include "GameFramework/Runtime/Character/AI/NoiseEmitterComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IAbilityCueSink.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"

using namespace sw;

namespace
{
    /** @brief 받은 연출을 세는 창구입니다(프리팹 · 소리를 내지 않는다). */
    class RecordingCueSink final : public IAbilityCueSink
    {
    public:
        int32  _presentCount{ 0 };
        TagID  _lastTag{};
        float3 _lastPosition{};

        void presentCue( GameObjectManager& manager, const AbilityPresentationDef& cueDef, const float3& position ) override
        {
            (void)manager;
            ++_presentCount;
            _lastTag      = cueDef._cueTag;
            _lastPosition = position;
        }
    };
} // namespace

/**
 * @brief [GimmickSignalTest] 연출 신호는 큐 표에 있으면 그 연출을 신호 자리 + 오프셋에 걸고, 표에 없으면 연출 없이 지나가며, 둘 다 "game" 채널 이벤트는 낸다
 */
SW_TEST_CASE( GimmickSignalTest, CuePresentsFromTheCueTableAndSendsTheEvent )
{
    AbilityCatalog         catalog;
    AbilityPresentationDef blastCue;
    blastCue._cueTag = TagID::request( "Explosion" );
    blastCue._offset = float3{ 0.0f, 1.0f, 0.0f };
    catalog.addCue( blastCue );
    RecordingCueSink sink;
    EventDispatcher  dispatcher;
    SubsystemScope   scope;
    SW_REQUIRE_TRUE( scope.bind<const AbilityCatalog>( &catalog ) );
    SW_REQUIRE_TRUE( scope.bind<IAbilityCueSink>( &sink ) );
    SW_REQUIRE_TRUE( scope.bind<EventDispatcher>( &dispatcher ) );
    game::ScopedGameScope scopedScope( scope );

    int32              eventCount   = 0;
    const Subscription subscription = dispatcher.subscribe<GimmickCueEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const GimmickCueEvent& )>, [&eventCount]( const GimmickCueEvent& )
    { ++eventCount; } ) );

    GameObjectManager manager;
    GameObject*       pBarrel = manager.createGameObject( hashed_string( "Barrel" ) );
    GimmickUtil::sendCue( *pBarrel, hashed_string( "Explosion" ), float3{ 2.0f, 0.0f, 0.0f }, 10.0f ); // 틱 밖이라 연출도 바로 건다
    SW_EXPECT_EQUAL( 1, eventCount );
    SW_EXPECT_EQUAL( 1, sink._presentCount );
    SW_EXPECT_TRUE( sink._lastTag == blastCue._cueTag );
    SW_EXPECT_NEAR( 2.0f, sink._lastPosition._x, 1e-5f );
    SW_EXPECT_NEAR( 1.0f, sink._lastPosition._y, 1e-5f );

    GimmickUtil::sendCue( *pBarrel, hashed_string( "Unlisted" ), float3{}, 0.0f );
    SW_EXPECT_EQUAL( 2, eventCount );
    SW_EXPECT_EQUAL( 1, sink._presentCount );
}

/**
 * @brief [GimmickSignalTest] 소리 내기는 같은 오브젝트의 자극 원천에 반경을 걸어 다른 팀 감각의 질의에 잡히고, 다음 프레임 틱에서 거둬진다
 */
SW_TEST_CASE( GimmickSignalTest, NoiseReachesTheAIStimulusIndex )
{
    GameObjectManager manager;
    GameObject*       pFloor = manager.createGameObject( hashed_string( "SqueakyFloor" ) );
    pFloor->addComponent<SceneComponent>()->setLocalPosition( float3{ 15.0f, 0.0f, 0.0f } ); // 시야 상자(반 크기 10) 밖 — 소리로만 잡힌다
    SW_REQUIRE_NOT_NULL( pFloor->addComponent<AIStimulusSourceComponent>() );
    NoiseEmitterComponent* pEmitter = pFloor->addComponent<NoiseEmitterComponent>();
    SW_REQUIRE_NOT_NULL( pEmitter );
    manager.beginPlay();

    vector<AIStimulus> listStimulus;
    vector<Component*> listKey;
    pEmitter->emit( 8.0f ); // 틱 밖이라 색인을 바로 고친다
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, listStimulus, listKey );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStimulus.size() );
    SW_EXPECT_EQUAL( pFloor->getObjectID(), listStimulus[0]._id );
    SW_EXPECT_NEAR( 8.0f, listStimulus[0]._noiseRadius, 1e-5f );

    manager.update( 1.0f / 60.0f ); // 다음 프레임 — 지난 소리를 거둔다
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
}
