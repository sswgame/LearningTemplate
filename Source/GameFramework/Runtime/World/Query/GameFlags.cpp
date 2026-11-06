#include "pch.h"

#include "GameFramework/Runtime/World/Query/GameFlags.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Gameplay/ConditionExpr.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Runtime/World/Query/GameConditions.h"

#include <algorithm>
#include <limits>

namespace sw
{
    GameFlags::GameFlags()
        : _mapFlag{}
        , _revision{ 0 }
    {
    }

    void GameFlags::setFlag( const hashed_string& name, int32 value )
    {
        if ( name.empty() )
            return;
        const auto mapIter = _mapFlag.find( name );
        if ( value == 0 )
        {
            if ( mapIter == _mapFlag.end() )
                return;
            _mapFlag.erase( mapIter );
            ++_revision;
            return;
        }
        if ( mapIter != _mapFlag.end() )
        {
            if ( mapIter->second == value )
                return;
            mapIter->second = value;
        }
        else
        {
            _mapFlag[name] = value;
        }
        ++_revision;
    }

    int32 GameFlags::addFlag( const hashed_string& name, int32 delta )
    {
        // int32 넘침은 정의되지 않은 동작이라 범위에서 멈춘다(데이터가 정한 값 · 저장본의 값이 더해진다).
        const int64 sum   = static_cast<int64>( getFlag( name ) ) + delta;
        const int32 value = static_cast<int32>( MathUtil::clamp( sum, static_cast<int64>( std::numeric_limits<int32>::min() ), static_cast<int64>( std::numeric_limits<int32>::max() ) ) );
        setFlag( name, value );
        return value;
    }

    int32 GameFlags::getFlag( const hashed_string& name ) const
    {
        const auto mapIter = _mapFlag.find( name );
        return mapIter != _mapFlag.end() ? mapIter->second : 0;
    }

    bool GameFlags::hasFlag( const hashed_string& name ) const
    {
        return _mapFlag.find( name ) != _mapFlag.end();
    }

    bool GameFlags::clearFlag( const hashed_string& name )
    {
        const bool bHad = hasFlag( name );
        setFlag( name, 0 );
        return bHad;
    }

    void GameFlags::clear()
    {
        if ( _mapFlag.empty() )
            return;
        _mapFlag.clear();
        ++_revision;
    }

    bool GameFlags::evaluate( const ConditionExpr& expression ) const
    {
        GameplayContext context;
        context.set<GameFlagsSlot>( this );
        return expression.evaluate( context );
    }

    void GameFlags::fillEntries( vector<GameFlagEntry>& outListEntry ) const
    {
        outListEntry.clear();
        outListEntry.reserve( _mapFlag.size() );
        for ( const auto& [name, value] : _mapFlag )
        {
            outListEntry.push_back( GameFlagEntry{ name, value } );
        }
        const HashedStringLexicalLess lexicalLess;
        VectorUtil::sort( outListEntry, [&]( const GameFlagEntry& lhs, const GameFlagEntry& rhs )
        {
            return lexicalLess( lhs._name, rhs._name );
        } );
    }

    void GameFlags::restoreEntries( const vector<GameFlagEntry>& listEntry )
    {
        _mapFlag.clear();
        for ( const GameFlagEntry& entry : listEntry )
        {
            if ( entry._name.empty() == false && entry._value != 0 )
                _mapFlag[entry._name] = entry._value;
        }
        ++_revision;
    }

    void GameFlags::writeState( Archive& outArchive ) const
    {
        vector<GameFlagEntry> listEntry;
        fillEntries( listEntry );
        outArchive << static_cast<uint32>( listEntry.size() );
        for ( const GameFlagEntry& entry : listEntry )
        {
            StateArchiveUtil::writeName( outArchive, entry._name );
            outArchive << entry._value;
        }
    }

    bool GameFlags::readState( Archive& archive )
    {
        uint32 count = 0;
        // 칸마다 이름 길이(4) + 값(4) 이상
        if ( archive.readCount( 8, count ) == false )
            return false;
        vector<GameFlagEntry> listEntry;
        listEntry.reserve( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            GameFlagEntry entry;
            if ( StateArchiveUtil::readName( archive, entry._name ) == false )
                return false;
            archive >> entry._value;
            if ( archive.isError() )
                return false;
            listEntry.push_back( entry );
        }
        restoreEntries( listEntry );
        return true;
    }
} // namespace sw
