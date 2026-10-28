/**
 * @file TagIndex.cpp
 * @brief 태그 → 오브젝트 색인의 쓰기(칸 맞추기 · 빼기)입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/Registry/TagIndex.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"

#include "Engine/Object/Component/TagComponent.h"

namespace sw
{
    namespace
    {
        /** @brief @p tag 와 그 조상(점 앞 접두어마다)의 id 를 @p inoutListKey 에 더합니다. 문자열이 없으면 자기 id 만 더합니다. */
        void appendTagKeys( const TagID& tag, vector<uint64>& inoutListKey )
        {
            inoutListKey.push_back( tag._id );
            const utf8* pString = tag.getString();
            if ( pString == nullptr )
                return;
            for ( size_t length = 0; pString[length] != '\0'; ++length )
            {
                if ( pString[length] == '.' )
                    inoutListKey.push_back( TagID::computeID( pString, length ) );
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    TagIndex::TagIndex()
        : _mutex{}
        , _mapKeyToObject{}
        , _mapComponentToRecord{}
        , _listScratchKey{}
        , _generation{ 0 }
    {
    }

    void TagIndex::update( const TagComponent& tagComponent, GameObject* pOwner, const TagContainer& tags )
    {
        ScopedLock<shared_mutex> lock{ _mutex };
        _listScratchKey.clear();
        for ( const TagID& tag : tags.getTags() )
        {
            appendTagKeys( tag, _listScratchKey );
        }
        VectorUtil::sortUnique( _listScratchKey );

        const auto recordIt   = _mapComponentToRecord.find( &tagComponent );
        const bool bHasRecord = recordIt != _mapComponentToRecord.end();
        const bool bUnchanged = bHasRecord && recordIt->second._pOwner == pOwner && recordIt->second._listKey == _listScratchKey;
        if ( bUnchanged )
            return;
        if ( bHasRecord )
        {
            removeFromBucketsUnlocked( recordIt->second );
            _mapComponentToRecord.erase( recordIt );
        }
        if ( pOwner != nullptr && _listScratchKey.empty() == false )
        {
            for ( const uint64 key : _listScratchKey )
            {
                _mapKeyToObject[key].push_back( pOwner );
            }
            _mapComponentToRecord.emplace( &tagComponent, Record{ pOwner, _listScratchKey } );
        }
        _generation.fetch_add( 1, std::memory_order_relaxed );
    }

    void TagIndex::remove( const TagComponent& tagComponent )
    {
        ScopedLock<shared_mutex> lock{ _mutex };
        const auto               recordIt = _mapComponentToRecord.find( &tagComponent );
        if ( recordIt == _mapComponentToRecord.end() )
            return;
        removeFromBucketsUnlocked( recordIt->second );
        _mapComponentToRecord.erase( recordIt );
        _generation.fetch_add( 1, std::memory_order_relaxed );
    }

    void TagIndex::findObjectsWithTags( const TagContainer& required, vector<GameObject*>& outListObject ) const
    {
        outListObject.clear();
        const vector<TagID>& listRequired = required.getTags();
        if ( listRequired.empty() )
            return;
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const vector<GameObject*>*     pSmallest = nullptr;
        for ( const TagID& tag : listRequired )
        {
            const auto bucketIt = _mapKeyToObject.find( tag._id );
            if ( bucketIt == _mapKeyToObject.end() || bucketIt->second.empty() )
                return;
            if ( pSmallest == nullptr || bucketIt->second.size() < pSmallest->size() )
                pSmallest = &bucketIt->second;
        }
        const bool bCheckOtherTags = listRequired.size() > 1;
        for ( GameObject* pObject : *pSmallest )
        {
            const bool bSkipObject = pObject->isPendingDestroy() || ( bCheckOtherTags && pObject->getTags().hasAllTags( required ) == false );
            if ( bSkipObject == false )
                outListObject.push_back( pObject );
        }
    }

    void TagIndex::removeFromBucketsUnlocked( const Record& record )
    {
        for ( const uint64 key : record._listKey )
        {
            const auto bucketIt = _mapKeyToObject.find( key );
            // 넣을 때 칸마다 한 번 넣었다. 없으면 색인이 깨진 것이다.
            [[maybe_unused]] const bool bRemoved = bucketIt != _mapKeyToObject.end() && VectorUtil::removeSingleSwap( bucketIt->second, record._pOwner );
            SW_ASSERT( bRemoved );
        }
    }
} // namespace sw
