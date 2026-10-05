#include "pch.h"

#include "GameFramework/Base/Framework/Autosave.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "Autosave" );

    namespace
    {
        struct AutosaveInternal
        {
            static constexpr uint32      kInfoTag       = 0x56415341u; ///< 'ASAV'
            static constexpr uint32      kInfoVersion   = 1;
            static constexpr const utf8* kInfoExtension = ".info";
            static constexpr int32       kMaxSlotCount  = 64;

            static string makeInfoPath( string_view slotPath ) { return string( slotPath ) + kInfoExtension; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AutosaveTrigger trigger )
    {
        switch ( trigger )
        {
            case AutosaveTrigger::Interval:
                return "Interval";
            case AutosaveTrigger::AreaChanged:
                return "AreaChanged";
            case AutosaveTrigger::Checkpoint:
                return "Checkpoint";
            case AutosaveTrigger::BeforeBoss:
                return "BeforeBoss";
            case AutosaveTrigger::Quit:
                return "Quit";
            case AutosaveTrigger::Manual:
                return "Manual";
        }
        return "Unknown";
    }

    // ------------------------------------------------------------------------------
    // 정책 수치
    // ------------------------------------------------------------------------------
    bool AutosaveSettings::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &AutosaveSettings::loadRoot, path, "Autosave" );
    }

    bool AutosaveSettings::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &AutosaveSettings::loadRoot, xmlText, sourceName, "Autosave" );
    }

    bool AutosaveSettings::loadRoot( const XmlNode& root, string_view sourceName )
    {
        const utf8* pDirectory = root.findAttribute( "directory" );
        const utf8* pPrefix    = root.findAttribute( "prefix" );
        if ( pDirectory != nullptr )
            _directory = pDirectory;
        if ( pPrefix != nullptr )
            _prefix = pPrefix;
        _interval   = MathUtil::max( 0.0f, root.getAttributeFloat( "interval", _interval ) );
        _minimumGap = MathUtil::max( 0.0f, root.getAttributeFloat( "minimumGap", _minimumGap ) );
        _slotCount  = root.getAttributeInt( "slots", _slotCount );
        if ( _slotCount < 1 || _slotCount > AutosaveInternal::kMaxSlotCount )
        {
            SW_LOG_WARNING( "%#: autosave slots %# is outside 1..%# - using 3", sourceName, _slotCount, AutosaveInternal::kMaxSlotCount );
            _slotCount = 3;
        }
        _bOnAreaChange = root.getAttributeBool( "areaChange", _bOnAreaChange != SW_FALSE ) ? SW_TRUE : SW_FALSE;
        _bOnCheckpoint = root.getAttributeBool( "checkpoint", _bOnCheckpoint != SW_FALSE ) ? SW_TRUE : SW_FALSE;
        _bOnBeforeBoss = root.getAttributeBool( "beforeBoss", _bOnBeforeBoss != SW_FALSE ) ? SW_TRUE : SW_FALSE;
        _bOnQuit       = root.getAttributeBool( "quit", _bOnQuit != SW_FALSE ) ? SW_TRUE : SW_FALSE;
        return true;
    }

    string AutosaveSettings::makeSlotPath( int32 slot ) const
    {
        string path = _directory;
        if ( path.empty() == false && path.back() != '/' && path.back() != '\\' )
            path += '/';
        path += _prefix;
        path += to_string( slot );
        path += ".sav";
        return path;
    }

    // ------------------------------------------------------------------------------
    // 관리자
    // ------------------------------------------------------------------------------
    AutosaveManager::AutosaveManager()
        : _settings{}
        , _onSave{}
        , _onLoad{}
        , _listSlot{}
        , _listBlockReason{}
        , _pendingLabel{}
        , _lastCheckpointId{}
        , _playTime{ 0.0 }
        , _lastSaveTime{ -1.0 }
        , _intervalTimer{ 0.0f }
        , _nextSequence{ 1 }
        , _saveCount{ 0 }
        , _pendingTrigger{ AutosaveTrigger::Manual }
        , _bPending{ SW_FALSE }
        , _bSaving{ SW_FALSE }
    {
    }

    void AutosaveManager::initialize( const AutosaveSettings& settings, const SaveDelegate& onSave, const LoadDelegate& onLoad )
    {
        _settings            = settings;
        _settings._slotCount = MathUtil::clamp( _settings._slotCount, 1, AutosaveInternal::kMaxSlotCount );
        _onSave              = onSave;
        _onLoad              = onLoad;
        _listBlockReason.clear();
        _pendingLabel     = hashed_string{};
        _lastCheckpointId = hashed_string{};
        _playTime         = 0.0;
        _lastSaveTime     = -1.0;
        _intervalTimer    = 0.0f;
        _saveCount        = 0;
        _bPending         = SW_FALSE;
        _bSaving          = SW_FALSE;
        scanSlots();
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
        if ( _bPending == SW_FALSE || isBlocked() || _bSaving == SW_TRUE )
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
            SW_LOG_WARNING( "Autosave (%#) failed - the previous slots are kept", toString( trigger ) );
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

    void AutosaveManager::reachCheckpoint( const hashed_string& checkpointId )
    {
        if ( checkpointId.empty() || checkpointId == _lastCheckpointId )
            return;
        _lastCheckpointId = checkpointId;
        requestSave( AutosaveTrigger::Checkpoint, checkpointId );
    }

    bool AutosaveManager::notifyQuit()
    {
        if ( _settings._bOnQuit == SW_FALSE || _bSaving == SW_TRUE )
            return false;
        _bPending = SW_FALSE;
        return performSave( AutosaveTrigger::Quit, hashed_string{} );
    }

    bool AutosaveManager::saveNow( AutosaveTrigger trigger, const hashed_string& label )
    {
        if ( isBlocked() || _bSaving == SW_TRUE )
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
        const AutosaveSlotInfo* pSlot = findLatestSlot();
        if ( pSlot == nullptr || _onLoad.isBound() == false )
            return false;
        const string path = pSlot->_path; // 불러오기가 칸 목록을 바꿀 수 있다
        return _onLoad( path );
    }

    bool AutosaveManager::restoreCheckpoint()
    {
        const AutosaveSlotInfo* pSlot = findLatestSlot( AutosaveTrigger::Checkpoint );
        if ( pSlot == nullptr || _onLoad.isBound() == false )
            return false;
        const string        path         = pSlot->_path;
        const hashed_string checkpointId = pSlot->_label;
        if ( _onLoad( path ) == false )
            return false;
        _lastCheckpointId = checkpointId; // 되돌린 체크포인트에 다시 서도 저장하지 않는다
        _bPending         = SW_FALSE;
        return true;
    }

    void AutosaveManager::collectSlots( vector<AutosaveSlotInfo>& outListSlot ) const
    {
        outListSlot.clear();
        for ( const AutosaveSlotInfo& slot : _listSlot )
        {
            if ( slot._sequence != 0 )
                outListSlot.push_back( slot );
        }
        std::sort( outListSlot.begin(), outListSlot.end(), []( const AutosaveSlotInfo& lhs, const AutosaveSlotInfo& rhs )
        { return lhs._sequence > rhs._sequence; } );
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
            if ( slot._sequence != 0 && slot._trigger == trigger && ( pLatest == nullptr || slot._sequence > pLatest->_sequence ) )
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
        if ( _onSave.isBound() == false || _listSlot.empty() )
            return false;
        // 가장 새 칸의 다음 칸 — 가장 새 칸은 건드리지 않는다(저장 도중 꺼져도 남는다).
        const AutosaveSlotInfo* pLatest = findLatestSlot();
        const int32             slot    = pLatest != nullptr ? ( pLatest->_slot + 1 ) % _settings._slotCount : 0;
        AutosaveSlotInfo        info{};
        info._path     = _settings.makeSlotPath( slot );
        info._label    = label;
        info._sequence = _nextSequence;
        info._playTime = _playTime;
        info._slot     = slot;
        info._trigger  = trigger;

        _bSaving          = SW_TRUE;
        const bool bSaved = _onSave( info._path );
        _bSaving          = SW_FALSE;
        if ( bSaved == false )
            return false;
        // 저장이 끝난 뒤에 기록을 쓴다 — 기록이 없는(또는 옛) 칸은 목록에서 옛것으로 남을 뿐 새 저장을 가리지 않는다.
        if ( writeSlotInfo( info ) == false )
            SW_LOG_WARNING( "Autosave slot info for %# could not be written", info._path.c_str() );
        _listSlot[static_cast<size_t>( slot )] = info;
        ++_nextSequence;
        ++_saveCount;
        _lastSaveTime  = _playTime;
        _intervalTimer = 0.0f;
        SW_LOG_INFO( "Autosaved (%#%#%#) to %#", toString( trigger ), label.empty() ? "" : " ", label.c_str(), info._path.c_str() );
        return true;
    }

    void AutosaveManager::scanSlots()
    {
        _listSlot.assign( static_cast<size_t>( _settings._slotCount ), AutosaveSlotInfo{} );
        _nextSequence = 1;
        for ( int32 slot = 0; slot < _settings._slotCount; ++slot )
        {
            const string     slotPath = _settings.makeSlotPath( slot );
            AutosaveSlotInfo info{};
            if ( FileUtil::fileExists( slotPath ) == false || readSlotInfo( AutosaveInternal::makeInfoPath( slotPath ), info ) == false )
                continue;
            info._path                             = slotPath;
            info._slot                             = slot;
            _listSlot[static_cast<size_t>( slot )] = info;
            _nextSequence                          = MathUtil::max( _nextSequence, info._sequence + 1 );
        }
    }

    bool AutosaveManager::writeSlotInfo( const AutosaveSlotInfo& info )
    {
        Archive archive;
        StateArchiveUtil::writeHeader( archive, AutosaveInternal::kInfoTag, AutosaveInternal::kInfoVersion );
        archive << info._sequence;
        archive << info._playTime;
        archive << static_cast<uint8>( info._trigger );
        StateArchiveUtil::writeName( archive, info._label );
        return FileUtil::writeFile( AutosaveInternal::makeInfoPath( info._path ), archive.getData(), archive.getSize() );
    }

    bool AutosaveManager::readSlotInfo( string_view infoPath, AutosaveSlotInfo& outInfo )
    {
        vector<uint8> bytes;
        if ( FileUtil::fileExists( infoPath ) == false || FileUtil::readFile( infoPath, bytes ) == false )
            return false;
        Archive archive( bytes.data(), bytes.size() );
        if ( StateArchiveUtil::readHeader( archive, AutosaveInternal::kInfoTag, AutosaveInternal::kInfoVersion ) == false )
            return false;
        uint8 trigger = 0;
        archive >> outInfo._sequence;
        archive >> outInfo._playTime;
        archive >> trigger;
        if ( StateArchiveUtil::readName( archive, outInfo._label ) == false || archive.isError() || trigger > static_cast<uint8>( AutosaveTrigger::Manual ) ||
             outInfo._sequence == 0 )
            return false;
        outInfo._trigger = static_cast<AutosaveTrigger>( trigger );
        return true;
    }
} // namespace sw
