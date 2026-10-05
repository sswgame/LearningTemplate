#include "pch.h"

#include "GameFramework/Kits/Rpg/TurnBattle/SaveGame.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Engine/Utility/KeyValueFile.h"

#include "GameFramework/Data/GameSettings.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Kits/Rpg/TurnBattle/SpeciesData.h"

namespace sw
{
    namespace
    {
        struct SaveGameInternal
        {
            /** @brief "party{i}.{field}" 문자열을 스택에 조립하여 반환합니다. */
            static fixed_string<constant::kMaxBuffer64> partyKey( int32 index, const utf8* pField )
            {
                fixed_string<constant::kMaxBuffer64> fs;
                fs.append( "party" );
                StringBuilder<constant::kMaxBuffer16> numSb;
                numSb.append( index );
                fs.append( numSb.c_str() );
                fs.append( "." );
                fs.append( pField );
                return fs;
            }

            /**
             * @brief 세이브에서 읽은 수를 그대로 믿지 않기 위한 하드 상한입니다.
             * @details 세이브 파일은 손으로 고칠 수 있고 망가질 수도 있습니다. 파티 수와 함께 **`ppCount` 도
             *          자릅니다** — 안 자르면 `party0.ppCount=2000000000` 한 줄이 8 GB 짜리 `assign` 이 됩니다.
             *          데이터가 정하는 `maxPartySize` 도 데이터가 망가지면 같은 문제이므로 함께 자릅니다.
             */
            static constexpr int32 kHardPartyCap = 64;
            /** @brief 한 파티원이 가질 수 있는 최대 기술 슬롯 수입니다. 기본 세이브는 둘을 씁니다. */
            static constexpr int32 kHardPpCap = 16;

            static size_t partyCap()
            {
                const GameSettings* pData    = game::getService<GameSettings>();
                const int32         rawValue = pData != nullptr ? pData->getCustomPropertyInt( "maxPartySize", 6 ) : 6;
                return static_cast<size_t>( MathUtil::clamp( rawValue > 0 ? rawValue : 6, 1, kHardPartyCap ) );
            }

            static size_t ppCap()
            {
                return static_cast<size_t>( kHardPpCap );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "TurnBattleSaveGame" );

    void TurnBattleSaveGame::clearParty()
    {
        _listParty.clear();
    }

    void TurnBattleSaveGame::setPartyFrom( const vector<PartyMember>& listParty )
    {
        _listParty.clear();
        const size_t n = listParty.size() < SaveGameInternal::partyCap() ? listParty.size() : SaveGameInternal::partyCap();
        _listParty.assign( listParty.begin(), listParty.begin() + static_cast<std::ptrdiff_t>( n ) );
    }

    void TurnBattleSaveGame::ensureStarterParty()
    {
        if ( _listParty.empty() == false )
            return;

        const GameSettings* pData = game::getService<GameSettings>();
        if ( pData == nullptr )
            return;

        const string_view starterSpecies = pData->getCustomProperty( "starterSpecies", "critter_a" );
        const int32       starterLevel   = pData->getCustomPropertyInt( "starterLevel", 5 );

        // 카탈로그에 그 종족이 있으면 카탈로그가 만든다(기본 HP · 기술 슬롯의 PP). 없을 때만 손으로 채운 최소 멤버다 — 손으로 채운 값은
        // 종족의 `baseHp` · 기술 수를 모르므로 카탈로그가 있을 때 쓰면 안 된다.
        const SpeciesCatalog* pCatalog  = game::getService<SpeciesCatalog>();
        const SpeciesDef*     pDef      = pCatalog != nullptr ? pCatalog->findSpecies( string( starterSpecies ).c_str() ) : nullptr;
        const int32           safeLevel = MathUtil::clamp( starterLevel, 1, SpeciesCatalog::kMaxLevel );
        PartyMember           m{};
        if ( pDef != nullptr && string_view( pDef->_id ) == starterSpecies )
            m = pCatalog->makeStarter( pDef->_id.c_str(), safeLevel );
        else
        {
            m._speciesId = starterSpecies;
            m._nickname  = starterSpecies;
            m._level     = safeLevel;
            m._hp        = 20 + safeLevel * 2;
            m._hpMax     = m._hp;
            m._listPp    = { 35, 30 };
            m._exp       = 0;
            m._expNext   = SpeciesCatalog::computeExpToNextLevel( safeLevel );
        }

        _listParty.push_back( std::move( m ) );
        SW_LOG_INFO( "Added starter party %# (lv%#)", _listParty[0]._speciesId, _listParty[0]._level );
    }

