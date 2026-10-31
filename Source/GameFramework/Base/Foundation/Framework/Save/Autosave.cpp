#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Save/Autosave.h"

#include "Core/Container/CompareUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/UI/Screen/UINotificationService.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Save/GameLocalStore.h"
#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Online/Local/LocalStore.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "Autosave" );

    namespace
    {
        struct AutosaveInternal
        {
            static constexpr uint32      kInfoTag           = FourCcUtil::make( "ASAV" );
            static constexpr uint32      kInfoVersion       = 1;
            static constexpr uint32      kSlotFormatVersion = 1; ///< 상태 슬롯의 봉투 형식 버전 - 게임 상태 바이트(스냅샷 봉투)를 바꾸면 올린다
            static constexpr const utf8* kInfoExtension     = ".info";
            static constexpr int32       kMaxSlotCount      = 64;
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 정책 수치
    // ------------------------------------------------------------------------------
    bool AutosaveSettings::loadFromAsset( const AssetID& assetID )
    {
        return GameDataXML::loadFile( *this, &AutosaveSettings::loadRoot, assetID, "Autosave" );
    }

    bool AutosaveSettings::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        return GameDataXML::loadText( *this, &AutosaveSettings::loadRoot, xmlText, sourceName, "Autosave" );
    }

    bool AutosaveSettings::loadRoot( const XMLNode& root, string_view sourceName )
    {
        const utf8* pGroup  = root.findAttribute( "group" );
        const utf8* pPrefix = root.findAttribute( "prefix" );
        // 적지 않은 칸은 생성자의 초기값이다. 깨진 값 · 범위 밖 값은 읽기 실패이고 그 칸은 바꾸지 않는다(자르거나 다른 값으로 바꿔 쓰지 않는다).
        bool bValid = true;
        if ( pGroup != nullptr )
        {
            // 자리(폴더)는 로컬 저장소가 정한다 - 데이터는 슬롯 그룹 이름만 적는다(경로 · `..` 는 슬롯 이름 규칙에 걸린다).
            if ( ILocalStore::isValidGroupPrefix( string( pGroup ) + "/" ) )
            {
                _group = pGroup;
            }
            else
            {
                SW_LOG_ERROR( "%#: Autosave group '%#' must be one local store slot group ([0-9a-z_.-], not starting with '.')", sourceName, pGroup );
                bValid = false;
            }
        }
        if ( pPrefix != nullptr )
            _prefix = pPrefix;
        bValid = XMLReadUtil::readOptionalNumber( root, "interval", sourceName, 0.0f, MathUtil::kMaxFloat, _interval ) && bValid;
        bValid = XMLReadUtil::readOptionalNumber( root, "minimumGap", sourceName, 0.0f, MathUtil::kMaxFloat, _minimumGap ) && bValid;
        bValid = XMLReadUtil::readOptionalNumber( root, "slots", sourceName, 1, AutosaveInternal::kMaxSlotCount, _slotCount ) && bValid;
        bValid = XMLReadUtil::readOptionalFlag( root, "areaChange", _bOnAreaChange ) && bValid;
        bValid = XMLReadUtil::readOptionalFlag( root, "checkpoint", _bOnCheckpoint ) && bValid;
        bValid = XMLReadUtil::readOptionalFlag( root, "beforeBoss", _bOnBeforeBoss ) && bValid;
        bValid = XMLReadUtil::readOptionalFlag( root, "quit", _bOnQuit ) && bValid;
        return bValid;
    }

    string AutosaveSettings::makeSlotName( int32 slot ) const
    {
        return formatToString( "%#/%#%#", _group, _prefix, slot );
    }

    string AutosaveSettings::makeInfoSlotName( int32 slot ) const
    {
        return makeSlotName( slot ) + AutosaveInternal::kInfoExtension;
    }

    // ------------------------------------------------------------------------------
    // 관리자
    // ------------------------------------------------------------------------------
    AutosaveManager::AutosaveManager()
        : _settings{}
        , _onSave{}
        , _onLoad{}
        , _pStore{ nullptr }
        , _listSlot{}
        , _listBlockReason{}
        , _savingSlot{}
        , _pendingLabel{}
        , _lastCheckpointID{}
        , _restoringCheckpointID{}
        , _playTime{ 0.0 }
        , _lastSaveTime{ -1.0 }
        , _intervalTimer{ 0.0f }
        , _nextSequence{ 1 }
        , _saveCount{ 0 }
        , _scanPendingCount{ 0 }
        , _pendingTrigger{ AutosaveTrigger::Manual }
        , _bPending{ SW_FALSE }
        , _bSaving{ SW_FALSE }
        , _listScanRequest{}
        , _saveRequest{}
        , _infoRequest{}
        , _restoreRequest{}
    {
    }

    bool AutosaveManager::initialize( const AutosaveSettings& settings, GameLocalStore& store, const SaveDelegate& onSave, const LoadDelegate& onLoad )
    {
        const bool bRangesValid = 1 <= settings._slotCount && settings._slotCount <= AutosaveInternal::kMaxSlotCount && 0.0f <= settings._interval && 0.0f <= settings._minimumGap;
        // 가장 긴 이름(마지막 슬롯의 기록)이 규칙을 지키면 모든 슬롯 이름이 지킨다.
        const bool bNamesValid = bRangesValid && ILocalStore::isValidSlotName( settings.makeInfoSlotName( settings._slotCount - 1 ) );
        if ( bNamesValid == false )
        {
            SW_LOG_ERROR( "AutosaveManager: refused settings - slots %# (1..%#), interval %# and minimumGap %# (0 or more), slot names '%#' (local store slot name rule)",
                          settings._slotCount, AutosaveInternal::kMaxSlotCount, settings._interval, settings._minimumGap, settings.makeInfoSlotName( 0 ).c_str() );
            return false;
        }
        _settings = settings;
        _pStore   = &store;
        _onSave   = onSave;
        _onLoad   = onLoad;
        _listBlockReason.clear();
        _pendingLabel          = hashed_string{};
        _lastCheckpointID      = hashed_string{};
        _restoringCheckpointID = hashed_string{};
        _playTime              = 0.0;
        _lastSaveTime          = -1.0;
        _intervalTimer         = 0.0f;
        _saveCount             = 0;
        _bPending              = SW_FALSE;
        _bSaving               = SW_FALSE;
        // 앞 초기화가 맡긴 요청은 잊는다(구독을 놓으면 콜백이 불리지 않는다).
        _saveRequest    = Subscription{};
        _infoRequest    = Subscription{};
        _restoreRequest = Subscription{};
        scanSlots();
        return true;
    }

    void AutosaveManager::update( float32 deltaTime )
    {
        if ( deltaTime > 0.0f )
        {
            _playTime += static_cast<float64>( deltaTime );
            if ( _settings._interval > 0.0f )
            {
                _intervalTimer += deltaTime;
                if ( _intervalTimer >= _settings._interval )
                {
                    _intervalTimer = 0.0f;
                    requestSave( AutosaveTrigger::Interval );
                }
            }
        }
        // 슬롯 기록을 읽는 중이면 다음 슬롯을 모르고, 맡긴 저장이 끝나기 전이면 가장 새 슬롯이 아직 바뀌지 않았다 - 기다린다.
        const bool bNothingToSaveNow = _bPending == SW_FALSE || isBlocked() || _bSaving == SW_TRUE || isScanning() || isSaving();
        if ( bNothingToSaveNow )
            return;
        // 최소 간격 — 지역을 빠르게 오가도 저장이 줄지어 서지 않는다. 체크포인트 · 보스 앞은 간격을 지키되 기다렸다 저장한다(버리지 않는다).
        const bool bTooSoon = _lastSaveTime >= 0.0 && _playTime - _lastSaveTime < static_cast<float64>( _settings._minimumGap );
        if ( bTooSoon )
            return;
        const AutosaveTrigger trigger = _pendingTrigger;
        const hashed_string   label   = _pendingLabel;
        _bPending                     = SW_FALSE;
        _pendingLabel                 = hashed_string{};
        if ( performSave( trigger, label ) == false )
            SW_LOG_WARNING( "Autosave (%#) failed - the previous slots are kept", getEnumName( trigger ) );
    }

    void AutosaveManager::requestSave( AutosaveTrigger trigger, const hashed_string& label )
    {
        if ( isTriggerEnabled( trigger ) == false )
            return;
        // 쌓인 요청은 하나다. 더 중요한 까닭이 칸 기록에 남는다(같으면 나중 이름).
        if ( _bPending == SW_TRUE && getTriggerPriority( trigger ) < getTriggerPriority( _pendingTrigger ) )
            return;
        _pendingTrigger = trigger;
        _pendingLabel   = label;
        _bPending       = SW_TRUE;
    }

    void AutosaveManager::reachCheckpoint( const hashed_string& checkpointID )
    {
        if ( checkpointID.empty() || checkpointID == _lastCheckpointID )
            return;
        _lastCheckpointID = checkpointID;
        requestSave( AutosaveTrigger::Checkpoint, checkpointID );
    }

    bool AutosaveManager::notifyQuit()
    {
        if ( _settings._bOnQuit == SW_FALSE || _bSaving == SW_TRUE || isScanning() || isSaving() )
            return false;
        _bPending = SW_FALSE;
        return performSave( AutosaveTrigger::Quit, hashed_string{} );
    }

    bool AutosaveManager::saveNow( AutosaveTrigger trigger, const hashed_string& label )
    {
        if ( isBlocked() || _bSaving == SW_TRUE || isScanning() || isSaving() )
            return false;
        _bPending = SW_FALSE;
        return performSave( trigger, label );
    }

    void AutosaveManager::setBlocked( const hashed_string& reason )
    {
        for ( const hashed_string& existing : _listBlockReason )
        {
            if ( existing == reason )
                return;
        }
        _listBlockReason.push_back( reason );
    }

    void AutosaveManager::clearBlocked( const hashed_string& reason )
    {
        for ( size_t index = 0; index < _listBlockReason.size(); ++index )
        {
            if ( _listBlockReason[index] == reason )
            {
                _listBlockReason.erase( _listBlockReason.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    bool AutosaveManager::restoreLatest()
    {
        return submitRestore( findLatestSlot(), false );
    }

    bool AutosaveManager::restoreCheckpoint()
    {
        return submitRestore( findLatestSlot( AutosaveTrigger::Checkpoint ), true );
    }

    bool AutosaveManager::submitRestore( const AutosaveSlotInfo* pSlot, bool bCheckpoint )
    {
        if ( pSlot == nullptr || _onLoad.isBound() == false || isScanning() )
            return false;
        _restoringCheckpointID = bCheckpoint ? pSlot->_label : hashed_string{};
        _restoreRequest        = _pStore->submitRead( pSlot->_slotName, GameLocalStore::CompletionFunction::create<&AutosaveManager::onSlotRead>( this ) );
        return true;
    }

    void AutosaveManager::onSlotRead( const LocalStoreCompletion& completion )
    {
        const hashed_string checkpointID = _restoringCheckpointID;
        _restoringCheckpointID           = hashed_string{};
        if ( completion._result != LocalStoreResult::Ok || completion._formatVersion != AutosaveInternal::kSlotFormatVersion )
        {
            SW_LOG_WARNING( "Autosave slot %# could not be restored (local store result %#, format version %#, this build reads %#)", completion._slot.c_str(),
                            static_cast<int32>( completion._result ), completion._formatVersion, AutosaveInternal::kSlotFormatVersion );
            return;
        }
        if ( _onLoad( completion._bytes ) == false )
        {
            SW_LOG_WARNING( "Autosave slot %# was read but the game could not restore it", completion._slot.c_str() );
            return;
        }
        if ( checkpointID.empty() )
            return;
        _lastCheckpointID = checkpointID; // 되돌린 체크포인트에 다시 서도 저장하지 않는다
        _bPending         = SW_FALSE;
    }

    void AutosaveManager::collectSlots( vector<AutosaveSlotInfo>& outListSlot ) const
    {
        outListSlot.clear();
        for ( const AutosaveSlotInfo& slot : _listSlot )
        {
            if ( slot._sequence != 0 )
                outListSlot.push_back( slot );
        }
        VectorUtil::sort( outListSlot, Descending<&AutosaveSlotInfo::_sequence>{} );
    }

    const AutosaveSlotInfo* AutosaveManager::findLatestSlot() const
    {
        const AutosaveSlotInfo* pLatest = nullptr;
        for ( const AutosaveSlotInfo& slot : _listSlot )
        {
            if ( slot._sequence != 0 && ( pLatest == nullptr || slot._sequence > pLatest->_sequence ) )
                pLatest = &slot;
        }
        return pLatest;
    }

    const AutosaveSlotInfo* AutosaveManager::findLatestSlot( AutosaveTrigger trigger ) const
    {
        const AutosaveSlotInfo* pLatest = nullptr;
        for ( const AutosaveSlotInfo& slot : _listSlot )
        {
            const bool bLatestOfTrigger = slot._sequence != 0 && slot._trigger == trigger && ( pLatest == nullptr || slot._sequence > pLatest->_sequence );
            if ( bLatestOfTrigger )
                pLatest = &slot;
        }
        return pLatest;
    }

    bool AutosaveManager::isTriggerEnabled( AutosaveTrigger trigger ) const
    {
        switch ( trigger )
        {
            case AutosaveTrigger::Interval:
                return _settings._interval > 0.0f;
            case AutosaveTrigger::AreaChanged:
                return _settings._bOnAreaChange != SW_FALSE;
            case AutosaveTrigger::Checkpoint:
                return _settings._bOnCheckpoint != SW_FALSE;
            case AutosaveTrigger::BeforeBoss:
                return _settings._bOnBeforeBoss != SW_FALSE;
            case AutosaveTrigger::Quit:
                return _settings._bOnQuit != SW_FALSE;
            case AutosaveTrigger::Manual:
                return true;
        }
        return false;
    }

    int32 AutosaveManager::getTriggerPriority( AutosaveTrigger trigger )
    {
        switch ( trigger )
        {
            case AutosaveTrigger::Interval:
                return 0;
            case AutosaveTrigger::AreaChanged:
                return 1;
            case AutosaveTrigger::BeforeBoss:
                return 2;
            case AutosaveTrigger::Checkpoint:
                return 3;
            case AutosaveTrigger::Manual:
                return 4;
            case AutosaveTrigger::Quit:
                return 5;
        }
        return 0;
    }

    bool AutosaveManager::performSave( AutosaveTrigger trigger, const hashed_string& label )
    {
        if ( _pStore == nullptr || _onSave.isBound() == false || _listSlot.empty() )
            return false;
        // 가장 새 칸의 다음 칸 — 가장 새 칸은 건드리지 않는다(저장 도중 꺼져도 남는다).
        const AutosaveSlotInfo* pLatest = findLatestSlot();
        const int32             slot    = pLatest != nullptr ? ( pLatest->_slot + 1 ) % _settings._slotCount : 0;
        AutosaveSlotInfo        info{};
        info._slotName = _settings.makeSlotName( slot );
        info._label    = label;
        info._sequence = _nextSequence;
        info._playTime = _playTime;
        info._slot     = slot;
        info._trigger  = trigger;

        vector<uint8> stateBytes;
        _bSaving             = SW_TRUE;
        const bool bCaptured = _onSave( stateBytes );
        _bSaving             = SW_FALSE;
        vector<uint8> infoBytes;
        if ( bCaptured == false || writeSlotInfo( info, infoBytes ) == false )
            return false;

        LocalStoreWriteOptions stateOptions{};
        stateOptions._formatVersion = AutosaveInternal::kSlotFormatVersion;
        LocalStoreWriteOptions infoOptions{};
        infoOptions._formatVersion = AutosaveInternal::kInfoVersion;
        _savingSlot                = info;
        // 기록은 상태 바로 뒤에 맡긴다 - 저장소는 맡은 순서로 쓰므로, 끝낼 때 맡긴 종료 저장도 기록까지 남는다(완료를 기다렸다 맡기면 저장소가 닫힌 뒤가 된다).
        _saveRequest = _pStore->submitWrite( info._slotName, std::move( stateBytes ), stateOptions, GameLocalStore::CompletionFunction::create<&AutosaveManager::onSlotWritten>( this ) );
        _infoRequest = _pStore->submitWrite( _settings.makeInfoSlotName( slot ), std::move( infoBytes ), infoOptions,
                                             GameLocalStore::CompletionFunction::create<&AutosaveManager::onInfoWritten>( this ) );
        return true;
    }

    void AutosaveManager::onSlotWritten( const LocalStoreCompletion& completion )
    {
        const AutosaveSlotInfo& info = _savingSlot;
        if ( completion._result != LocalStoreResult::Ok )
        {
            // 상태 슬롯은 원자 쓰기라 옛 상태가 남았는데 기록은 새 순번으로 쓰였을 수 있다 - 기록을 지워 그 칸을 빈 칸으로 둔다(옛 상태를 새것으로 되돌리지 않는다).
            SW_LOG_WARNING_ID( "Autosave.WriteFailed", "Autosave (%#) to %# failed (local store result %#) - the previous slots are kept", getEnumName( info._trigger ), completion._slot.c_str(),
                               static_cast<int32>( completion._result ) );
            _listSlot[static_cast<size_t>( info._slot )] = AutosaveSlotInfo{};
            _infoRequest                                 = _pStore->submitErase( _settings.makeInfoSlotName( info._slot ), GameLocalStore::CompletionFunction::create<&AutosaveManager::onInfoWritten>( this ) );
            return;
        }
        _listSlot[static_cast<size_t>( info._slot )] = info;
        _nextSequence                                = MathUtil::max( _nextSequence, info._sequence + 1 );
        ++_saveCount;
        _lastSaveTime  = _playTime;
        _intervalTimer = 0.0f;
        SW_LOG_INFO( "Autosaved (%#%#%#) to %#", getEnumName( info._trigger ), info._label.empty() ? "" : " ", info._label.c_str(), info._slotName.c_str() );
        // 플레이어에게 알린다(UI 가 없는 서버 · 테스트는 건너뛴다) — 같은 알림이 떠 있으면 센다.
        if ( game::findSubsystem<UISystem>() != nullptr )
        {
            UINotificationDesc notice{};
            notice._text            = SW_LOCTEXT( "Notification", "GameSaved", "Game saved" );
            notice._durationSeconds = 3.0f;
            game::getSubsystem<UINotificationService>().post( notice );
        }
    }

    void AutosaveManager::onInfoWritten( const LocalStoreCompletion& completion )
    {
        // 기록이 없는(또는 옛) 칸은 다음 실행의 목록에서 빈 칸이나 옛것으로 남을 뿐 새 저장을 가리지 않는다.
        const bool bErasedMissing = completion._operation == LocalStoreOperation::Erase && completion._result == LocalStoreResult::NotFound;
        if ( completion._result != LocalStoreResult::Ok && bErasedMissing == false )
            SW_LOG_WARNING( "Autosave slot info %# could not be updated (local store result %#)", completion._slot.c_str(), static_cast<int32>( completion._result ) );
    }

    void AutosaveManager::scanSlots()
    {
        _listSlot.assign( static_cast<size_t>( _settings._slotCount ), AutosaveSlotInfo{} );
        _nextSequence = 1;
        _listScanRequest.clear();
        _scanPendingCount = _settings._slotCount;
        for ( int32 slot = 0; slot < _settings._slotCount; ++slot )
        {
            _listScanRequest.push_back( _pStore->submitRead( _settings.makeInfoSlotName( slot ), GameLocalStore::CompletionFunction::create<&AutosaveManager::onInfoRead>( this ) ) );
        }
    }

    void AutosaveManager::onInfoRead( const LocalStoreCompletion& completion )
    {
        --_scanPendingCount;
        if ( _scanPendingCount == 0 )
            _listScanRequest.clear();
        if ( completion._result == LocalStoreResult::NotFound )
            return; // 아직 쓴 적 없는 칸
        int32 slot = 0;
        while ( slot < _settings._slotCount && _settings.makeInfoSlotName( slot ) != completion._slot )
        {
            ++slot;
        }
        AutosaveSlotInfo info{};
        const bool       bRead = completion._result == LocalStoreResult::Ok && completion._formatVersion == AutosaveInternal::kInfoVersion && slot < _settings._slotCount &&
                           readSlotInfo( completion._bytes, info );
        if ( bRead == false )
        {
            SW_LOG_WARNING( "Autosave slot info %# could not be read (local store result %#) - that slot is treated as empty", completion._slot.c_str(),
                            static_cast<int32>( completion._result ) );
            return;
        }
        info._slotName                         = _settings.makeSlotName( slot );
        info._slot                             = slot;
        _listSlot[static_cast<size_t>( slot )] = info;
        _nextSequence                          = MathUtil::max( _nextSequence, info._sequence + 1 );
    }

    bool AutosaveManager::writeSlotInfo( const AutosaveSlotInfo& info, vector<uint8>& outBytes )
    {
        Archive archive;
        StateArchiveUtil::writeHeader( archive, AutosaveInternal::kInfoTag, AutosaveInternal::kInfoVersion );
        archive << info._sequence;
        archive << info._playTime;
        archive << static_cast<uint8>( info._trigger );
        StateArchiveUtil::writeName( archive, info._label );
        if ( archive.isError() )
            return false;
        outBytes.assign( archive.getData(), archive.getData() + archive.getSize() );
        return true;
    }

    bool AutosaveManager::readSlotInfo( const vector<uint8>& bytes, AutosaveSlotInfo& outInfo )
    {
        Archive archive( bytes.data(), bytes.size() );
        if ( StateArchiveUtil::readHeader( archive, AutosaveInternal::kInfoTag, AutosaveInternal::kInfoVersion ) == false )
            return false;
        uint8 trigger = 0;
        archive >> outInfo._sequence;
        archive >> outInfo._playTime;
        archive >> trigger;
        const bool bInvalidSlotInfo = StateArchiveUtil::readName( archive, outInfo._label ) == false || archive.isError() || trigger > static_cast<uint8>( AutosaveTrigger::Manual ) ||
                                      outInfo._sequence == 0;
        if ( bInvalidSlotInfo )
            return false;
        outInfo._trigger = static_cast<AutosaveTrigger>( trigger );
        return true;
    }
} // namespace sw
