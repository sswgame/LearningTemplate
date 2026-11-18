#include "pch.h"

#include "Games/Empty/KitDemo/AsymmetricHorrorDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorMatch.h"

namespace sw
{
    namespace
    {
        struct AsymmetricHorrorDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDAH" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kKillerID       = "trapper"; ///< 카탈로그의 살인마 id
            static constexpr uint32      kSeed           = 7;         ///< 같은 판이 나오게 시드를 고정한다
            static constexpr float32     kFixedStep      = 0.1f;      ///< 판의 고정 걸음(초)
            static constexpr int32       kGeneratorCount = 2;         ///< 카탈로그의 `generatorsRequired` 와 같다
            static constexpr int32       kActivitySteps  = 12;        ///< 수리 · 문 열기 한 번에 흘리는 걸음 수(1.2 초 — 카탈로그 시간 1 초를 넘긴다)
            static constexpr int32       kNoWinningTeam  = -1;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoAsymmetricHorrorState, "KitDemo.AsymmetricHorror.State", "Asymmetric horror demo state (0 not started, 1 match run finished)", AsymmetricHorrorDemoComponent,
                                &AsymmetricHorrorDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoAsymmetricHorrorGenerators, "KitDemo.AsymmetricHorror.Generators", "Generators repaired in the match", AsymmetricHorrorDemoComponent,
                                &AsymmetricHorrorDemoComponent::getCompletedGenerators );
    SW_AUTOMATION_GETTER_WATCH( kitDemoAsymmetricHorrorEscaped, "KitDemo.AsymmetricHorror.Escaped", "Survivors who escaped through the gate", AsymmetricHorrorDemoComponent,
                                &AsymmetricHorrorDemoComponent::getEscapedCount );
    SW_AUTOMATION_GETTER_WATCH( kitDemoAsymmetricHorrorWinner, "KitDemo.AsymmetricHorror.WinningTeam", "Winning team index (-1 none, 0 survivors, 1 killer)", AsymmetricHorrorDemoComponent,
                                &AsymmetricHorrorDemoComponent::getWinningTeam );

    AsymmetricHorrorDemoComponent::AsymmetricHorrorDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _completedGenerators{ 0 }
        , _escapedCount{ 0 }
        , _winningTeam{ AsymmetricHorrorDemoComponentInternal::kNoWinningTeam }
    {
    }

    AsymmetricHorrorDemoComponent::~AsymmetricHorrorDemoComponent() = default;

    bool AsymmetricHorrorDemoComponent::runMatch()
    {
        AsymmetricHorrorMatchSettings settings;
        settings._killerID  = hashed_string( AsymmetricHorrorDemoComponentInternal::kKillerID );
        settings._fixedStep = AsymmetricHorrorDemoComponentInternal::kFixedStep;
        settings._seed      = AsymmetricHorrorDemoComponentInternal::kSeed;
        AsymmetricHorrorMatch match;
        if ( match.initialize( settings, _pCatalog.get() ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.AsymmetricHorrorOpenFailed", "Asymmetric horror demo: the catalog has no killer '%#' - the match does not open", AsymmetricHorrorDemoComponentInternal::kKillerID );
            return false;
        }
        // 생존자 · 발전기 · 탈출문을 모두 원점에 둔다 — 상호작용 거리 안이라 걷지 않고 손을 댄다.
        const expected<int32> survivor = match.addSurvivor( float3{} );
        if ( survivor.has_value() == false )
            return false;
        for ( int32 index = 0; index < AsymmetricHorrorDemoComponentInternal::kGeneratorCount; ++index )
        {
            if ( match.addGenerator( float3{} ).has_value() == false )
                return false;
        }
        const expected<int32> gate = match.addGate( float3{} );
        if ( gate.has_value() == false )
            return false;
        match.start();

        const auto advance = [&match]()
        {
            for ( int32 step = 0; step < AsymmetricHorrorDemoComponentInternal::kActivitySteps; ++step )
            {
                match.update( AsymmetricHorrorDemoComponentInternal::kFixedStep );
            }
        };
        // 발전기를 하나씩 고쳐 필요한 수를 채우면 전원이 들어온다.
        for ( int32 generator = 0; generator < AsymmetricHorrorDemoComponentInternal::kGeneratorCount; ++generator )
        {
            if ( match.startRepair( survivor.value(), generator ) == false )
                return false;
            advance();
        }
        // 전원 뒤에 문을 열고, 열린 문 옆에서 나간다 — 모두 나가면 생존자 팀이 이긴다.
        if ( match.startOpenGate( survivor.value(), gate.value() ) == false )
            return false;
        advance();
        (void)match.escape( survivor.value() );

        _completedGenerators = match.getCompletedGeneratorCount();
        _escapedCount        = 0;
        for ( int32 index = 0; index < match.getSurvivorCount(); ++index )
        {
            const AsymmetricHorrorSurvivor* pSurvivor = match.findSurvivor( index );
            _escapedCount += pSurvivor != nullptr && pSurvivor->_state == SurvivorState::Escaped ? 1 : 0;
        }
        _winningTeam = match.getMatch().getWinningTeam();
        markRunFinished();
        return true;
    }

    bool AsymmetricHorrorDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runMatch();
    }

    void AsymmetricHorrorDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.AsymmetricHorrorDone", "[KitDemo] asymmetric horror: %# generators repaired, %# escaped, team %# won", _completedGenerators, _escapedCount, _winningTeam );
    }

    void AsymmetricHorrorDemoComponent::serializeState( StateIO& io )
    {
        serializeResults( io, AsymmetricHorrorDemoComponentInternal::kStateTag, AsymmetricHorrorDemoComponentInternal::kStateVersion, { &_completedGenerators, &_escapedCount, &_winningTeam } );
    }
} // namespace sw
