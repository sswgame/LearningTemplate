#include "pch.h"

#include "Engine/FileSystem/DomainTable.h"

#include <algorithm>

namespace sw
{
    bool DomainTable::add( string_view name, int32 priority, bool bCooked )
    {
        // 이름도 리소스 경로와 같은 정규화를 거친다 - 같은 이름이 글자만 달라 둘 들어가지 않게.
        const std::optional<ResourcePath> normalized = ResourcePath::tryMake( {}, name );
        if ( normalized.has_value() == false || find( normalized->relative() ) != nullptr )
            return false;

        _listEntry.push_back( DomainEntry{ normalized->relative(), priority, bCooked } );
        return true;
    }

    const DomainEntry* DomainTable::find( string_view name ) const
    {
        for ( const DomainEntry& entry : _listEntry )
        {
            if ( entry.name == name )
                return &entry;
        }
        return nullptr;
    }

    vector<const DomainEntry*> DomainTable::listByPriority() const
    {
        vector<const DomainEntry*> result;
        for ( const DomainEntry& entry : _listEntry )
            result.push_back( &entry );
        std::stable_sort( result.begin(), result.end(), []( const DomainEntry* left, const DomainEntry* right )
        { return left->priority < right->priority; } );
        return result;
    }

    std::optional<ResourcePath> DomainTable::split( string_view reference ) const
    {
        // 정규화한 뒤 가른다 - 대소문자 · 구분자가 달라도 같은 도메인으로 간다.
        const std::optional<ResourcePath> whole = ResourcePath::tryMake( {}, reference );
        if ( whole.has_value() == false )
            return std::nullopt;

        const string&      text  = whole->relative();
        const DomainEntry* pBest = nullptr;
        for ( const DomainEntry& entry : _listEntry )
        {
            const bool bPrefix = text.size() > entry.name.size() && text[entry.name.size()] == '/' && text.compare( 0, entry.name.size(), entry.name ) == 0;
            if ( bPrefix && ( pBest == nullptr || entry.name.size() > pBest->name.size() ) )
                pBest = &entry;
        }

        if ( pBest == nullptr )
            return whole;
        return ResourcePath::tryMake( pBest->name, string_view{ text }.substr( pBest->name.size() + 1 ) );
    }
} // namespace sw
