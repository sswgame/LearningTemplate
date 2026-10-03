#include "pch.h"

#include "GameFramework/Kits/ClassicJrpg/JrpgEncounter.h"

namespace sw
{
    JrpgEncounterWalker::JrpgEncounterWalker()
        : _random{}
        , _pCatalog{ nullptr }
        , _stepsSinceEncounter{ kNoEncounterYet }
        , _totalSteps{ 0 }
    {
    }

    void JrpgEncounterWalker::initialize( const JrpgCatalog* pCatalog, uint32 seed )
    {
        _pCatalog = pCatalog;
        _random.setSeed( seed );
        _stepsSinceEncounter = kNoEncounterYet;
        _totalSteps          = 0;
    }

    const JrpgEncounterGroup* JrpgEncounterWalker::step( const hashed_string& areaId )
    {
        ++_totalSteps;
        if ( _stepsSinceEncounter < kNoEncounterYet )
            ++_stepsSinceEncounter;
        const JrpgAreaDef* pArea = _pCatalog != nullptr ? _pCatalog->findArea( areaId ) : nullptr;
        if ( pArea == nullptr || pArea->_listGroup.empty() )
            return nullptr;
        if ( _stepsSinceEncounter <= pArea->_graceSteps )
            return nullptr;
        // 걸음마다 난수를 하나 쓴다(유예 중에는 쓰지 않는다).
        if ( _random.nextChance( pArea->_rate ) == false )
            return nullptr;

        int32 totalWeight = 0;
        for ( const JrpgEncounterGroup& group : pArea->_listGroup )
            totalWeight += group._weight;
        if ( totalWeight <= 0 )
            return nullptr;
        int32 pick = _random.nextInt( 0, totalWeight - 1 );
        for ( const JrpgEncounterGroup& group : pArea->_listGroup )
        {
            if ( pick < group._weight )
            {
                _stepsSinceEncounter = 0;
                return &group;
            }
            pick -= group._weight;
        }
        return nullptr;
    }
} // namespace sw
