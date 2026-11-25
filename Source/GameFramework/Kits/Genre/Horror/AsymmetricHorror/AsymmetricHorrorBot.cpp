#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorBot.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorMatch.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"

namespace sw
{
    namespace
    {
        struct AsymmetricHorrorBotInternal
        {
            static constexpr float32 kArriveRatio     = 0.8f; ///< 상호작용 거리의 이 비율 안까지 다가가 선다(경계에서 걸음 오차로 거절되지 않게)
            static constexpr float32 kBlockedMove     = 0.5f; ///< 무대 경계가 깎은 달아날 방향이 이보다 짧으면 옆으로 돈다
            static constexpr float32 kLoseSightMargin = 2.0f; ///< 보던 대상은 시야 거리에 이만큼 더한 곳까지 놓치지 않는다

            static float3 directionTo( const float3& from, const float3& to )
            {
                const float3  delta{ to._x - from._x, 0.0f, to._z - from._z };
                const float32 length = delta.getLength();
                return length > MathUtil::kEpsilon ? delta / length : float3{};
            }

            /** @brief @p listPosition 에서 @p from 에 가장 가까운 자리의 번호입니다. 비었으면 없음입니다. */
            static optional<int32> findNearest( const vector<float3>& listPosition, const float3& from )
            {
                optional<int32> nearest{};
                float32         best = MathUtil::kMaxFloat;
                for ( size_t index = 0; index < listPosition.size(); ++index )
                {
                    const float32 distance = float3::getHorizontalDistance( from, listPosition[index] );
                    if ( distance >= best )
                        continue;
                    best    = distance;
                    nearest = static_cast<int32>( index );
                }
                return nearest;
            }

            /** @brief 곁이면(@p bIdle 일 때) 쓰기를 누르고, 멀면 @p target 으로 걷는 명령입니다. */
            static void approach( AsymmetricHorrorCommand& outCommand, const float3& position, const float3& target, float32 range, bool bIdle )
            {
                if ( float3::getHorizontalDistance( position, target ) <= range * kArriveRatio )
                {
                    outCommand._bUseTriggered = bIdle ? SW_TRUE : SW_FALSE;
                    return;
                }
                const float3 direction = directionTo( position, target );
                outCommand._move       = float2{ direction._x, direction._z };
            }

            /** @brief 달아날 방향입니다. 경계에 막히면 무대 가운데 쪽으로 도는 옆 방향을 고릅니다. */
            static float3 computeFleeDirection( const AsymmetricHorrorStage& stage, const float3& position, const float3& killer )
            {
                const float3 away    = directionTo( killer, position );
                const float3 limited = stage.limitMove( position, away );
                if ( limited.getLength() >= kBlockedMove )
                    return limited;
                const float3 side{ away._z, 0.0f, -away._x };
                const bool   bTowardCenter = side._x * position._x + side._z * position._z <= 0.0f;
                return bTowardCenter ? side : side * -1.0f;
            }

