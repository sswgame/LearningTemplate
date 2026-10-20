#include "pch.h"

#include "Engine/Scene/SceneStateHash.h"

#include "Core/Common/BitUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <algorithm>
#include <charconv>
#include <cstring>

namespace sw
{
    namespace
    {
        struct SceneStateHashInternal
        {
            static void foldFloat( uint64& inoutHash, float32 value )
            {
                const uint32 bits = BitUtil::floatToBits( value );
                inoutHash         = HashUtil::appendBytes64( inoutHash, &bits, sizeof( bits ) );
            }

            struct Entry
            {
                string _name;
                uint8  _bActive{ 0 };
                float3 _position{};
            };
        };
    } // namespace

    uint64 SceneStateHash::compute( const GameObjectManager& manager )
    {
        vector<SceneStateHashInternal::Entry> listEntry;
        manager.forEachGameObject( [&listEntry]( GameObject* pObject )
        {
            if ( pObject->getName().empty() )
                return;
            SceneStateHashInternal::Entry& entry = listEntry.emplace_back();
            entry._name                          = pObject->getName().c_str();
            entry._bActive                       = pObject->isActive() ? 1 : 0;
            const SceneComponent* pRoot          = pObject->getComponent<SceneComponent>();
            if ( pRoot != nullptr )
                entry._position = pRoot->getWorldPosition();
        } );
        // 이름이 같은 오브젝트끼리는 값으로 정렬해 프로세스마다 순회 순서가 달라도 같은 결과가 되게 한다.
        std::sort( listEntry.begin(), listEntry.end(), []( const SceneStateHashInternal::Entry& left, const SceneStateHashInternal::Entry& right )
        {
            if ( left._name != right._name )
                return left._name < right._name;
            if ( left._bActive != right._bActive )
                return left._bActive < right._bActive;
            if ( left._position._x != right._position._x )
                return left._position._x < right._position._x;
            if ( left._position._y != right._position._y )
                return left._position._y < right._position._y;
            return left._position._z < right._position._z;
        } );
        uint64 hash = HashUtil::kFnvOffset64;
        for ( const SceneStateHashInternal::Entry& entry : listEntry )
        {
            hash = HashUtil::appendBytes64( hash, entry._name.data(), entry._name.size() );
            hash = HashUtil::appendBytes64( hash, &entry._bActive, sizeof( entry._bActive ) );
            SceneStateHashInternal::foldFloat( hash, entry._position._x );
            SceneStateHashInternal::foldFloat( hash, entry._position._y );
            SceneStateHashInternal::foldFloat( hash, entry._position._z );
        }
        return hash;
    }

    StateHashHistory& StateHashHistory::get()
    {
        static StateHashHistory s_history;
        return s_history;
    }

    void StateHashHistory::setCapacity( uint32 capacity )
    {
        _listSample.clear();
        _head     = 0;
        _capacity = capacity;
        if ( capacity > 0 )
            _listSample.reserve( capacity );
    }

    void StateHashHistory::record( uint64 tick, uint64 hash )
    {
        if ( _capacity == 0 )
            return;
        const StateHashSample sample{ tick, hash };
        if ( _listSample.size() < _capacity )
        {
            _listSample.push_back( sample );
            return;
        }
        _listSample[_head] = sample;
        _head              = ( _head + 1 ) % _capacity;
    }

    bool StateHashHistory::tryGetLatest( StateHashSample& outSample ) const
    {
        if ( _listSample.empty() )
            return false;
        const size_t latest = _listSample.size() < _capacity ? _listSample.size() - 1 : ( _head + _capacity - 1 ) % _capacity;
        outSample           = _listSample[latest];
        return true;
    }

    void StateHashHistory::collect( uint64 fromTick, uint32 count, vector<StateHashSample>& outListSample ) const
    {
        outListSample.clear();
        const size_t size  = _listSample.size();
        const size_t start = size < _capacity ? 0 : _head;
        for ( size_t offset = 0; offset < size && outListSample.size() < count; ++offset )
        {
            const StateHashSample& sample = _listSample[( start + offset ) % size];
            if ( sample._tick >= fromTick )
                outListSample.push_back( sample );
        }
    }

    void StateHashHistory::writeReport( const vector<StateHashSample>& listSample, string& outText )
    {
        constexpr const utf8* kHexDigit = "0123456789abcdef";
        for ( const StateHashSample& sample : listSample )
        {
            utf8                       arrBuffer[constant::kMaxBuffer32];
            const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), sample._tick );
            outText.append( arrBuffer, result.ptr );
            outText += ' ';
            for ( int32 nibble = 15; nibble >= 0; --nibble )
            {
                outText += kHexDigit[( sample._hash >> ( nibble * 4 ) ) & 0xF];
            }
            outText += '\n';
        }
    }

    bool StateHashHistory::parseReport( string_view text, vector<StateHashSample>& outListSample )
    {
        outListSample.clear();
        size_t lineStart = 0;
        while ( lineStart < text.size() )
        {
            size_t lineEnd = text.find( '\n', lineStart );
            if ( lineEnd == string_view::npos )
                lineEnd = text.size();
            const string_view line = StringUtil::trim( text.substr( lineStart, lineEnd - lineStart ) );
            lineStart              = lineEnd + 1;
            if ( line.empty() )
                continue;
            const size_t    space = line.find( ' ' );
            StateHashSample sample;
            const bool      bHasTick = space != string_view::npos && StringUtil::parseUint64( line.substr( 0, space ), sample._tick );
            const bool      bHasHash = bHasTick && StringUtil::parseUint64( line.substr( space + 1 ), sample._hash, 16 );
            // 읽지 못한 줄을 건너뛰면 그 틱이 비교에서 조용히 빠진다 — 다른 글(오류 답 · 다른 파일)을 이력으로 받지 않는다.
            if ( bHasHash == false )
            {
                outListSample.clear();
                return false;
            }
            outListSample.push_back( sample );
        }
        return true;
    }

    bool StateHashHistory::findFirstDivergence( const vector<StateHashSample>& listLeft, const vector<StateHashSample>& listRight, uint64& outTick )
    {
        bool bFound = false;
        for ( const StateHashSample& left : listLeft )
        {
            for ( const StateHashSample& right : listRight )
            {
                const bool bDiffers = left._tick == right._tick && left._hash != right._hash;
                const bool bEarlier = bFound == false || left._tick < outTick;
                if ( bDiffers && bEarlier )
                {
                    outTick = left._tick;
                    bFound  = true;
                }
            }
        }
        return bFound;
    }
} // namespace sw
