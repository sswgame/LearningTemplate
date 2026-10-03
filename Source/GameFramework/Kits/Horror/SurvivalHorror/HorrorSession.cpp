#include "pch.h"

#include "GameFramework/Kits/Horror/SurvivalHorror/HorrorSession.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/World/AreaGraph.h"

namespace sw
{
    namespace
    {
        struct HorrorSessionInternal
        {
            static ResourceGaugeSettings makeSanitySettings( const HorrorRules& rules )
            {
                ResourceGaugeSettings settings;
                settings._max            = rules._maxSanity;
                settings._regenRate      = rules._sanityRegen;
                settings._regenDelay     = rules._sanityRegenDelay;
                settings._drainPerSecond = rules._darknessDrain;
                return settings;
            }

            static ResourceGaugeSettings makeBatterySettings( const HorrorRules& rules )
            {
                ResourceGaugeSettings settings;
                settings._max            = rules._maxBattery;
                settings._regenRate      = 0.0f;
                settings._regenDelay     = 0.0f;
                settings._drainPerSecond = rules._batteryDrain;
                return settings;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( HorrorPuzzleResult result )
    {
        switch ( result )
        {
            case HorrorPuzzleResult::Solved:
                return "Solved";
            case HorrorPuzzleResult::Progress:
                return "Progress";
            case HorrorPuzzleResult::Wrong:
                return "Wrong";
            case HorrorPuzzleResult::AlreadySolved:
                return "AlreadySolved";
            case HorrorPuzzleResult::LockedOut:
                return "LockedOut";
            case HorrorPuzzleResult::MissingItem:
                return "MissingItem";
            case HorrorPuzzleResult::Unknown:
                return "Unknown";
        }
        return "?";
    }

    HorrorSession::HorrorSession()
        : _inventory{}
        , _itemBox{}
        , _flags{}
        , _sanity{}
        , _battery{}
        , _uniqueSeenMonster{}
        , _uniqueReadDocument{}
        , _uniqueClue{}
        , _uniqueSolvedPuzzle{}
        , _mapDialAttempt{}
        , _mapSequenceProgress{}
        , _listClueLink{}
        , _listEvent{}
        , _currentArea{}
        , _pCatalog{ nullptr }
        , _pAreaGraph{ nullptr }
        , _health{ 0.0f }
        , _saveCount{ 0 }
        , _wrongDeductionCount{ 0 }
        , _bFlashlightOn{ SW_FALSE }
        , _bHallucinating{ SW_FALSE }
    {
    }

    void HorrorSession::initialize( const HorrorCatalog* pCatalog, AreaGraph* pAreaGraph, const hashed_string& startArea )
    {
        _pCatalog               = pCatalog;
        _pAreaGraph             = pAreaGraph;
        const HorrorRules rules = pCatalog != nullptr ? pCatalog->getRules() : HorrorRules{};
        _inventory.initialize( pCatalog != nullptr ? pCatalog->makeShapeLookup() : GridInventory::ShapeDelegate{}, rules._gridWidth, rules._gridHeight );
        _itemBox.clear();
        _flags.clear();
        _sanity.initialize( HorrorSessionInternal::makeSanitySettings( rules ) );
        _battery.initialize( HorrorSessionInternal::makeBatterySettings( rules ) );
        _uniqueSeenMonster.clear();
        _uniqueReadDocument.clear();
        _uniqueClue.clear();
        _uniqueSolvedPuzzle.clear();
        _mapDialAttempt.clear();
        _mapSequenceProgress.clear();
        _listClueLink.clear();
        _listEvent.clear();
        _health              = rules._maxHealth;
        _saveCount           = 0;
        _wrongDeductionCount = 0;
        _bFlashlightOn       = SW_FALSE;
        _bHallucinating      = SW_FALSE;
        _currentArea         = startArea;
        if ( _pAreaGraph != nullptr && startArea.empty() == false )
            (void)_pAreaGraph->enterArea( startArea );
    }

    void HorrorSession::update( float32 deltaTime, bool bInDarkness )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _bFlashlightOn == SW_TRUE && _battery.drain( deltaTime ) == false )
        {
            _bFlashlightOn = SW_FALSE;
            pushEvent( HorrorEvent::Kind::FlashlightDied, hashed_string() );
        }
        // 어둠은 매 프레임 조금씩이라 알림을 내지 않는다(환각 시작 · 끝만 알린다).
        if ( bInDarkness && _bFlashlightOn == SW_FALSE )
            (void)_sanity.drain( deltaTime ); // 0 에 붙어 있어도 계속 깎는 중 — 회복 지연이 다시 센다
        _sanity.update( deltaTime );
        _battery.update( deltaTime );
        refreshHallucination();
    }

