#include "pch.h"

#include "Games/Empty/KitDemo/MonsterCollectorDemoComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterBattle.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterInstance.h"
#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"

namespace sw
{
    namespace
    {
        struct MonsterCollectorDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDMC" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr uint32  kSeed       = 7;    ///< 같은 판이 나오게 시드를 고정한다(급소 난수)
            static constexpr int32   kLevel      = 50;   ///< 모든 개체의 레벨
            static constexpr int32   kWeakFoeHP  = 1;    ///< 한 번에 쓰러지게 깎아 둔 야생 개체의 체력
            static constexpr float32 kSureBall   = 3.0f; ///< 포획률 255 와 함께 반드시 잡는 볼 배율
            static constexpr int32   kStrikeSlot = 0;    ///< 처음 배운 기술의 슬롯
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoMonsterCollectorState, "KitDemo.MonsterCollector.State", "Monster collector demo state (0 not started, 1 battle and capture finished)",
                                MonsterCollectorDemoComponent, &MonsterCollectorDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMonsterCollectorWonExp, "KitDemo.MonsterCollector.WonExp", "Exp gained by winning the wild battle (0 if not won)", MonsterCollectorDemoComponent,
                                &MonsterCollectorDemoComponent::getWonExp );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMonsterCollectorCaptured, "KitDemo.MonsterCollector.Captured", "1 if the capture battle ended with the foe caught", MonsterCollectorDemoComponent,
                                &MonsterCollectorDemoComponent::getCaptured );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMonsterCollectorShakes, "KitDemo.MonsterCollector.CaptureShakes", "Ball shakes in the capture battle", MonsterCollectorDemoComponent,
                                &MonsterCollectorDemoComponent::getCaptureShakes );

    MonsterCollectorDemoComponent::MonsterCollectorDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _wonExp{ 0 }
        , _captured{ 0 }
        , _captureShakes{ 0 }
    {
    }

    MonsterCollectorDemoComponent::~MonsterCollectorDemoComponent() = default;

    bool MonsterCollectorDemoComponent::runBattleAndCapture()
    {
        const MonsterCollectorCatalog& catalog = *_pCatalog.get();
        // 개체값 0 · 성격 없음 · HP 가득인 개체를 만든다(키트 단위 테스트의 `make` 와 같은 모양).
        const auto makeMonster = [&catalog]( const hashed_string& speciesID, const hashed_string& moveID, MonsterInstance& outMonster )
        {
            const MonsterSpeciesDef* pSpecies = catalog.findSpecies( speciesID );
            if ( pSpecies == nullptr )
                return false;
            outMonster._speciesID = speciesID;
            outMonster._level     = MonsterCollectorDemoComponentInternal::kLevel;
            outMonster._nickname  = pSpecies->_name;
            outMonster._exp       = MonsterCollectorCatalog::computeTotalExp( pSpecies->_expGroup, MonsterCollectorDemoComponentInternal::kLevel );
            MonsterInstance::recomputeStats( catalog, outMonster );
            return moveID.empty() || MonsterInstance::learnMove( catalog, outMonster, moveID );
        };

        MonsterInstance fastMonster;
        MonsterInstance weakFoe;
        MonsterInstance leadMonster;
        MonsterInstance wildFoe;
        const bool      bMade = makeMonster( hashed_string( "swift" ), hashed_string( "strike" ), fastMonster ) && makeMonster( hashed_string( "normie" ), hashed_string( "strike" ), weakFoe ) && makeMonster( hashed_string( "normie" ), hashed_string{}, leadMonster ) && makeMonster( hashed_string( "flamo" ), hashed_string{}, wildFoe );
        if ( bMade == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.MonsterCollectorMissingSpecies", "Monster collector demo: the catalog lacks species swift / normie / flamo or the move strike - the battle does not open" );
            return false;
        }
        weakFoe._hp = MonsterCollectorDemoComponentInternal::kWeakFoeHP;

        ElementChart chart;
        chart.addElement( hashed_string( "Normal" ) );
        chart.addElement( hashed_string( "Fire" ) );
        MonsterBattle battle;
        battle.initialize( &catalog, &chart, MonsterCollectorDemoComponentInternal::kSeed );

        // 전투: 빠른 swift 가 먼저 쳐서 체력 1 인 야생 normie 를 쓰러뜨린다 — 이기고 경험치(기본 경험치 x 레벨 / 7)를 받는다.
        vector<MonsterInstance> listPlayer;
        vector<MonsterInstance> listFoe;
        listPlayer.push_back( fastMonster );
        listFoe.push_back( weakFoe );
        battle.start( listPlayer, listFoe, true );
        const int64 expBefore = battle.getActive( MonsterBattle::kPlayerSide )._exp;
        if ( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeMove( MonsterCollectorDemoComponentInternal::kStrikeSlot ) ) == false )
            return false;
        battle.resolveRound();
        if ( battle.getResult() == MonsterBattleResult::Won )
            _wonExp = static_cast<int32>( battle.getActive( MonsterBattle::kPlayerSide )._exp - expBefore );

        // 포획: 포획률 255 인 야생 flamo 에 볼 x3 을 던지면 세 번 흔들리고 잡힌다.
        vector<MonsterBattleEvent> listEvent;
        battle.pollEvents( listEvent ); // 앞 전투의 이벤트를 비운다
        listEvent.clear();
        listPlayer.clear();
        listFoe.clear();
        listPlayer.push_back( leadMonster );
        listFoe.push_back( wildFoe );
        battle.start( listPlayer, listFoe, true );
        if ( battle.setAction( MonsterBattle::kPlayerSide, MonsterAction::makeBall( MonsterCollectorDemoComponentInternal::kSureBall ) ) == false )
            return false;
        battle.resolveRound();
        battle.pollEvents( listEvent );
        _captured      = battle.getResult() == MonsterBattleResult::Captured ? 1 : 0;
        _captureShakes = VectorUtil::countBy<&MonsterBattleEvent::_kind>( listEvent, MonsterBattleEvent::Kind::CaptureShake );
        markRunFinished();
        return true;
    }

    bool MonsterCollectorDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runBattleAndCapture();
    }

    void MonsterCollectorDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.MonsterCollectorDone", "[KitDemo] monster collector: won %# exp, captured %# after %# shakes", _wonExp, _captured, _captureShakes );
    }

    void MonsterCollectorDemoComponent::serializeState( StateIO& io )
    {
        serializeResults( io, MonsterCollectorDemoComponentInternal::kStateTag, MonsterCollectorDemoComponentInternal::kStateVersion, { &_wonExp, &_captured, &_captureShakes } );
    }
} // namespace sw
