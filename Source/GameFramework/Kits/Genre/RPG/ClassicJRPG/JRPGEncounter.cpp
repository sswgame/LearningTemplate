#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGEncounter.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    JRPGEncounterWalker::JRPGEncounterWalker()
        : _random{}
        , _pCatalog{ nullptr }
        , _stepsSinceEncounter{ kNoEncounterYet }
        , _totalSteps{ 0 }
    {
    }

    void JRPGEncounterWalker::initialize( const JRPGCatalog* pCatalog, uint32 seed )
    {
        _pCatalog = pCatalog;
        _random.setSeed( seed );
        _stepsSinceEncounter = kNoEncounterYet;
        _totalSteps          = 0;
    }

    const JRPGEncounterGroup* JRPGEncounterWalker::step( const hashed_string& areaID )
    {
        ++_totalSteps;
        if ( _stepsSinceEncounter < kNoEncounterYet )
            ++_stepsSinceEncounter;
        const JRPGAreaDef* pArea = _pCatalog != nullptr ? _pCatalog->findArea( areaID ) : nullptr;
        if ( pArea == nullptr || pArea->_listGroup.empty() )
            return nullptr;
        if ( _stepsSinceEncounter <= pArea->_graceSteps )
            return nullptr;
        // 걸음마다 난수를 하나 쓴다(유예 중에는 쓰지 않는다).
        if ( _random.nextChance( pArea->_rate ) == false )
            return nullptr;

        const int32 groupIndex = _random.pickWeightedIndexInt( pArea->_listGroup, []( const JRPGEncounterGroup& group )
        { return group._weight; } );
        if ( groupIndex < 0 )
            return nullptr;
        _stepsSinceEncounter = 0;
        return pArea->_listGroup.data() + groupIndex;
    }

    void JRPGEncounterWalker::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _stepsSinceEncounter;
        outArchive << _totalSteps;
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
        const bool bValid = archive.isOk() && 0 <= stepsSinceEncounter && stepsSinceEncounter <= kNoEncounterYet && 0 <= totalSteps;
        if ( bValid == false )
            return false;
        _random              = random;
        _stepsSinceEncounter = stepsSinceEncounter;
        _totalSteps          = totalSteps;
        return true;
    }
} // namespace sw