    bool HorrorSession::storeInBox( int32 instanceId, int32 count )
    {
        const GridItem* pItem = _inventory.findInstance( instanceId );
        if ( pItem == nullptr || count <= 0 || pItem->_count < count )
            return false;
        const hashed_string itemId = pItem->_itemId;
        const int32         taken  = _inventory.takeFromInstance( instanceId, count );
        _itemBox.addItem( itemId, taken );
        return taken == count;
    }

    int32 HorrorSession::takeFromBox( const hashed_string& itemId, int32 count )
    {
        const int32 wanted = MathUtil::min( count, _itemBox.getItemCount( itemId ) );
        if ( wanted <= 0 )
            return 0;
        const int32 added = _inventory.addItem( itemId, wanted );
        if ( added > 0 )
            (void)_itemBox.removeItem( itemId, added );
        return added;
    }

    bool HorrorSession::combineItems( int32 firstInstanceId, int32 secondInstanceId )
    {
        if ( _pCatalog == nullptr )
            return false;
        const GridItem* pFirst  = _inventory.findInstance( firstInstanceId );
        const GridItem* pSecond = _inventory.findInstance( secondInstanceId );
        if ( pFirst == nullptr || pSecond == nullptr )
            return false;
        if ( firstInstanceId == secondInstanceId && pFirst->_count < 2 )
            return false;
        const hashed_string firstItem  = pFirst->_itemId;
        const hashed_string secondItem = pSecond->_itemId;
        const RecipeDef*    pRecipe    = _pCatalog->findCombine( firstItem, secondItem );
        if ( pRecipe == nullptr || pRecipe->_outputs.isEmpty() )
            return false;

        (void)_inventory.takeFromInstance( firstInstanceId, 1 );
        (void)_inventory.takeFromInstance( secondInstanceId, 1 );
        vector<hashed_string> listOutput;
        pRecipe->_outputs.getItemIds( listOutput );
        vector<int32> listAdded;
        bool          bAllFit = true;
        for ( const hashed_string& outputId : listOutput )
        {
            const int32 wanted = pRecipe->_outputs.getItemCount( outputId );
            const int32 added  = _inventory.addItem( outputId, wanted );
            listAdded.push_back( added );
            if ( added < wanted )
                bAllFit = false;
        }
        if ( bAllFit == false )
        {
            // 결과가 다 들어가지 않으면 없던 일로 — 넣은 결과를 빼고 재료를 돌려놓는다(재료가 비운 자리라 늘 들어간다).
            for ( size_t outputIndex = 0; outputIndex < listOutput.size(); ++outputIndex )
            {
                if ( listAdded[outputIndex] > 0 )
                    (void)_inventory.removeItem( listOutput[outputIndex], listAdded[outputIndex] );
            }
            (void)_inventory.addItem( firstItem, 1 );
            (void)_inventory.addItem( secondItem, 1 );
            return false;
        }
        for ( const hashed_string& outputId : listOutput )
            pushEvent( HorrorEvent::Kind::Combined, outputId, static_cast<float32>( pRecipe->_outputs.getItemCount( outputId ) ) );
        return true;
    }

    bool HorrorSession::tryConsumeAmmo( const hashed_string& ammoItemId, int32 count )
    {
        return _inventory.removeItem( ammoItemId, count );
    }