    void TurnBattleSaveGame::ensureStartMap()
    {
        if ( _mapPath.empty() == false )
            return;
        const GameSettings* pData = game::getService<GameSettings>();
        if ( pData != nullptr )
            _mapPath = pData->_startMap;
    }

    int32 TurnBattleSaveGame::getFlag( string_view key, int32 defaultValue ) const
    {
        const auto it = _mapFlag.find( string( key ) );
        if ( it != _mapFlag.end() )
            return it->second;
        return defaultValue;
    }

    void TurnBattleSaveGame::setFlag( string_view key, int32 value )
    {
        _mapFlag[string( key )] = value;
    }

    bool TurnBattleSaveGame::saveToFile( string_view path ) const
    {
        if ( StringUtil::endsWith( path, ".sav", false ) || StringUtil::endsWith( path, ".bin", false ) )
            return SaveGameSerializer::saveGameToSlot( *this, path );

        StringBuilder<constant::kMaxBuffer2048> sb;
        sb.append( "map=" ).append( _mapPath.c_str() ).append( "\nx=" ).append( _playerX ).append( "\ny=" ).append( _playerY ).append( "\npartyCount=" ).append( static_cast<int32>( _listParty.size() ) ).append( '\n' );

        for ( size_t partyIndex = 0; partyIndex < _listParty.size() && partyIndex < SaveGameInternal::partyCap(); ++partyIndex )
        {
            const PartyMember& m = _listParty[partyIndex];
            sb.append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".speciesId=" ).append( m._speciesId.c_str() ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".nickname=" ).append( m._nickname.c_str() ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".level=" ).append( m._level ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".hp=" ).append( m._hp ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".hpMax=" ).append( m._hpMax ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".exp=" ).append( m._exp ).append( '\n' );

            // 슬롯 수가 데이터에 달렸으므로 개수를 함께 적는다 — 읽는 쪽은 ppCount 가 말한 칸만 읽는다.
            sb.append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".ppCount=" ).append( static_cast<int32>( m._listPp.size() ) ).append( '\n' );
            for ( size_t slot = 0; slot < m._listPp.size(); ++slot )
            {
                sb.append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".pp" ).append( static_cast<int32>( slot ) ).append( '=' ).append( m._listPp[slot] ).append( '\n' );
            }
        }

        for ( const auto& [key, val] : _mapFlag )
        {
            sb.append( "flag." ).append( key.c_str() ).append( '=' ).append( val ).append( '\n' );
        }

        // `writeTextFile` 이 임시 파일 → 결과 확인 → 바꿔 끼우기를 한다. 주의: 손으로 임시 파일에 쓰고 원본에 복사하면, 쓰기 실패를
        // 놓쳤을 때 잘린 파일이 멀쩡한 세이브를 덮고 복사 자체도 원자적이지 않다.
        if ( FileUtil::writeTextFile( path, sb.view() ) == false )
        {
            SW_LOG_ERROR( "Failed to save %# — the previous save was left untouched", path );
            return false;
        }

