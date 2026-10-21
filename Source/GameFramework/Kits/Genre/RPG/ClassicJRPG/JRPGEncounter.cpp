#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGEncounter.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    JRPGEncounterWalker::JRPGEncounterWalker()
        : _random{}
        , _pCatalog{ nullptr }
        , _counter{}
    {
    }

    void JRPGEncounterWalker::initialize( const JRPGCatalog* pCatalog, uint32 seed )
    {
        _pCatalog = pCatalog;
        _random.setSeed( seed );
        _counter.reset();
    }

    const JRPGEncounterGroup* JRPGEncounterWalker::step( const hashed_string& areaID )
    {
        _counter.advance();
        const JRPGAreaDef* pArea = _pCatalog != nullptr ? _pCatalog->findArea( areaID ) : nullptr;
        if ( pArea == nullptr || pArea->_listGroup.empty() )
            return nullptr;
        // 판정은 기반 걸음 조우 규칙(확률 + 유예)이다. 걸음마다 난수를 하나 쓰고, 유예 중에는 쓰지 않는다.
        StepEncounterRule rule;
        rule._mode       = StepEncounterMode::Chance;
        rule._rate       = pArea->_rate;
        rule._graceSteps = pArea->_graceSteps;
        if ( _counter.roll( rule, &_random ) == false )
            return nullptr;

        const int32 groupIndex = _random.pickWeightedIndexInt( pArea->_listGroup, []( const JRPGEncounterGroup& group )
        { return group._weight; } );
        if ( groupIndex < 0 )
            return nullptr;
        _counter.markEncounter();
        return pArea->_listGroup.data() + groupIndex;
    }

    void JRPGEncounterWalker::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _counter.getStepsSinceEncounter();
        outArchive << _counter.getTotalSteps();
    }

    bool JRPGEncounterWalker::readState( Archive& archive )
    {
        GameRandom random;
        int32      stepsSinceEncounter = 0;
        int32      totalSteps          = 0;
        if ( StateArchiveUtil::readRandom( archive, random ) == false )
            return false;
        archive >> stepsSinceEncounter;
        archive >> totalSteps;
        StepEncounterCounter counter;
        if ( archive.isOk() == false || counter.restore( stepsSinceEncounter, totalSteps ) == false )
            return false;
        _random  = random;
        _counter = counter;
        return true;
    }
} // namespace sw