            static void moveKillerTo( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, const float3& target, float32 deltaTime )
            {
                const float3 position = inoutMatch.getKiller()._position;
                inoutMatch.moveKiller( stage.limitMove( position, directionTo( position, target ) ), deltaTime );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AsymmetricHorrorBot::AsymmetricHorrorBot()
        : _settings{}
        , _perception{}
        , _listStimulus{}
    {
    }

    void AsymmetricHorrorBot::initialize( const AsymmetricHorrorBotSettings& settings )
    {
        _settings = settings;
        AIPerceptionSettings perception;
        perception._sightRange     = settings._sightRange;
        perception._loseSightRange = settings._sightRange + AsymmetricHorrorBotInternal::kLoseSightMargin;
        perception._memoryDuration = settings._memorySeconds;
        _perception.setSettings( perception );
        _perception.clearAll();
    }

    AsymmetricHorrorCommand AsymmetricHorrorBot::decideSurvivor( const AsymmetricHorrorMatch& match, const AsymmetricHorrorStage& stage, int32 survivor ) const
    {
        using Internal = AsymmetricHorrorBotInternal;
        AsymmetricHorrorCommand               command;
        const AsymmetricHorrorSurvivor* const pSelf = match.findSurvivor( survivor );
        if ( pSelf == nullptr )
            return command;
        if ( pSelf->_state == SurvivorState::Carried || pSelf->_state == SurvivorState::Hooked )
        {
            command._bUseHeld = SW_TRUE;
            return command;
        }
        if ( pSelf->canAct() == false )
            return command;

        const float3& position = pSelf->_position;
        const float3& killer   = match.getKiller()._position;
        if ( float3::getHorizontalDistance( position, killer ) <= _settings._fleeRange )
        {
            const float3 flee = Internal::computeFleeDirection( stage, position, killer );
            command._move     = float2{ flee._x, flee._z };
            return command;
        }
        const InteractionProgress* const pProgress  = match.findActivityProgress( survivor );
        const bool                       bSkillTime = pProgress != nullptr && pProgress->hasPendingSkillCheck() && pProgress->getSkillCheckActor() == static_cast<uint32>( survivor ) &&
                                pProgress->getTime() >= pProgress->getSkillCheckTargetTime();
        if ( bSkillTime )
        {
            command._bUseTriggered = SW_TRUE;
            return command;
        }

        const float32 range = match.getInteractRange();
        const bool    bIdle = pSelf->_activity == SurvivorActivity::None;
        for ( int32 index = 0; index < match.getSurvivorCount(); ++index )
        {
            const AsymmetricHorrorSurvivor& other     = *match.findSurvivor( index );
            const bool                      bNeedHelp = index != survivor && ( other._state == SurvivorState::Hooked || other._state == SurvivorState::Dying );
            if ( bNeedHelp == false )
                continue;
            Internal::approach( command, position, other._position, range, bIdle );
            return command;
        }
        if ( match.isHatchOpen() )
        {
            Internal::approach( command, position, stage._hatch, range, bIdle );
            return command;
        }
        if ( match.areGatesPowered() )
        {
            // 열린 탈출구를 먼저, 없으면 가장 가까운 탈출구를 연다.
            optional<int32> gate = Internal::findNearest( stage._listGate, position );
            for ( int32 index = 0; index < match.getGateCount(); ++index )
            {
                if ( match.findGate( index )->_progress.isCompleted() )
                    gate = index;
            }
            if ( gate.has_value() )
                Internal::approach( command, position, stage._listGate[static_cast<size_t>( *gate )], range, bIdle );
            return command;
        }
        const AsymmetricHorrorGenerator* pNearest = nullptr;
        float32                          best     = MathUtil::kMaxFloat;
        for ( int32 index = 0; index < match.getGeneratorCount(); ++index )
        {
            const AsymmetricHorrorGenerator& generator  = *match.findGenerator( index );
            const bool                       bCanRepair = generator._progress.isCompleted() == false && generator._bBlocked == SW_FALSE;
            const float32                    distance   = float3::getHorizontalDistance( position, generator._position );
            if ( bCanRepair == false || distance >= best )
                continue;
            best     = distance;
            pNearest = &generator;
        }
        if ( pNearest != nullptr )
            Internal::approach( command, position, pNearest->_position, range, bIdle );
        return command;
    }

    void AsymmetricHorrorBot::stepKiller( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, float32 deltaTime )
    {
        const AsymmetricHorrorKiller& killer = inoutMatch.getKiller();
        inoutMatch.collectKillerStimuli( _listStimulus );
        _perception.sense( killer._position, killer._forward, _listStimulus, nullptr, deltaTime );
        const bool bBusy = killer.canAct() == false || inoutMatch.isEnded();
        if ( bBusy || handleDowned( inoutMatch, stage, deltaTime ) || chase( inoutMatch, stage, deltaTime ) )
            return;
        patrol( inoutMatch, stage, deltaTime );
    }

    bool AsymmetricHorrorBot::handleDowned( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, float32 deltaTime ) const
    {
        using Internal                       = AsymmetricHorrorBotInternal;
        const AsymmetricHorrorKiller& killer = inoutMatch.getKiller();
        if ( killer._carrying >= 0 )
        {
            const optional<int32> hook = Internal::findNearest( stage._listHook, killer._position );
            if ( hook.has_value() && inoutMatch.hookCarried( *hook ) == false )
                Internal::moveKillerTo( inoutMatch, stage, stage._listHook[static_cast<size_t>( *hook )], deltaTime );
            return true;
        }
        for ( int32 index = 0; index < inoutMatch.getSurvivorCount(); ++index )
        {
            const AsymmetricHorrorSurvivor& survivor = *inoutMatch.findSurvivor( index );
            const bool                      bInSight = survivor._state == SurvivorState::Dying && float3::getHorizontalDistance( killer._position, survivor._position ) <= _settings._sightRange;
            if ( bInSight == false || stage._listHook.empty() )
                continue;
            if ( inoutMatch.pickUp( index ) == false )
                Internal::moveKillerTo( inoutMatch, stage, survivor._position, deltaTime );
            return true;
        }
        if ( inoutMatch.isHatchOpen() == false )
            return false;
        if ( inoutMatch.closeHatch() == false )
            Internal::moveKillerTo( inoutMatch, stage, stage._hatch, deltaTime );
        return true;
    }

    bool AsymmetricHorrorBot::chase( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, float32 deltaTime ) const
    {
        using Internal                         = AsymmetricHorrorBotInternal;
        const AsymmetricHorrorKiller& killer   = inoutMatch.getKiller();
        const AIPerceivedTarget*      pNearest = nullptr;
        float32                       best     = MathUtil::kMaxFloat;
        for ( const AIPerceivedTarget& target : _perception.getTargets() )
        {
            // 자극의 id 는 생존자 번호 + 1 이다(`collectKillerStimuli`).
            const AsymmetricHorrorSurvivor* const pSurvivor  = inoutMatch.findSurvivor( static_cast<int32>( target._id ) - 1 );
            const bool                            bCanAttack = pSurvivor != nullptr && ( pSurvivor->_state == SurvivorState::Healthy || pSurvivor->_state == SurvivorState::Injured );
            const float32                         distance   = float3::getHorizontalDistance( killer._position, target._lastKnownPosition );
            if ( bCanAttack == false || distance >= best )
                continue;
            best     = distance;
            pNearest = &target;
        }
        if ( pNearest == nullptr )
            return false;
        if ( pNearest->_bSeen == SW_FALSE )
        {
            // 놓친 자리 곁의 사물함을 뒤진다.
            for ( size_t locker = 0; locker < stage._listLocker.size(); ++locker )
            {
                if ( float3::getHorizontalDistance( killer._position, stage._listLocker[locker] ) > inoutMatch.getInteractRange() )
                    continue;
                (void)inoutMatch.searchLocker( static_cast<int32>( locker ) ); // 비었으면 뒤지는 시간만 든다
                return true;
            }
        }
        const bool bSeen    = pNearest->_bSeen == SW_TRUE;
        const bool bInReach = bSeen && best <= killer._pDef->_lungeRange;
        if ( bSeen && bInReach == false )
            (void)inoutMatch.useKillerAbility(); // 보이는데 닿지 않으면 쓴다 — 쿨다운 중이거나 능력이 없으면 거절한다
        Internal::moveKillerTo( inoutMatch, stage, pNearest->_lastKnownPosition, deltaTime );
        if ( bInReach )
            (void)inoutMatch.killerAttack(); // 쿨다운 중이면 거절한다
        return true;
    }

    void AsymmetricHorrorBot::patrol( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, float32 deltaTime ) const
    {
        for ( size_t pallet = 0; pallet < stage._listPallet.size(); ++pallet )
        {
            if ( inoutMatch.breakPallet( static_cast<int32>( pallet ) ) )
                return;
        }
        const int32 generatorCount = inoutMatch.getGeneratorCount();
        for ( int32 index = 0; index < generatorCount; ++index )
        {
            if ( inoutMatch.kickGenerator( index ) )
                return;
        }
        if ( generatorCount == 0 )
            return;
        const uint32                     turn   = inoutMatch.getTick() / static_cast<uint32>( _settings._patrolSteps );
        const AsymmetricHorrorGenerator& target = *inoutMatch.findGenerator( static_cast<int32>( turn % static_cast<uint32>( generatorCount ) ) );
        if ( float3::getHorizontalDistance( inoutMatch.getKiller()._position, target._position ) > inoutMatch.getInteractRange() )
            AsymmetricHorrorBotInternal::moveKillerTo( inoutMatch, stage, target._position, deltaTime );
    }
} // namespace sw

namespace sw
{
    bool AsymmetricHorrorStage::populate( AsymmetricHorrorMatch& inoutMatch ) const
    {
        const bool bMissingPlace = _listSurvivorStart.empty() || _listGenerator.empty() || _listGate.empty();
        if ( bMissingPlace || _halfExtent._x <= 0.0f || _halfExtent._y <= 0.0f )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorrorStage.Incomplete", "AsymmetricHorrorStage: the stage needs survivors (%#), generators (%#), gates (%#) and a positive half extent (%#, %#)",
                             _listSurvivorStart.size(), _listGenerator.size(), _listGate.size(), _halfExtent._x, _halfExtent._y );
            return false;
        }
        bool bAccepted = true;
        for ( const float3& position : _listSurvivorStart )
        {
            bAccepted = inoutMatch.addSurvivor( position ).has_value() && bAccepted;
        }
        for ( const float3& position : _listGenerator )
        {
            bAccepted = inoutMatch.addGenerator( position ).has_value() && bAccepted;
        }
        for ( const float3& position : _listGate )
        {
            bAccepted = inoutMatch.addGate( position ).has_value() && bAccepted;
        }
        if ( bAccepted == false )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorrorStage.Refused", "AsymmetricHorrorStage: the match refused a survivor, generator or gate - check the health, repair, heal and gate rules of the catalog" );
            return false;
        }
        for ( const float3& position : _listHook )
        {
            (void)inoutMatch.addHook( position ); // 번호는 목록 순서 그대로다
        }
        for ( const float3& position : _listPallet )
        {
            (void)inoutMatch.addPallet( position );
        }
        for ( const float3& position : _listWindow )
        {
            (void)inoutMatch.addWindow( position );
        }
        for ( const float3& position : _listLocker )
        {
            (void)inoutMatch.addLocker( position );
        }
        inoutMatch.setKillerPosition( _killerStart );
        inoutMatch.setHatchPosition( _hatch );
        return true;
    }

    float3 AsymmetricHorrorStage::limitMove( const float3& position, const float3& direction ) const
    {
        float3     limited{ direction._x, 0.0f, direction._z };
        const bool bLeavesX = ( position._x >= _halfExtent._x && limited._x > 0.0f ) || ( position._x <= -_halfExtent._x && limited._x < 0.0f );
        const bool bLeavesZ = ( position._z >= _halfExtent._y && limited._z > 0.0f ) || ( position._z <= -_halfExtent._y && limited._z < 0.0f );
        if ( bLeavesX )
            limited._x = 0.0f;
        if ( bLeavesZ )
            limited._z = 0.0f;
        return limited;
    }
} // namespace sw