        SW_LOG_INFO( "Saved %# (party=%# flags=%#)", path, _listParty.size(), _mapFlag.size() );
        return true;
    }

    bool TurnBattleSaveGame::loadFromFile( string_view path )
    {
        vector<uint8> headBytes;
        if ( FileUtil::readFile( path, headBytes, 0, 4 ) && headBytes.size() >= 4 )
        {
            uint32 magic = 0;
            Memory::copy( &magic, headBytes.data(), sizeof( magic ) );
            if ( magic == SaveGameSerializer::kSaveBinMagic )
            {
                if ( SaveGameSerializer::loadGameFromSlot( *this, path ) == false )
                    return false;
                ensureStartMap();
                return true;
            }
        }

        KeyValueMap map;
        if ( KeyValueFile::loadFile( path, map ) == false )
            return false;

        const utf8* pMapPath = KeyValueFile::get( map, "map", nullptr );
        if ( StringUtil::isNullOrEmpty( pMapPath ) == false )
            _mapPath = pMapPath;
        _playerX = KeyValueFile::getInt( map, "x", _playerX );
        _playerY = KeyValueFile::getInt( map, "y", _playerY );

        _listParty.clear();
        int32 count = KeyValueFile::getInt( map, "partyCount", 0 );
        if ( count < 0 )
            count = 0;
        if ( count > static_cast<int32>( SaveGameInternal::partyCap() ) )
            count = static_cast<int32>( SaveGameInternal::partyCap() );

        for ( int32 itemIndex = 0; itemIndex < count; ++itemIndex )
        {
            PartyMember m{};
            const utf8* pSid = KeyValueFile::get( map, SaveGameInternal::partyKey( itemIndex, "speciesId" ).c_str(), nullptr );
            if ( StringUtil::isNullOrEmpty( pSid ) == false )
                m._speciesId = pSid;
            const utf8* pNick = KeyValueFile::get( map, SaveGameInternal::partyKey( itemIndex, "nickname" ).c_str(), nullptr );
            if ( StringUtil::isNullOrEmpty( pNick ) == false )
                m._nickname = pNick;
            // **레벨도 자른다.** 아래 `_expNext = 40 + level * 10` 과 `makeWild` 의
            // `baseHp + level * 2` 가 곱셈이다. 손으로 고친 `level=2000000000` 한 줄이
            // 부호 있는 정수 오버플로(= 미정의 동작)가 된다. 파티 수 · PP 수와 같은 규칙이다.
            m._level = MathUtil::clamp(
                KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "level" ).c_str(), m._level ),
                1, SpeciesCatalog::kMaxLevel );
            m._hp    = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "hp" ).c_str(), m._hp );
            m._hpMax = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "hpMax" ).c_str(), m._hpMax );
            m._exp   = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "exp" ).c_str(), m._exp );

            // 슬롯 수는 ppCount 가 정한다. 없으면 기술 슬롯 없이 읽는다 — `pp0` · `pp1` 칸이 있어도 개수 없이는 읽지 않는다.
            int32 ppCount = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "ppCount" ).c_str(), -1 );
            if ( ppCount < 0 )
            {
                SW_LOG_WARNING( "Save %# party%# has no ppCount - loading it with no move slots", path, itemIndex );
                ppCount = 0;
            }
            // **파일이 말한 수를 그대로 잡지 않는다.** 바로 위 파티 수와 같은 규칙이다.
            const size_t slotCount = MathUtil::min( static_cast<size_t>( ppCount ), SaveGameInternal::ppCap() );
            m._listPp.assign( slotCount, 0 );
            for ( size_t slot = 0; slot < slotCount; ++slot )
            {
                const string key = string( "pp" ) + to_string( static_cast<int32>( slot ) );
                m._listPp[slot]  = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, key.c_str() ).c_str(), 0 );
            }

            if ( m._nickname.empty() )
            {
                const SpeciesCatalog* pCatalog = game::getService<SpeciesCatalog>();
                const SpeciesDef*     pDef     = pCatalog != nullptr ? pCatalog->findSpecies( m._speciesId.c_str() ) : nullptr;
                m._nickname                    = pDef != nullptr ? pDef->_name : m._speciesId;
            }
            m._expNext = SpeciesCatalog::computeExpToNextLevel( m._level );
            _listParty.push_back( std::move( m ) );
        }

        if ( _listParty.empty() )
            ensureStarterParty();
        ensureStartMap();

        _mapFlag.clear();
        constexpr const utf8* kFlagPrefix = "flag.";
        const size_t          prefixLen   = StringUtil::strlen( kFlagPrefix );
        for ( const auto& [key, val] : map )
        {
            if ( key.size() > prefixLen && key.compare( 0, prefixLen, kFlagPrefix ) == 0 )
            {
                int32 flagVal{ 0 };
                if ( StringUtil::parseInt( val, flagVal ) == false )
                    SW_LOG_WARNING( "Save flag '%#' has an unreadable value '%#' - using 0", key, val );
                _mapFlag[key.substr( prefixLen )] = flagVal;
            }
        }

        SW_LOG_INFO( "Loaded %# @ (%#,%#) party=%# flags=%#",
                     _mapPath, _playerX, _playerY, _listParty.size(), _mapFlag.size() );
        return true;
    }
} // namespace sw