    bool HorrorSession::tryUseItem( int32 instanceId )
    {
        const GridItem* pItem = _inventory.findInstance( instanceId );
        if ( pItem == nullptr || _pCatalog == nullptr )
            return false;
        const HorrorItemDef* pDef = _pCatalog->findItem( pItem->_itemId );
        if ( pDef == nullptr || ( pDef->_healAmount <= 0.0f && pDef->_sanityAmount <= 0.0f && pDef->_batteryAmount <= 0.0f ) )
            return false;
        const float32 maxHealth = _pCatalog->getRules()._maxHealth;
        _health                 = MathUtil::min( maxHealth, _health + pDef->_healAmount );
        if ( pDef->_sanityAmount > 0.0f )
            _sanity.restore( pDef->_sanityAmount );
        if ( pDef->_batteryAmount > 0.0f )
            _battery.restore( pDef->_batteryAmount );
        (void)_inventory.takeFromInstance( instanceId, 1 );
        refreshHallucination();
        return true;
    }

    void HorrorSession::applyDamage( float32 amount )
    {
        _health = MathUtil::max( 0.0f, _health - MathUtil::max( 0.0f, amount ) );
    }

    HorrorSaveResult HorrorSession::trySave()
    {
        const HorrorRules rules = _pCatalog != nullptr ? _pCatalog->getRules() : HorrorRules{};
        if ( rules._saveMode == HorrorSaveMode::Limited && _saveCount >= rules._maxSaves )
            return HorrorSaveResult::NoSavesLeft;
        if ( rules._saveMode == HorrorSaveMode::InkRibbon )
        {
            // 가방에서 처음 찾은 SaveItem 하나를 쓴다(놓은 순서 — 결정적).
            int32 ribbonInstance = -1;
            for ( const GridItem& item : _inventory.getItems() )
            {
                const HorrorItemDef* pDef = _pCatalog->findItem( item._itemId );
                if ( pDef != nullptr && pDef->_kind == HorrorItemKind::SaveItem )
                {
                    ribbonInstance = item._instanceId;
                    break;
                }
            }
            if ( ribbonInstance < 0 )
                return HorrorSaveResult::NoSaveItem;
            (void)_inventory.takeFromInstance( ribbonInstance, 1 );
        }
        ++_saveCount;
        pushEvent( HorrorEvent::Kind::Saved, hashed_string(), static_cast<float32>( _saveCount ) );
        return HorrorSaveResult::Ok;
    }

    bool HorrorSession::trySetFlashlight( bool bOn )
    {
        if ( bOn && _battery.getValue() <= 0.0f )
            return false;
        _bFlashlightOn = bOn ? SW_TRUE : SW_FALSE;
        return true;
    }

    float32 HorrorSession::witnessMonster( const hashed_string& monsterId )
    {
        const HorrorMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterId ) : nullptr;
        if ( pMonster == nullptr )
            return 0.0f;
        const bool    bFirstSight = _uniqueSeenMonster.insert( monsterId ).second;
        const float32 loss        = bFirstSight ? pMonster->_sanityLoss : pMonster->_sanityLoss * _pCatalog->getRules()._repeatSightingScale;
        const float32 before      = _sanity.getValue();
        loseSanity( loss, monsterId );
        return before - _sanity.getValue();
    }

    void HorrorSession::loseSanity( float32 amount, const hashed_string& cause )
    {
        const float32 lost = _sanity.reduce( amount );
        if ( lost <= 0.0f )
            return;
        pushEvent( HorrorEvent::Kind::SanityLost, cause, lost );
        refreshHallucination();
    }

    float32 HorrorSession::computeAimSwayScale() const
    {
        const float32 maxSway = _pCatalog != nullptr ? _pCatalog->getRules()._maxAimSway : 0.0f;
        return 1.0f + ( 1.0f - MathUtil::saturate( _sanity.getRatio() ) ) * maxSway;
    }

