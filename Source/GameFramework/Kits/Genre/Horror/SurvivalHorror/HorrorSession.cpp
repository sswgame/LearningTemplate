#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/HorrorSession.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct HorrorSessionInternal
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

            /** @brief 이름 집합을 이름 순으로 씁니다 — 같은 집합이면 같은 바이트입니다. */
            static void writeSortedSet( Archive& outArchive, const unordered_set<hashed_string>& uniqueName )
            {
                vector<hashed_string> listName( uniqueName.begin(), uniqueName.end() );
                std::sort( listName.begin(), listName.end(), HashedStringLexicalLess{} );
                outArchive << static_cast<uint32>( listName.size() );
                for ( const hashed_string& name : listName )
                {
                    StateArchiveUtil::writeName( outArchive, name );
                }
            }

            [[nodiscard]] static bool readSet( Archive& archive, unordered_set<hashed_string>& outUniqueName )
            {
                uint32 count = 0;
                if ( StateArchiveUtil::readCount( archive, 4, count ) == false )
                    return false;
                outUniqueName.clear();
                for ( uint32 index = 0; index < count; ++index )
                {
                    hashed_string name;
                    if ( StateArchiveUtil::readName( archive, name ) == false || outUniqueName.insert( name ).second == false )
                        return false;
                }
                return true;
            }

            /** @brief 이름 → 수 맵을 이름 순으로 씁니다 — 같은 맵이면 같은 바이트입니다. */
            static void writeSortedMap( Archive& outArchive, const unordered_map<hashed_string, int32>& mapNameToCount )
            {
                vector<hashed_string> listName;
                listName.reserve( mapNameToCount.size() );
                for ( const auto& [name, count] : mapNameToCount )
                {
                    listName.push_back( name );
                }
                std::sort( listName.begin(), listName.end(), HashedStringLexicalLess{} );
                outArchive << static_cast<uint32>( listName.size() );
                for ( const hashed_string& name : listName )
                {
                    StateArchiveUtil::writeName( outArchive, name );
                    outArchive << mapNameToCount.find( name )->second;
                }
            }

            [[nodiscard]] static bool readMap( Archive& archive, unordered_map<hashed_string, int32>& outMapNameToCount )
            {
                uint32 count = 0;
                // 이름(4) + 수(4)
                if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
                    return false;
                outMapNameToCount.clear();
                for ( uint32 index = 0; index < count; ++index )
                {
                    hashed_string name;
                    int32         value = 0;
                    if ( StateArchiveUtil::readName( archive, name ) == false )
                        return false;
                    archive >> value;
                    if ( archive.isError() || outMapNameToCount.emplace( name, value ).second == false )
                        return false;
                }
                return true;
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

    void HorrorSession::initialize( const HorrorCatalog* pCatalog, AreaGraph* pAreaGraph, const hashed_string& startArea, const GameStateRefs& refs,
                                    GridInventory& inventory, Inventory& itemBox )
    {
        _pCatalog                       = pCatalog;
        _pAreaGraph                     = pAreaGraph;
        const SurvivalHorrorRules rules = pCatalog != nullptr ? pCatalog->getRules() : SurvivalHorrorRules{};
        _pInventory                     = &inventory;
        _pItemBox                       = &itemBox;
        _pFlags                         = refs._pFlags;
        _pInventory->initialize( pCatalog != nullptr ? pCatalog->makeShapeLookup() : GridInventory::ShapeDelegate{}, rules._gridWidth, rules._gridHeight );
        _sanity.initialize( HorrorSessionInternal::makeSanitySettings( rules ) );
        _battery.initialize( HorrorSessionInternal::makeBatterySettings( rules ) );
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
    }

    void HorrorSession::update( float32 deltaTime, bool bInDarkness )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _bFlashlightOn == SW_TRUE && _battery.drain( deltaTime ) == false )
        {
            _bFlashlightOn = SW_FALSE;
            pushEvent( SurvivalHorrorEvent::Kind::FlashlightDied, hashed_string() );
        }
        // 어둠은 매 프레임 조금씩이라 알림을 내지 않는다(환각 시작 · 끝만 알린다).
        if ( bInDarkness && _bFlashlightOn == SW_FALSE )
            (void)_sanity.drain( deltaTime ); // 0 에 붙어 있어도 계속 깎는 중 — 회복 지연이 다시 센다
        _sanity.update( deltaTime );
        _battery.update( deltaTime );
        refreshHallucination();
    }

    bool HorrorSession::storeInBox( int32 instanceID, int32 count )
    {
        const GridItem* pItem = _pInventory->findInstance( instanceID );
        if ( pItem == nullptr || count <= 0 || pItem->_count < count )
            return false;
        const hashed_string itemID = pItem->_itemID;
        if ( _pItemBox->hasRoomFor( itemID, count ) == false )
            return false;
        const int32 taken = _pInventory->takeFromInstance( instanceID, count );
        const int32 added = _pItemBox->addItem( itemID, taken );
        return taken == count && added == taken;
    }

    int32 HorrorSession::takeFromBox( const hashed_string& itemID, int32 count )
    {
        const int32 wanted = MathUtil::min( count, _pItemBox->getItemCount( itemID ) );
        if ( wanted <= 0 )
            return 0;
        const int32 added = _pInventory->addItem( itemID, wanted );
        if ( added > 0 )
            (void)_pItemBox->removeItem( itemID, added ); // added 는 상자에 있던 수 이하라 늘 빠진다
        return added;
    }

    bool HorrorSession::combineItems( int32 firstInstanceID, int32 secondInstanceID )
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
            // 결과가 다 들어가지 않으면 없던 일로 — 넣은 결과를 빼고 재료를 돌려놓는다(재료가 비운 자리라 늘 들어간다).
            for ( size_t outputIndex = 0; outputIndex < listOutput.size(); ++outputIndex )
            {
                if ( listAdded[outputIndex] > 0 )
                    (void)_pInventory->removeItem( listOutput[outputIndex], listAdded[outputIndex] ); // 방금 넣은 수라 늘 빠진다
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

    bool HorrorSession::tryConsumeAmmo( const hashed_string& ammoItemID, int32 count )
    {
        return _pInventory->removeItem( ammoItemID, count );
    }

    bool HorrorSession::tryUseItem( int32 instanceID )
    {
        const GridItem* pItem = _pInventory->findInstance( instanceID );
        if ( pItem == nullptr || _pCatalog == nullptr )
            return false;
        const HorrorItemDef* pDef = _pCatalog->findItem( pItem->_itemID );
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

    void HorrorSession::applyDamage( float32 amount )
    {
        _health = MathUtil::max( 0.0f, _health - MathUtil::max( 0.0f, amount ) );
    }

    HorrorSaveResult HorrorSession::trySave()
    {
        const SurvivalHorrorRules rules = _pCatalog != nullptr ? _pCatalog->getRules() : SurvivalHorrorRules{};
        if ( rules._saveMode == HorrorSaveMode::Limited && _saveCount >= rules._maxSaves )
            return HorrorSaveResult::NoSavesLeft;
        if ( rules._saveMode == HorrorSaveMode::InkRibbon )
        {
            // 가방에서 처음 찾은 SaveItem 하나를 쓴다(놓은 순서 — 결정적).
            int32 ribbonInstance = -1;
            for ( const GridItem& item : _pInventory->getItems() )
            {
                const HorrorItemDef* pDef = _pCatalog->findItem( item._itemID );
                if ( pDef != nullptr && pDef->_kind == HorrorItemKind::SaveItem )
                {
                    ribbonInstance = item._instanceID;
                    break;
                }
            }
            if ( ribbonInstance < 0 )
                return HorrorSaveResult::NoSaveItem;
            (void)_pInventory->takeFromInstance( ribbonInstance, 1 );
        }
        ++_saveCount;
        pushEvent( SurvivalHorrorEvent::Kind::Saved, hashed_string(), static_cast<float32>( _saveCount ) );
        return HorrorSaveResult::Ok;
    }

    bool HorrorSession::trySetFlashlight( bool bOn )
    {
        if ( bOn && _battery.getValue() <= 0.0f )
            return false;
        _bFlashlightOn = bOn ? SW_TRUE : SW_FALSE;
        return true;
    }

    float32 HorrorSession::witnessMonster( const hashed_string& monsterID )
    {
        const HorrorMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterID ) : nullptr;
        if ( pMonster == nullptr )
            return 0.0f;
        const bool    bFirstSight = _uniqueSeenMonster.insert( monsterID ).second;
        const float32 loss        = bFirstSight ? pMonster->_sanityLoss : pMonster->_sanityLoss * _pCatalog->getRules()._repeatSightingScale;
        const float32 before      = _sanity.getValue();
        loseSanity( loss, monsterID );
        return before - _sanity.getValue();
    }

    void HorrorSession::loseSanity( float32 amount, const hashed_string& cause )
    {
        const float32 lost = _sanity.reduce( amount );
        if ( lost <= 0.0f )
            return;
        pushEvent( SurvivalHorrorEvent::Kind::SanityLost, cause, lost );
        refreshHallucination();
    }

    float32 HorrorSession::computeAimSwayScale() const
    {
        const float32 maxSway = _pCatalog != nullptr ? _pCatalog->getRules()._maxAimSway : 0.0f;
        return 1.0f + ( 1.0f - MathUtil::saturate( _sanity.getRatio() ) ) * maxSway;
    }

    HorrorPuzzleResult HorrorSession::useKey( const hashed_string& lockID )
    {
        const HorrorKeyLockDef* pLock = _pCatalog != nullptr ? _pCatalog->findKeyLock( lockID ) : nullptr;
        if ( pLock == nullptr )
            return HorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( lockID ) > 0 )
            return HorrorPuzzleResult::AlreadySolved;
        if ( _pInventory->hasItem( pLock->_keyItem ) == false )
            return HorrorPuzzleResult::MissingItem;
        if ( pLock->_bConsumeKey == SW_TRUE )
            (void)_pInventory->removeItem( pLock->_keyItem, 1 ); // 열쇠는 위 hasItem 이 확인했다
        (void)markSolved( lockID, pLock->_flag );
        pushEvent( SurvivalHorrorEvent::Kind::DoorUnlocked, lockID );
        return HorrorPuzzleResult::Solved;
    }

    HorrorPuzzleResult HorrorSession::enterDialCode( const hashed_string& lockID, const vector<int32>& listDigit )
    {
        const HorrorDialLockDef* pLock = _pCatalog != nullptr ? _pCatalog->findDialLock( lockID ) : nullptr;
        if ( pLock == nullptr )
            return HorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( lockID ) > 0 )
            return HorrorPuzzleResult::AlreadySolved;
        int32& wrongCount = _mapDialAttempt[lockID];
        if ( pLock->_maxAttempts > 0 && wrongCount >= pLock->_maxAttempts )
            return HorrorPuzzleResult::LockedOut;
        if ( listDigit != pLock->_listDigit )
        {
            ++wrongCount;
            return pLock->_maxAttempts > 0 && wrongCount >= pLock->_maxAttempts ? HorrorPuzzleResult::LockedOut : HorrorPuzzleResult::Wrong;
        }
        (void)markSolved( lockID, pLock->_flag );
        pushEvent( SurvivalHorrorEvent::Kind::PuzzleSolved, lockID );
        return HorrorPuzzleResult::Solved;
    }

    HorrorPuzzleResult HorrorSession::pressSequenceStep( const hashed_string& puzzleID, const hashed_string& step )
    {
        const HorrorSequenceDef* pSequence = _pCatalog != nullptr ? _pCatalog->findSequence( puzzleID ) : nullptr;
        if ( pSequence == nullptr || pSequence->_listStep.empty() )
            return HorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( puzzleID ) > 0 )
            return HorrorPuzzleResult::AlreadySolved;
        int32& progress = _mapSequenceProgress[puzzleID];
        if ( pSequence->_listStep[static_cast<size_t>( progress )] != step )
        {
            progress = 0;
            loseSanity( pSequence->_mistakeSanity, puzzleID );
            return HorrorPuzzleResult::Wrong;
        }
        ++progress;
        if ( progress < static_cast<int32>( pSequence->_listStep.size() ) )
            return HorrorPuzzleResult::Progress;
        (void)markSolved( puzzleID, pSequence->_flag );
        pushEvent( SurvivalHorrorEvent::Kind::PuzzleSolved, puzzleID );
        return HorrorPuzzleResult::Solved;
    }

    bool HorrorSession::tryMoveTo( const hashed_string& areaID )
    {
        static const GameFlags kNoFlags; // 빌리지 않은 세션 — 조건 있는 길은 닫힌 채다
        if ( _pAreaGraph == nullptr || _pAreaGraph->canTraverse( _currentArea, areaID, _pFlags != nullptr ? *_pFlags : kNoFlags ) == false )
            return false;
        (void)_pAreaGraph->enterArea( areaID );
        _currentArea = areaID;
        pushEvent( SurvivalHorrorEvent::Kind::AreaEntered, areaID );
        return true;
    }

    bool HorrorSession::readDocument( const hashed_string& documentID )
    {
        const HorrorDocumentDef* pDocument = _pCatalog != nullptr ? _pCatalog->findDocument( documentID ) : nullptr;
        if ( pDocument == nullptr || _uniqueReadDocument.insert( documentID ).second == false )
            return false;
        pushEvent( SurvivalHorrorEvent::Kind::DocumentRead, documentID );
        for ( const hashed_string& clueID : pDocument->_listClue )
        {
            addClue( clueID );
        }
        return true;
    }

    void HorrorSession::addClue( const hashed_string& clueID )
    {
        if ( clueID.empty() == false && _uniqueClue.insert( clueID ).second )
            pushEvent( SurvivalHorrorEvent::Kind::ClueGained, clueID );
    }

    bool HorrorSession::hasClue( const hashed_string& clueID ) const
    {
        return _uniqueClue.count( clueID ) > 0;
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

    HorrorPuzzleResult HorrorSession::submitDeduction( const hashed_string& deductionID, const hashed_string& answer )
    {
        const HorrorDeductionDef* pDeduction = _pCatalog != nullptr ? _pCatalog->findDeduction( deductionID ) : nullptr;
        if ( pDeduction == nullptr )
            return HorrorPuzzleResult::Unknown;
        if ( _uniqueSolvedPuzzle.count( deductionID ) > 0 )
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
            loseSanity( pDeduction->_wrongSanity, deductionID );
            if ( _pCatalog->getRules()._bClearLinksOnWrong == SW_TRUE )
                _listClueLink.clear();
            pushEvent( SurvivalHorrorEvent::Kind::DeductionFailed, deductionID );
            return HorrorPuzzleResult::Wrong;
        }
        (void)markSolved( deductionID, pDeduction->_flag );
        pushEvent( SurvivalHorrorEvent::Kind::DeductionSolved, deductionID );
        return HorrorPuzzleResult::Solved;
    }

    void HorrorSession::drainEvents( vector<SurvivalHorrorEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void HorrorSession::writeState( Archive& outArchive ) const
    {
        _sanity.writeState( outArchive );
        _battery.writeState( outArchive );
        HorrorSessionInternal::writeSortedSet( outArchive, _uniqueSeenMonster );
        HorrorSessionInternal::writeSortedSet( outArchive, _uniqueReadDocument );
        HorrorSessionInternal::writeSortedSet( outArchive, _uniqueClue );
        HorrorSessionInternal::writeSortedSet( outArchive, _uniqueSolvedPuzzle );
        HorrorSessionInternal::writeSortedMap( outArchive, _mapDialAttempt );
        HorrorSessionInternal::writeSortedMap( outArchive, _mapSequenceProgress );
        outArchive << static_cast<uint32>( _listClueLink.size() );
        for ( const HorrorClueLink& link : _listClueLink )
        {
            StateArchiveUtil::writeName( outArchive, link._first );
            StateArchiveUtil::writeName( outArchive, link._second );
        }
        StateArchiveUtil::writeName( outArchive, _currentArea );
        outArchive << _health;
        outArchive << _saveCount;
        outArchive << _wrongDeductionCount;
        outArchive << _bFlashlightOn;
        outArchive << _bHallucinating;
    }

    bool HorrorSession::readState( Archive& archive )
    {
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 카탈로그 · 빌린 포인터 · 게이지 설정은 사본이 그대로 든다.
        HorrorSession restored = *this;
        const bool    bPartRead =
            restored._sanity.readState( archive ) && restored._battery.readState( archive ) && HorrorSessionInternal::readSet( archive, restored._uniqueSeenMonster ) &&
            HorrorSessionInternal::readSet( archive, restored._uniqueReadDocument ) && HorrorSessionInternal::readSet( archive, restored._uniqueClue ) &&
            HorrorSessionInternal::readSet( archive, restored._uniqueSolvedPuzzle ) && HorrorSessionInternal::readMap( archive, restored._mapDialAttempt ) &&
            HorrorSessionInternal::readMap( archive, restored._mapSequenceProgress );
        uint32 linkCount = 0;
        // 연결마다 이름 둘(8)
        if ( bPartRead == false || StateArchiveUtil::readCount( archive, 8, linkCount ) == false )
            return false;
        restored._listClueLink.assign( linkCount, HorrorClueLink{} );
        for ( HorrorClueLink& link : restored._listClueLink )
        {
            if ( StateArchiveUtil::readName( archive, link._first ) == false || StateArchiveUtil::readName( archive, link._second ) == false )
                return false;
        }
        if ( StateArchiveUtil::readName( archive, restored._currentArea ) == false )
            return false;
        archive >> restored._health;
        archive >> restored._saveCount;
        archive >> restored._wrongDeductionCount;
        archive >> restored._bFlashlightOn;
        archive >> restored._bHallucinating;
        const bool bValid = archive.isOk() && 0 <= restored._saveCount && 0 <= restored._wrongDeductionCount && restored._bFlashlightOn <= SW_TRUE &&
                            restored._bHallucinating <= SW_TRUE;
        if ( bValid == false )
            return false;
        // 순서 퍼즐 진행은 다음 걸음의 자리라 그 퍼즐의 걸음 수 안이어야 한다.
        for ( const auto& [puzzleID, progress] : restored._mapSequenceProgress )
        {
            const HorrorSequenceDef* pSequence = _pCatalog != nullptr ? _pCatalog->findSequence( puzzleID ) : nullptr;
            if ( pSequence == nullptr || progress < 0 || progress >= static_cast<int32>( pSequence->_listStep.size() ) )
                return false;
        }
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }

    void HorrorSession::pushEvent( SurvivalHorrorEvent::Kind kind, const hashed_string& id, float32 value )
    {
        SurvivalHorrorEvent event;
        event._kind  = kind;
        event._id    = id;
        event._value = value;
        _eventBuffer.push( event );
    }

    void HorrorSession::refreshHallucination()
    {
        const float32 threshold = _pCatalog != nullptr ? _pCatalog->getRules()._hallucinationThreshold : 0.0f;
        const bool    bNow      = _sanity.getRatio() < threshold;
        if ( bNow == ( _bHallucinating == SW_TRUE ) )
            return;
        _bHallucinating = bNow ? SW_TRUE : SW_FALSE;
        pushEvent( bNow ? SurvivalHorrorEvent::Kind::HallucinationStarted : SurvivalHorrorEvent::Kind::HallucinationEnded, hashed_string() );
    }

    bool HorrorSession::markSolved( const hashed_string& puzzleID, const hashed_string& flag )
    {
        if ( flag.empty() == false && _pFlags != nullptr )
            _pFlags->setFlag( flag, 1 );
        return _uniqueSolvedPuzzle.insert( puzzleID ).second;
    }
} // namespace sw
