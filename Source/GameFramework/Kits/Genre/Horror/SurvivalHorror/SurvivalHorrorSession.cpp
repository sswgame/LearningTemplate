#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorSession.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct SurvivalHorrorSessionInternal
        {
            static ResourceGaugeSettings makeSanitySettings( const SurvivalHorrorRules& rules )
            {
                ResourceGaugeSettings settings;
                settings._max            = rules._maxSanity;
                settings._regenRate      = rules._sanityRegen;
                settings._regenDelay     = rules._sanityRegenDelay;
                settings._drainPerSecond = rules._darknessDrain;
                return settings;
            }

            static ResourceGaugeSettings makeBatterySettings( const SurvivalHorrorRules& rules )
            {
                ResourceGaugeSettings settings;
                settings._max            = rules._maxBattery;
                settings._regenRate      = 0.0f;
                settings._regenDelay     = 0.0f;
                settings._drainPerSecond = rules._batteryDrain;
                return settings;
            }

            static void serializeSet( StateIO& io, unordered_set<hashed_string>& uniqueName )
            {
                vector<hashed_string> listName( uniqueName.begin(), uniqueName.end() );
                VectorUtil::sort( listName, HashedStringLexicalLess{} );
                uint32 nameCount = static_cast<uint32>( listName.size() );
                io.count( 4, nameCount );
                if ( io.isOk() == false )
                    return;
                if ( io.isReading() )
                {
                    uniqueName.clear();
                }
                for ( uint32 index = 0; index < nameCount; ++index )
                {
                    hashed_string name = io.isWriting() ? listName[index] : hashed_string{};
                    io.value( name );
                    if ( io.isReading() && ( io.isOk() == false || io.check( uniqueName.insert( name ).second ) == false ) )
                        return;
                }
            }

            static void serializeMap( StateIO& io, unordered_map<hashed_string, int32>& mapNameToCount )
            {
                vector<hashed_string> listName;
                listName.reserve( mapNameToCount.size() );
                for ( const auto& [name, count] : mapNameToCount )
                {
                    listName.push_back( name );
                }
                VectorUtil::sort( listName, HashedStringLexicalLess{} );
                uint32 nameCount = static_cast<uint32>( listName.size() );
                // 이름(4) + 수(4)
                io.count( 8, nameCount );
                if ( io.isOk() == false )
                    return;
                if ( io.isReading() )
                {
                    mapNameToCount.clear();
                }
                for ( uint32 index = 0; index < nameCount; ++index )
                {
                    hashed_string name  = io.isWriting() ? listName[index] : hashed_string{};
                    int32         value = io.isWriting() ? mapNameToCount.find( name )->second : 0;
                    io.value( name );
                    io.value( value );
                    if ( io.isReading() && ( io.isOk() == false || io.check( mapNameToCount.emplace( name, value ).second ) == false ) )
                        return;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SurvivalHorrorSession::SurvivalHorrorSession()
        : _sanity{}
        , _battery{}
        , _uniqueSeenMonster{}
        , _uniqueReadDocument{}
        , _uniqueClue{}
        , _uniqueSolvedPuzzle{}
        , _mapDialAttempt{}
        , _mapSequenceProgress{}
        , _listClueLink{}
        , _eventBuffer{}
        , _currentArea{}
        , _pCatalog{ nullptr }
        , _pInventory{ nullptr }
        , _pItemBox{ nullptr }
        , _pFlags{ nullptr }
        , _pAreaGraph{ nullptr }
        , _health{ 0.0f }
        , _saveCount{ 0 }
        , _wrongDeductionCount{ 0 }
        , _bFlashlightOn{ SW_FALSE }
        , _bHallucinating{ SW_FALSE }
    {
    }

    bool SurvivalHorrorSession::initialize( const SurvivalHorrorCatalog* pCatalog, AreaGraph* pAreaGraph, const hashed_string& startArea, const SessionStateRefs& refs,
                                            GridInventory& inventory, Inventory& itemBox )
    {
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "SurvivalHorrorSession.SurvivalHorrorSessionRefusedNoCatalog", "SurvivalHorrorSession: refused - no catalog (the rules come from the catalog, there is no default set)" );
            return false;
        }
        // 없는 방에서 시작하면 어느 길도 지날 수 없어 판이 멈춘다.
        if ( pAreaGraph != nullptr && startArea.empty() == false && pAreaGraph->findArea( startArea ) == nullptr )
        {
            SW_LOG_ERROR_ID( "SurvivalHorrorSession.UnknownStartArea", "SurvivalHorrorSession: refused - start area '%#' is not on the graph", startArea.c_str() );
            return false;
        }
        const SurvivalHorrorRules& rules = pCatalog->getRules();
        // 따로 만들어 둘 다 받아들여질 때만 바꾼다 — 하나라도 거절되면 상태는 그대로다.
        ResourceGauge sanity;
        ResourceGauge battery;
        if ( sanity.initialize( SurvivalHorrorSessionInternal::makeSanitySettings( rules ) ) == false || battery.initialize( SurvivalHorrorSessionInternal::makeBatterySettings( rules ) ) == false )
            return false;
        if ( inventory.initialize( pCatalog->makeShapeLookup(), rules._gridWidth, rules._gridHeight ) == false )
            return false;
        _pCatalog   = pCatalog;
        _pAreaGraph = pAreaGraph;
        _pInventory = &inventory;
        _pItemBox   = &itemBox;
        _pFlags     = refs._pFlags;
        _sanity     = sanity;
        _battery    = battery;
        _uniqueSeenMonster.clear();
        _uniqueReadDocument.clear();
        _uniqueClue.clear();
        _uniqueSolvedPuzzle.clear();
        _mapDialAttempt.clear();
        _mapSequenceProgress.clear();
        _listClueLink.clear();
        _eventBuffer.clear();
        _health              = rules._maxHealth;
        _saveCount           = 0;
        _wrongDeductionCount = 0;
        _bFlashlightOn       = SW_FALSE;
        _bHallucinating      = SW_FALSE;
        _currentArea         = startArea;
        if ( _pAreaGraph != nullptr && startArea.empty() == false )
            (void)_pAreaGraph->enterArea( startArea );
        return true;
    }

    void SurvivalHorrorSession::update( float32 deltaTime, bool bInDarkness )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _bFlashlightOn == SW_TRUE && _battery.drain( deltaTime ) == false )
        {
            _bFlashlightOn = SW_FALSE;
            pushEvent( SurvivalHorrorEvent::Kind::FlashlightDied, hashed_string() );
        }
        // 어둠은 매 프레임 조금씩 깎이므로 이벤트를 내지 않는다(환각 시작과 끝만 알린다).
        // 회복은 밝은 곳에서만 돈다. 회복 지연이 걸음보다 짧으면 어둠 속에서도 찼다.
        if ( bInDarkness && _bFlashlightOn == SW_FALSE )
            (void)_sanity.drain( deltaTime ); // 0 에 붙어 있어도 계속 깎는 중이므로 회복 지연이 다시 센다
        else
            _sanity.update( deltaTime );
        _battery.update( deltaTime );
        refreshHallucination();
    }

    bool SurvivalHorrorSession::storeInBox( int32 instanceID, int32 count )
    {
        const GridItem* pItem               = _pInventory->findInstance( instanceID );
        const bool      bInvalidMoveRequest = pItem == nullptr || count <= 0 || pItem->_count < count;
        if ( bInvalidMoveRequest )
            return false;
        const hashed_string itemID = pItem->_itemID;
        if ( _pItemBox->hasRoomFor( itemID, count ) == false )
            return false;
        const int32 taken = _pInventory->takeFromInstance( instanceID, count );
        const int32 added = _pItemBox->addItem( itemID, taken );
        return taken == count && added == taken;
    }

    int32 SurvivalHorrorSession::takeFromBox( const hashed_string& itemID, int32 count )
    {
        const int32 wanted = MathUtil::min( count, _pItemBox->getItemCount( itemID ) );
        if ( wanted <= 0 )
            return 0;
        const int32 added = _pInventory->addItem( itemID, wanted );
        if ( added > 0 )
            (void)_pItemBox->removeItem( itemID, added ); // added 는 상자에 있던 수 이하이므로 항상 빠진다
        return added;
    }

    bool SurvivalHorrorSession::combineItems( int32 firstInstanceID, int32 secondInstanceID )
    {
        if ( _pCatalog == nullptr )
            return false;
        const GridItem* pFirst  = _pInventory->findInstance( firstInstanceID );
        const GridItem* pSecond = _pInventory->findInstance( secondInstanceID );
        if ( pFirst == nullptr || pSecond == nullptr )
            return false;
        if ( firstInstanceID == secondInstanceID && pFirst->_count < 2 )
            return false;
        const hashed_string firstItem  = pFirst->_itemID;
        const hashed_string secondItem = pSecond->_itemID;
        const RecipeDef*    pRecipe    = _pCatalog->findCombine( firstItem, secondItem );
        if ( pRecipe == nullptr || pRecipe->_outputs.isEmpty() )
            return false;

        (void)_pInventory->takeFromInstance( firstInstanceID, 1 );
        (void)_pInventory->takeFromInstance( secondInstanceID, 1 );
        vector<hashed_string> listOutput;
        pRecipe->_outputs.getItemIDs( listOutput );
        vector<int32> listAdded;
        bool          bAllFit = true;
        for ( const hashed_string& outputID : listOutput )
        {
            const int32 wanted = pRecipe->_outputs.getItemCount( outputID );
            const int32 added  = _pInventory->addItem( outputID, wanted );
            listAdded.push_back( added );
            if ( added < wanted )
                bAllFit = false;
        }
        if ( bAllFit == false )
        {
            // 결과가 다 들어가지 않으면 없던 일로 한다. 넣은 결과를 빼고 재료를 돌려놓는다(재료가 비운 슬롯이므로 항상 들어간다).
            for ( size_t outputIndex = 0; outputIndex < listOutput.size(); ++outputIndex )
            {
                if ( listAdded[outputIndex] > 0 )
                    (void)_pInventory->removeItem( listOutput[outputIndex], listAdded[outputIndex] ); // 방금 넣은 수이므로 항상 빠진다
            }
            (void)_pInventory->addItem( firstItem, 1 );
            (void)_pInventory->addItem( secondItem, 1 );
            return false;
        }
        for ( const hashed_string& outputID : listOutput )
        {
            pushEvent( SurvivalHorrorEvent::Kind::Combined, outputID, static_cast<float32>( pRecipe->_outputs.getItemCount( outputID ) ) );
        }
        return true;
    }

    bool SurvivalHorrorSession::tryConsumeAmmo( const hashed_string& ammoItemID, int32 count )
    {
        return _pInventory->removeItem( ammoItemID, count );
    }

    bool SurvivalHorrorSession::tryUseItem( int32 instanceID )
    {
        const GridItem* pItem = _pInventory->findInstance( instanceID );
        // 죽은 뒤에 약을 먹어 되살아나지 않는다.
        if ( pItem == nullptr || _pCatalog == nullptr || isDead() )
            return false;
        const SurvivalHorrorItemDef* pDef = _pCatalog->findItem( pItem->_itemID );
        if ( pDef == nullptr || ( pDef->_healAmount <= 0.0f && pDef->_sanityAmount <= 0.0f && pDef->_batteryAmount <= 0.0f ) )
            return false;
        const float32 maxHealth = _pCatalog->getRules()._maxHealth;
        _health                 = MathUtil::min( maxHealth, _health + pDef->_healAmount );
        if ( pDef->_sanityAmount > 0.0f )
            _sanity.restore( pDef->_sanityAmount );
        if ( pDef->_batteryAmount > 0.0f )
            _battery.restore( pDef->_batteryAmount );
        (void)_pInventory->takeFromInstance( instanceID, 1 );
        refreshHallucination();
        return true;
    }

    void SurvivalHorrorSession::applyDamage( float32 amount )
    {
        DepletingValue::take( _health, amount );
    }

    SurvivalHorrorSaveResult SurvivalHorrorSession::trySave()
    {
        const SurvivalHorrorRules rules = _pCatalog != nullptr ? _pCatalog->getRules() : SurvivalHorrorRules{};
        if ( rules._saveMode == SurvivalHorrorSaveMode::Limited && _saveCount >= rules._maxSaves )
            return SurvivalHorrorSaveResult::NoSavesLeft;
        if ( rules._saveMode == SurvivalHorrorSaveMode::InkRibbon )
        {
            // 가방에서 처음 찾은 SaveItem 하나를 쓴다(놓은 순서이므로 결정적이다).
            int32 ribbonInstance = constant::invalid_index::kInt32;
            for ( const GridItem& item : _pInventory->getItems() )
            {
                const SurvivalHorrorItemDef* pDef = _pCatalog->findItem( item._itemID );
                if ( pDef != nullptr && pDef->_kind == SurvivalHorrorItemKind::SaveItem )
                {
                    ribbonInstance = item._instanceID;
                    break;
                }
            }
            if ( ribbonInstance < 0 )
                return SurvivalHorrorSaveResult::NoSaveItem;
            (void)_pInventory->takeFromInstance( ribbonInstance, 1 );
        }
        ++_saveCount;
        pushEvent( SurvivalHorrorEvent::Kind::Saved, hashed_string(), static_cast<float32>( _saveCount ) );
        return SurvivalHorrorSaveResult::Ok;
    }

    bool SurvivalHorrorSession::trySetFlashlight( bool bOn )
    {
        if ( bOn && _battery.getValue() <= 0.0f )
            return false;
        _bFlashlightOn = bOn ? SW_TRUE : SW_FALSE;
        return true;
    }

    float32 SurvivalHorrorSession::witnessMonster( const hashed_string& monsterID )
    {
        const SurvivalHorrorMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterID ) : nullptr;
        if ( pMonster == nullptr )
            return 0.0f;
        const bool    bFirstSight = _uniqueSeenMonster.insert( monsterID ).second;
        const float32 loss        = bFirstSight ? pMonster->_sanityLoss : pMonster->_sanityLoss * _pCatalog->getRules()._repeatSightingScale;
        const float32 before      = _sanity.getValue();
        loseSanity( loss, monsterID );
        return before - _sanity.getValue();
    }

    void SurvivalHorrorSession::loseSanity( float32 amount, const hashed_string& cause )
    {
        const float32 lost = _sanity.reduce( amount );
        if ( lost <= 0.0f )
            return;
        pushEvent( SurvivalHorrorEvent::Kind::SanityLost, cause, lost );
        refreshHallucination();
    }

    float32 SurvivalHorrorSession::computeAimSwayScale() const
    {
        const float32 maxSway = _pCatalog != nullptr ? _pCatalog->getRules()._maxAimSway : 0.0f;
        return 1.0f + ( 1.0f - MathUtil::saturate( _sanity.getRatio() ) ) * maxSway;
    }

    SurvivalHorrorPuzzleResult SurvivalHorrorSession::useKey( const hashed_string& lockID )
    {
        const SurvivalHorrorKeyLockDef* pLock = _pCatalog != nullptr ? _pCatalog->findKeyLock( lockID ) : nullptr;
        if ( pLock == nullptr )
            return SurvivalHorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( lockID ) > 0 )
            return SurvivalHorrorPuzzleResult::AlreadySolved;
        if ( _pInventory->hasItem( pLock->_keyItem ) == false )
            return SurvivalHorrorPuzzleResult::MissingItem;
        if ( pLock->_bConsumeKey )
            (void)_pInventory->removeItem( pLock->_keyItem, 1 ); // 열쇠는 위의 hasItem 이 확인했다
        (void)markSolved( lockID, pLock->_flag );
        pushEvent( SurvivalHorrorEvent::Kind::DoorUnlocked, lockID );
        return SurvivalHorrorPuzzleResult::Solved;
    }

    SurvivalHorrorPuzzleResult SurvivalHorrorSession::enterDialCode( const hashed_string& lockID, const vector<int32>& listDigit )
    {
        const SurvivalHorrorDialLockDef* pLock = _pCatalog != nullptr ? _pCatalog->findDialLock( lockID ) : nullptr;
        if ( pLock == nullptr )
            return SurvivalHorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( lockID ) > 0 )
            return SurvivalHorrorPuzzleResult::AlreadySolved;
        // 틀렸을 때만 칸을 만든다 — 시도하지 않은 자물쇠와 맞힌 자물쇠의 바이트가 같아야 한다.
        const auto  attemptIter = _mapDialAttempt.find( lockID );
        const int32 wrongCount  = attemptIter != _mapDialAttempt.end() ? attemptIter->second : 0;
        if ( pLock->_maxAttempts > 0 && wrongCount >= pLock->_maxAttempts )
            return SurvivalHorrorPuzzleResult::LockedOut;
        if ( listDigit != pLock->_listDigit )
        {
            _mapDialAttempt[lockID] = wrongCount + 1;
            return pLock->_maxAttempts > 0 && wrongCount + 1 >= pLock->_maxAttempts ? SurvivalHorrorPuzzleResult::LockedOut : SurvivalHorrorPuzzleResult::Wrong;
        }
        (void)markSolved( lockID, pLock->_flag );
        pushEvent( SurvivalHorrorEvent::Kind::PuzzleSolved, lockID );
        return SurvivalHorrorPuzzleResult::Solved;
    }

    SurvivalHorrorPuzzleResult SurvivalHorrorSession::pressSequenceStep( const hashed_string& puzzleID, const hashed_string& step )
    {
        const SurvivalHorrorSequenceDef* pSequence = _pCatalog != nullptr ? _pCatalog->findSequence( puzzleID ) : nullptr;
        if ( pSequence == nullptr || pSequence->_listStep.empty() )
            return SurvivalHorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( puzzleID ) > 0 )
            return SurvivalHorrorPuzzleResult::AlreadySolved;
        // 진행 칸은 푸는 중에만 있다. 푼 퍼즐에 걸음 수만큼 남겨 두면 그 값이 `readState` 의 범위를 넘어 세이브를 다시 읽지 못했다.
        const auto  progressIter = _mapSequenceProgress.find( puzzleID );
        const int32 progress     = progressIter != _mapSequenceProgress.end() ? progressIter->second : 0;
        if ( pSequence->_listStep[static_cast<size_t>( progress )] != step )
        {
            _mapSequenceProgress.erase( puzzleID );
            loseSanity( pSequence->_mistakeSanity, puzzleID );
            return SurvivalHorrorPuzzleResult::Wrong;
        }
        if ( progress + 1 < static_cast<int32>( pSequence->_listStep.size() ) )
        {
            _mapSequenceProgress[puzzleID] = progress + 1;
            return SurvivalHorrorPuzzleResult::Progress;
        }
        _mapSequenceProgress.erase( puzzleID );
        (void)markSolved( puzzleID, pSequence->_flag );
        pushEvent( SurvivalHorrorEvent::Kind::PuzzleSolved, puzzleID );
        return SurvivalHorrorPuzzleResult::Solved;
    }

    bool SurvivalHorrorSession::tryMoveTo( const hashed_string& areaID )
    {
        static const GameFlags kNoFlags; // 빌리지 않은 세션이므로 조건 있는 길은 닫힌 채이다
        if ( _pAreaGraph == nullptr || _pAreaGraph->canTraverse( _currentArea, areaID, _pFlags != nullptr ? *_pFlags : kNoFlags ) == false )
            return false;
        (void)_pAreaGraph->enterArea( areaID );
        _currentArea = areaID;
        pushEvent( SurvivalHorrorEvent::Kind::AreaEntered, areaID );
        return true;
    }

    bool SurvivalHorrorSession::readDocument( const hashed_string& documentID )
    {
        const SurvivalHorrorDocumentDef* pDocument = _pCatalog != nullptr ? _pCatalog->findDocument( documentID ) : nullptr;
        if ( pDocument == nullptr || _uniqueReadDocument.insert( documentID ).second == false )
            return false;
        pushEvent( SurvivalHorrorEvent::Kind::DocumentRead, documentID );
        for ( const hashed_string& clueID : pDocument->_listClue )
        {
            addClue( clueID );
        }
        return true;
    }

    void SurvivalHorrorSession::addClue( const hashed_string& clueID )
    {
        if ( clueID.empty() == false && _uniqueClue.insert( clueID ).second )
            pushEvent( SurvivalHorrorEvent::Kind::ClueGained, clueID );
    }

    bool SurvivalHorrorSession::hasClue( const hashed_string& clueID ) const
    {
        return _uniqueClue.count( clueID ) > 0;
    }

    bool SurvivalHorrorSession::linkClues( const hashed_string& firstClue, const hashed_string& secondClue )
    {
        const bool bCannotLinkClues = firstClue == secondClue || hasClue( firstClue ) == false || hasClue( secondClue ) == false || isLinked( firstClue, secondClue );
        if ( bCannotLinkClues )
            return false;
        SurvivalHorrorClueLink link;
        link._first  = firstClue;
        link._second = secondClue;
        _listClueLink.push_back( link );
        return true;
    }

    bool SurvivalHorrorSession::unlinkClues( const hashed_string& firstClue, const hashed_string& secondClue )
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

    bool SurvivalHorrorSession::isLinked( const hashed_string& firstClue, const hashed_string& secondClue ) const
    {
        return VectorUtil::containsIf( _listClueLink, [&]( const SurvivalHorrorClueLink& link )
        { return link.isSame( firstClue, secondClue ); } );
    }

    SurvivalHorrorPuzzleResult SurvivalHorrorSession::submitDeduction( const hashed_string& deductionID, const hashed_string& answer )
    {
        const SurvivalHorrorDeductionDef* pDeduction = _pCatalog != nullptr ? _pCatalog->findDeduction( deductionID ) : nullptr;
        if ( pDeduction == nullptr )
            return SurvivalHorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( deductionID ) > 0 )
            return SurvivalHorrorPuzzleResult::AlreadySolved;
        for ( const SurvivalHorrorClueLink& required : pDeduction->_listRequiredLink )
        {
            if ( hasClue( required._first ) == false || hasClue( required._second ) == false )
                return SurvivalHorrorPuzzleResult::MissingItem;
        }
        bool bCorrect = answer == pDeduction->_answer;
        for ( const SurvivalHorrorClueLink& required : pDeduction->_listRequiredLink )
        {
            if ( bCorrect && isLinked( required._first, required._second ) == false )
                bCorrect = false;
        }
        if ( bCorrect == false )
        {
            ++_wrongDeductionCount;
            loseSanity( pDeduction->_wrongSanity, deductionID );
            if ( _pCatalog->getRules()._bClearLinksOnWrong )
                _listClueLink.clear();
            pushEvent( SurvivalHorrorEvent::Kind::DeductionFailed, deductionID );
            return SurvivalHorrorPuzzleResult::Wrong;
        }
        (void)markSolved( deductionID, pDeduction->_flag );
        pushEvent( SurvivalHorrorEvent::Kind::DeductionSolved, deductionID );
        return SurvivalHorrorPuzzleResult::Solved;
    }

    void SurvivalHorrorSession::pollEvents( vector<SurvivalHorrorEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void SurvivalHorrorSession::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        io.value( _sanity );
        io.value( _battery );
        SurvivalHorrorSessionInternal::serializeSet( io, _uniqueSeenMonster );
        SurvivalHorrorSessionInternal::serializeSet( io, _uniqueReadDocument );
        SurvivalHorrorSessionInternal::serializeSet( io, _uniqueClue );
        SurvivalHorrorSessionInternal::serializeSet( io, _uniqueSolvedPuzzle );
        SurvivalHorrorSessionInternal::serializeMap( io, _mapDialAttempt );
        SurvivalHorrorSessionInternal::serializeMap( io, _mapSequenceProgress );
        // 연결마다 이름 둘(8)
        io.list( _listClueLink, 8, [&]( SurvivalHorrorClueLink& link )
        {
            io.value( link._first );
            io.value( link._second );
        } );
        io.value( _currentArea );
        io.value( _health );
        io.value( _saveCount );
        io.value( _wrongDeductionCount );
        io.flag( _bFlashlightOn );
        io.flag( _bHallucinating );
        if ( io.isReading() && io.isOk() )
        {
            io.check( isStateConsistent() );
            _eventBuffer.clear();
        }
    }

    bool SurvivalHorrorSession::isStateConsistent() const
    {
        const SurvivalHorrorRules& rules          = _pCatalog->getRules();
        const bool                 bLimitedOver   = rules._saveMode == SurvivalHorrorSaveMode::Limited && _saveCount > rules._maxSaves;
        const bool                 bCountsInvalid = _health < 0.0f || _health > rules._maxHealth || _saveCount < 0 || _wrongDeductionCount < 0 || bLimitedOver;
        if ( bCountsInvalid )
            return false;
        for ( const hashed_string& monsterID : _uniqueSeenMonster )
        {
            if ( _pCatalog->findMonster( monsterID ) == nullptr )
                return false;
        }
        for ( const hashed_string& documentID : _uniqueReadDocument )
        {
            if ( _pCatalog->findDocument( documentID ) == nullptr )
                return false;
        }
        for ( const hashed_string& puzzleID : _uniqueSolvedPuzzle )
        {
            const bool bKnownPuzzle = _pCatalog->findKeyLock( puzzleID ) != nullptr || _pCatalog->findDialLock( puzzleID ) != nullptr || _pCatalog->findSequence( puzzleID ) != nullptr || _pCatalog->findDeduction( puzzleID ) != nullptr;
            if ( bKnownPuzzle == false )
                return false;
        }
        for ( const auto& [lockID, wrongCount] : _mapDialAttempt )
        {
            const SurvivalHorrorDialLockDef* pLock           = _pCatalog->findDialLock( lockID );
            const bool                       bInvalidAttempt = pLock == nullptr || wrongCount < 0 || ( pLock->_maxAttempts > 0 && wrongCount > pLock->_maxAttempts );
            if ( bInvalidAttempt )
                return false;
        }
        // 순서 퍼즐 진행은 다음 걸음의 index 이므로 그 퍼즐의 걸음 수 안이어야 한다.
        for ( const auto& [puzzleID, progress] : _mapSequenceProgress )
        {
            const SurvivalHorrorSequenceDef* pSequence                = _pCatalog->findSequence( puzzleID );
            const bool                       bInvalidSequenceProgress = pSequence == nullptr || progress < 0 || progress >= static_cast<int32>( pSequence->_listStep.size() );
            if ( bInvalidSequenceProgress )
                return false;
        }
        // 보드의 규칙(`linkClues`)과 같다. 가진 단서끼리이고, 자기 자신과 겹침은 없다.
        for ( size_t linkIndex = 0; linkIndex < _listClueLink.size(); ++linkIndex )
        {
            const SurvivalHorrorClueLink& link     = _listClueLink[linkIndex];
            bool                          bInvalid = link._first == link._second || hasClue( link._first ) == false || hasClue( link._second ) == false;
            for ( size_t earlier = 0; earlier < linkIndex; ++earlier )
            {
                bInvalid = bInvalid || _listClueLink[earlier].isSame( link._first, link._second );
            }
            if ( bInvalid )
                return false;
        }
        return _currentArea.empty() || _pAreaGraph == nullptr || _pAreaGraph->findArea( _currentArea ) != nullptr;
    }

    void SurvivalHorrorSession::pushEvent( SurvivalHorrorEvent::Kind kind, const hashed_string& id, float32 value )
    {
        SurvivalHorrorEvent event;
        event._kind  = kind;
        event._id    = id;
        event._value = value;
        _eventBuffer.push( event );
    }

    void SurvivalHorrorSession::refreshHallucination()
    {
        const float32 threshold = _pCatalog != nullptr ? _pCatalog->getRules()._hallucinationThreshold : 0.0f;
        const bool    bNow      = _sanity.getRatio() < threshold;
        if ( bNow == ( _bHallucinating == SW_TRUE ) )
            return;
        _bHallucinating = bNow ? SW_TRUE : SW_FALSE;
        pushEvent( bNow ? SurvivalHorrorEvent::Kind::HallucinationStarted : SurvivalHorrorEvent::Kind::HallucinationEnded, hashed_string() );
    }

    bool SurvivalHorrorSession::markSolved( const hashed_string& puzzleID, const hashed_string& flag )
    {
        if ( flag.empty() == false && _pFlags != nullptr )
            _pFlags->setFlag( flag, 1 );
        return _uniqueSolvedPuzzle.insert( puzzleID ).second;
    }
} // namespace sw
