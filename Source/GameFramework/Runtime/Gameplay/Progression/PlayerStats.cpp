#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/PlayerStats.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct PlayerStatsInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "STAT" );
            static constexpr uint32 kStateVersion = 1;
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 카탈로그
    // ------------------------------------------------------------------------------
    StatCatalog::StatCatalog()
        : _listStat{}
        , _mapIndex{}
        , _listAchievement{}
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

    const AchievementDef* StatCatalog::findAchievement( const hashed_string& id ) const
    {
        for ( const AchievementDef& def : _listAchievement )
        {
            if ( def._id == id )
                return &def;
        }
        return nullptr;
    }

    uint32 StatCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<StatCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&StatCatalog::loadStat>( "Stat" ),
            Element::of<&StatCatalog::loadAchievement>( "Achievement" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool StatCatalog::loadStat( const XMLNode& node, string_view sourceName )
    {

        // 칸 · 종류 이름 · 상한 >= 0 은 StatDef 의 PROPERTY 가 본다. 틀린 통계는 넣지 않는다(자르지 않는다).
        StatDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false )
            return false;
        if ( def._name.empty() )
            def._name = def._id.c_str();
        addStat( def );
        return true;
    }

    bool StatCatalog::loadAchievement( const XMLNode& node, string_view sourceName )
    {
        // 칸 · 필수 · 문턱 >= 0 은 AchievementDef 의 PROPERTY 가 본다. 통계가 있는지는 쓰는 쪽(`AchievementTracker::initialize`)이 본다 - 원소 순서에 매이지 않는다.
        AchievementDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false )
            return false;
        if ( def._name.empty() )
            def._name = def._id.c_str();
        for ( AchievementDef& existing : _listAchievement )
        {
            if ( existing._id == def._id )
            {
                existing = def;
                return true;
            }
        }
        _listAchievement.push_back( def );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 값
    // ------------------------------------------------------------------------------
    PlayerStats::PlayerStats()
        : _pCatalog{ nullptr }
        , _mapValue{}
        , _changedMulticast{}
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

    Subscription PlayerStats::registerChangeListener( const ChangeDelegate& listener )
    {
        return _changedMulticast.add( listener );
    }

    const StatDef* PlayerStats::findDefForUse( const hashed_string& id, StatKind expectedKind, StatKind alternateKind ) const
    {
        const StatDef* pDef = _pCatalog != nullptr ? _pCatalog->findStat( id ) : nullptr;
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING_ID( "PlayerStats.UnknownStatIgnored", "Unknown stat '%#' - ignored", id.c_str() );
            return nullptr;
        }
        if ( pDef->_kind != expectedKind && pDef->_kind != alternateKind )
        {
            SW_LOG_WARNING_ID( "PlayerStats.StatStatNotHow", "Stat '%#' is a %# stat - %# is not how it takes values", id.c_str(), getEnumName( pDef->_kind ), getEnumName( expectedKind ) );
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
        // 듣는 쪽이 순회 중에 등록하거나 해제해도 된다(멀티캐스트가 해제를 방송 뒤로 미룬다).
        _changedMulticast.broadcast( change );
        return true;
    }

    void PlayerStats::serializeState( StateIO& io )
    {
        io.header( PlayerStatsInternal::kStateTag, PlayerStatsInternal::kStateVersion );
        // 카탈로그 순서대로 쓴다. 같은 값이면 같은 바이트가 나온다. 카탈로그에 없는 값은 애초에 보관하지 않는다.
        uint32 statCount = 0;
        if ( io.isWriting() && _pCatalog != nullptr )
        {
            for ( const StatDef& def : _pCatalog->getStats() )
            {
                statCount += hasValue( def._id ) ? 1u : 0u;
            }
        }
        io.count( sizeof( uint32 ) + sizeof( float64 ), statCount );
        if ( io.isOk() == false )
            return;
        if ( io.isWriting() )
        {
            if ( _pCatalog == nullptr )
                return;
            for ( const StatDef& def : _pCatalog->getStats() )
            {
                if ( hasValue( def._id ) == false )
                    continue;
                hashed_string id    = def._id;
                float64       value = getValue( id );
                io.value( id );
                io.value( value );
            }
            return;
        }
        unordered_map<hashed_string, float64> mapValue;
        for ( uint32 index = 0; index < statCount; ++index )
        {
            hashed_string id;
            float64       value = 0.0;
            io.value( id );
            io.value( value );
            if ( io.isOk() == false )
                return;
            if ( _pCatalog == nullptr || _pCatalog->findStat( id ) == nullptr )
            {
                SW_LOG_WARNING_ID( "PlayerStats.SavedStatNoDefinition", "Saved stat '%#' has no definition any more - dropped", id.c_str() );
                continue;
            }
            mapValue[id] = value;
        }
        _mapValue = std::move( mapValue );
    }

    bool PlayerStats::saveToFile( const AbsolutePath& absPath ) const
    {
        Archive archive;
        writeState( archive );
        return FileUtil::ensureParentDirectoryExists( absPath.str() ) && FileUtil::writeFile( absPath.str(), archive.getData(), archive.getSize() );
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

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<StatCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( StatCatalog, StatCatalog, SW_GF_API, StatCatalog::kAssetType.withLoader( AssetTypeLoader::of<StatCatalog>( MemoryTag::Game ) ) );
} // namespace sw