    HorrorPuzzleResult HorrorSession::useKey( const hashed_string& lockId )
    {
        const HorrorKeyLockDef* pLock = _pCatalog != nullptr ? _pCatalog->findKeyLock( lockId ) : nullptr;
        if ( pLock == nullptr )
            return HorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( lockId ) > 0 )
            return HorrorPuzzleResult::AlreadySolved;
        if ( _inventory.hasItem( pLock->_keyItem ) == false )
            return HorrorPuzzleResult::MissingItem;
        if ( pLock->_bConsumeKey == SW_TRUE )
            (void)_inventory.removeItem( pLock->_keyItem, 1 );
        (void)markSolved( lockId, pLock->_flag );
        pushEvent( HorrorEvent::Kind::DoorUnlocked, lockId );
        return HorrorPuzzleResult::Solved;
    }

    HorrorPuzzleResult HorrorSession::enterDialCode( const hashed_string& lockId, const vector<int32>& listDigit )
    {
        const HorrorDialLockDef* pLock = _pCatalog != nullptr ? _pCatalog->findDialLock( lockId ) : nullptr;
        if ( pLock == nullptr )
            return HorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( lockId ) > 0 )
            return HorrorPuzzleResult::AlreadySolved;
        int32& wrongCount = _mapDialAttempt[lockId];
        if ( pLock->_maxAttempts > 0 && wrongCount >= pLock->_maxAttempts )
            return HorrorPuzzleResult::LockedOut;
        if ( listDigit != pLock->_listDigit )
        {
            ++wrongCount;
            return pLock->_maxAttempts > 0 && wrongCount >= pLock->_maxAttempts ? HorrorPuzzleResult::LockedOut : HorrorPuzzleResult::Wrong;
        }
        (void)markSolved( lockId, pLock->_flag );
        pushEvent( HorrorEvent::Kind::PuzzleSolved, lockId );
        return HorrorPuzzleResult::Solved;
    }

    HorrorPuzzleResult HorrorSession::pressSequenceStep( const hashed_string& puzzleId, const hashed_string& step )
    {
        const HorrorSequenceDef* pSequence = _pCatalog != nullptr ? _pCatalog->findSequence( puzzleId ) : nullptr;
        if ( pSequence == nullptr || pSequence->_listStep.empty() )
            return HorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( puzzleId ) > 0 )
            return HorrorPuzzleResult::AlreadySolved;
        int32& progress = _mapSequenceProgress[puzzleId];
        if ( pSequence->_listStep[static_cast<size_t>( progress )] != step )
        {
            progress = 0;
            loseSanity( pSequence->_mistakeSanity, puzzleId );
            return HorrorPuzzleResult::Wrong;
        }
        ++progress;
        if ( progress < static_cast<int32>( pSequence->_listStep.size() ) )
            return HorrorPuzzleResult::Progress;
        (void)markSolved( puzzleId, pSequence->_flag );
        pushEvent( HorrorEvent::Kind::PuzzleSolved, puzzleId );
        return HorrorPuzzleResult::Solved;
    }

    bool HorrorSession::tryMoveTo( const hashed_string& areaId )
    {
        if ( _pAreaGraph == nullptr || _pAreaGraph->canTraverse( _currentArea, areaId, _flags ) == false )
            return false;
        (void)_pAreaGraph->enterArea( areaId );
        _currentArea = areaId;
        pushEvent( HorrorEvent::Kind::AreaEntered, areaId );
        return true;
    }

    bool HorrorSession::readDocument( const hashed_string& documentId )
    {
        const HorrorDocumentDef* pDocument = _pCatalog != nullptr ? _pCatalog->findDocument( documentId ) : nullptr;
        if ( pDocument == nullptr || _uniqueReadDocument.insert( documentId ).second == false )
            return false;
        pushEvent( HorrorEvent::Kind::DocumentRead, documentId );
        for ( const hashed_string& clueId : pDocument->_listClue )
            addClue( clueId );
        return true;
    }

    void HorrorSession::addClue( const hashed_string& clueId )
    {
        if ( clueId.empty() == false && _uniqueClue.insert( clueId ).second )
            pushEvent( HorrorEvent::Kind::ClueGained, clueId );
    }

    bool HorrorSession::hasClue( const hashed_string& clueId ) const
    {
        return _uniqueClue.count( clueId ) > 0;
    }

    bool HorrorSession::linkClues( const hashed_string& firstClue, const hashed_string& secondClue )
    {
        if ( firstClue == secondClue || hasClue( firstClue ) == false || hasClue( secondClue ) == false || isLinked( firstClue, secondClue ) )
            return false;
        HorrorClueLink link;
        link._first  = firstClue;
        link._second = secondClue;
        _listClueLink.push_back( link );
        return true;
    }

    bool HorrorSession::unlinkClues( const hashed_string& firstClue, const hashed_string& secondClue )
    {
        for ( size_t linkIndex = 0; linkIndex < _listClueLink.size(); ++linkIndex )
        {
            if ( _listClueLink[linkIndex].isSame( firstClue, secondClue ) )
            {
                _listClueLink.erase( _listClueLink.begin() + static_cast<ptrdiff_t>( linkIndex ) );
                return true;
            }
        }
        return false;
    }

    bool HorrorSession::isLinked( const hashed_string& firstClue, const hashed_string& secondClue ) const
    {
        for ( const HorrorClueLink& link : _listClueLink )
        {
            if ( link.isSame( firstClue, secondClue ) )
                return true;
        }
        return false;
    }

    HorrorPuzzleResult HorrorSession::submitDeduction( const hashed_string& deductionId, const hashed_string& answer )
    {
        const HorrorDeductionDef* pDeduction = _pCatalog != nullptr ? _pCatalog->findDeduction( deductionId ) : nullptr;
        if ( pDeduction == nullptr )
            return HorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( deductionId ) > 0 )
            return HorrorPuzzleResult::AlreadySolved;
        for ( const HorrorClueLink& required : pDeduction->_listRequiredLink )
        {
            if ( hasClue( required._first ) == false || hasClue( required._second ) == false )
                return HorrorPuzzleResult::MissingItem;
        }
        bool bCorrect = answer == pDeduction->_answer;
        for ( const HorrorClueLink& required : pDeduction->_listRequiredLink )
        {
            if ( bCorrect && isLinked( required._first, required._second ) == false )
                bCorrect = false;
        }
        if ( bCorrect == false )
        {
            ++_wrongDeductionCount;
            loseSanity( pDeduction->_wrongSanity, deductionId );
            if ( _pCatalog->getRules()._bClearLinksOnWrong == SW_TRUE )
                _listClueLink.clear();
            pushEvent( HorrorEvent::Kind::DeductionFailed, deductionId );
            return HorrorPuzzleResult::Wrong;
        }
        (void)markSolved( deductionId, pDeduction->_flag );
        pushEvent( HorrorEvent::Kind::DeductionSolved, deductionId );
        return HorrorPuzzleResult::Solved;
    }

    void HorrorSession::drainEvents( vector<HorrorEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    void HorrorSession::pushEvent( HorrorEvent::Kind kind, const hashed_string& id, float32 value )
    {
        HorrorEvent event;
        event._kind  = kind;
        event._id    = id;
        event._value = value;
        _listEvent.push_back( event );
    }

    void HorrorSession::refreshHallucination()
    {
        const float32 threshold = _pCatalog != nullptr ? _pCatalog->getRules()._hallucinationThreshold : 0.0f;
        const bool    bNow      = _sanity.getRatio() < threshold;
        if ( bNow == ( _bHallucinating == SW_TRUE ) )
            return;
        _bHallucinating = bNow ? SW_TRUE : SW_FALSE;
        pushEvent( bNow ? HorrorEvent::Kind::HallucinationStarted : HorrorEvent::Kind::HallucinationEnded, hashed_string() );
    }

    bool HorrorSession::markSolved( const hashed_string& puzzleId, const hashed_string& flag )
    {
        if ( flag.empty() == false )
            _flags.setFlag( flag, 1 );
        return _uniqueSolvedPuzzle.insert( puzzleId ).second;
    }
} // namespace sw
