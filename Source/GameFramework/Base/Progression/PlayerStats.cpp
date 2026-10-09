#include "pch.h"

#include "GameFramework/Base/Progression/PlayerStats.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "PlayerStats" );

    namespace
    {
        struct PlayerStatsInternal
        {
            static constexpr uint32 kStateTag     = FourCcUtil::make( "STAT" );
            static constexpr uint32 kStateVersion = 1;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool parseStatKind( string_view text, StatKind& outKind )
    {
        struct KindName
        {
            const utf8* _pName;
            StatKind    _kind;
        };
        static constexpr KindName kArrKind[] = {
            {"Counter", StatKind::Counter},
            {    "Max",     StatKind::Max},
            {    "Min",     StatKind::Min},
            {   "Time",    StatKind::Time},
        };
        for ( const KindName& entry : kArrKind )
        {
            if ( StringUtil::equals( text, entry._pName, true ) )
            {
                outKind = entry._kind;
                return true;
            }
        }
        return false;
    }

    const utf8* toString( StatKind kind )
    {
        switch ( kind )
        {
            case StatKind::Counter:
                return "Counter";
            case StatKind::Max:
                return "Max";
            case StatKind::Min:
                return "Min";
            case StatKind::Time:
                return "Time";
        }
        return "Unknown";
    }

    // ------------------------------------------------------------------------------
    // 카탈로그
    // ------------------------------------------------------------------------------
    StatCatalog::StatCatalog()
        : _listStat{}
        , _mapIndex{}
    {
    }

    void StatCatalog::addStat( const StatDef& def )
    {
        if ( def._id.empty() )
            return;
        const auto mapIter = _mapIndex.find( def._id );
        if ( mapIter != _mapIndex.end() )
        {
            _listStat[mapIter->second] = def;
            return;
        }
        _mapIndex[def._id] = static_cast<uint32>( _listStat.size() );
        _listStat.push_back( def );
    }

    const StatDef* StatCatalog::findStat( const hashed_string& id ) const
    {
        const auto mapIter = _mapIndex.find( id );
        return mapIter != _mapIndex.end() ? &_listStat[mapIter->second] : nullptr;
    }

    uint32 StatCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Stat" ); node; node = node.findNextSibling( "Stat" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            StatDef           def;
            const string_view kindText = node.getAttributeText( "kind" );
            if ( kindText.empty() == false && parseStatKind( kindText, def._kind ) == false )
            {
                SW_LOG_ERROR( "%#: stat '%#' has an unknown kind '%#' (Counter, Max, Min, Time) - skipped", sourceName, pId, kindText );
                continue;
            }
            const utf8* pName = node.findAttribute( "name" );
            def._id           = hashed_string( pId );
            def._name         = pName != nullptr ? pName : pId;
            def._maxValue     = MathUtil::max( 0.0, static_cast<float64>( node.getAttributeFloat( "max", 0.0f ) ) );
            addStat( def );
            ++loadedCount;
        }
        return loadedCount;
    }

    // ------------------------------------------------------------------------------
    // 값
    // ------------------------------------------------------------------------------
    PlayerStats::PlayerStats()
        : _pCatalog{ nullptr }
        , _mapValue{}
        , _listListener{}
        , _nextListenerHandle{ 1 }
    {
    }

    void PlayerStats::initialize( const StatCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _mapValue.clear();
    }

    bool PlayerStats::increment( const hashed_string& id, int64 amount )
    {
        const StatDef* pDef = findDefForUse( id, StatKind::Counter, StatKind::Counter );
        if ( pDef == nullptr || amount <= 0 )
            return false;
        return applyValue( *pDef, getValue( id ) + static_cast<float64>( amount ) );
    }

    bool PlayerStats::submit( const hashed_string& id, float64 value )
    {
        const StatDef* pDef = findDefForUse( id, StatKind::Max, StatKind::Min );
        if ( pDef == nullptr )
            return false;
        if ( hasValue( id ) )
        {
            const float64 current = getValue( id );
            const bool    bBetter = pDef->_kind == StatKind::Max ? value > current : value < current;
            if ( bBetter == false )
                return false;
        }
        return applyValue( *pDef, value );
    }

    bool PlayerStats::addTime( const hashed_string& id, float64 seconds )
    {
        const StatDef* pDef = findDefForUse( id, StatKind::Time, StatKind::Time );
        if ( pDef == nullptr || seconds <= 0.0 )
            return false;
        return applyValue( *pDef, getValue( id ) + seconds );
    }

    float64 PlayerStats::getValue( const hashed_string& id ) const
    {
        const auto mapIter = _mapValue.find( id );
        return mapIter != _mapValue.end() ? mapIter->second : 0.0;
    }

    bool PlayerStats::hasValue( const hashed_string& id ) const
    {
        return _mapValue.find( id ) != _mapValue.end();
    }

    void PlayerStats::reset()
    {
        _mapValue.clear();
    }

    uint32 PlayerStats::registerChangeListener( const ChangeDelegate& listener )
    {
        Listener entry{};
        entry._delegate = listener;
        entry._handle   = _nextListenerHandle++;
        _listListener.push_back( entry );
        return entry._handle;
    }

    void PlayerStats::unregisterChangeListener( uint32 handle )
    {
        for ( size_t index = 0; index < _listListener.size(); ++index )
        {
            if ( _listListener[index]._handle == handle )
            {
                _listListener.erase( _listListener.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    const StatDef* PlayerStats::findDefForUse( const hashed_string& id, StatKind expectedKind, StatKind alternateKind ) const
    {
        const StatDef* pDef = _pCatalog != nullptr ? _pCatalog->findStat( id ) : nullptr;
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "Unknown stat '%#' - ignored", id.c_str() );
            return nullptr;
        }
        if ( pDef->_kind != expectedKind && pDef->_kind != alternateKind )
        {
            SW_LOG_WARNING( "Stat '%#' is a %# stat - %# is not how it takes values", id.c_str(), toString( pDef->_kind ), toString( expectedKind ) );
            return nullptr;
        }
        return pDef;
    }

    bool PlayerStats::applyValue( const StatDef& def, float64 newValue )
    {
        if ( def._maxValue > 0.0 )
            newValue = MathUtil::min( newValue, def._maxValue );
        const bool    bHad     = hasValue( def._id );
        const float64 oldValue = getValue( def._id );
        if ( bHad && oldValue == newValue )
            return false;
        _mapValue[def._id] = newValue;
        StatChange change{};
        change._id       = def._id;
        change._oldValue = oldValue;
        change._newValue = newValue;
        change._kind     = def._kind;
        // 듣는 쪽이 등록 · 해제할 수 있으므로 사본을 돈다.
        const vector<Listener> listListener = _listListener;
        for ( const Listener& listener : listListener )
        {
            if ( listener._delegate.isBound() )
                listener._delegate( change );
        }
        return true;
    }

    void PlayerStats::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, PlayerStatsInternal::kStateTag, PlayerStatsInternal::kStateVersion );
        // 카탈로그 순서로 적는다(같은 값이면 같은 바이트) — 카탈로그에 없는 값은 이미 들 수 없다.
        uint32 count = 0;
        if ( _pCatalog != nullptr )
        {
            for ( const StatDef& def : _pCatalog->getStats() )
            {
                count += hasValue( def._id ) ? 1u : 0u;
            }
        }
        outArchive << count;
        if ( _pCatalog == nullptr )
            return;
        for ( const StatDef& def : _pCatalog->getStats() )
        {
            if ( hasValue( def._id ) == false )
                continue;
            StateArchiveUtil::writeName( outArchive, def._id );
            outArchive << getValue( def._id );
        }
    }

    bool PlayerStats::readState( Archive& archive )
    {
        uint32 count = 0;
        if ( StateArchiveUtil::readHeader( archive, PlayerStatsInternal::kStateTag, PlayerStatsInternal::kStateVersion ) == false ||
             StateArchiveUtil::readCount( archive, sizeof( uint32 ) + sizeof( float64 ), count ) == false )
            return false;
        unordered_map<hashed_string, float64> mapValue;
        for ( uint32 index = 0; index < count; ++index )
        {
            hashed_string id;
            float64       value = 0.0;
            if ( StateArchiveUtil::readName( archive, id ) == false )
                return false;
            archive >> value;
            if ( archive.isError() )
                return false;
            if ( _pCatalog == nullptr || _pCatalog->findStat( id ) == nullptr )
            {
                SW_LOG_WARNING( "Saved stat '%#' has no definition any more - dropped", id.c_str() );
                continue;
            }
            mapValue[id] = value;
        }
        _mapValue = std::move( mapValue );
        return true;
    }

    bool PlayerStats::saveToFile( string_view path ) const
    {
        Archive archive;
        writeState( archive );
        return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeFile( path, archive.getData(), archive.getSize() );
    }

    bool PlayerStats::loadFromFile( string_view path )
    {
        vector<uint8> bytes;
        if ( FileUtil::exists( path ) == false || FileUtil::readFile( path, bytes ) == false )
            return false;
        Archive archive( bytes.data(), bytes.size() );
        return readState( archive );
    }
} // namespace sw
