#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserTypeFilter.h"

namespace sw::editor
{
    void ContentBrowserTypeFilter::sync()
    {
        const uint32 generation = EditorAssetTypeRegistry::getGeneration();
        if ( _bBuilt && generation == _generation )
            return;
        _bBuilt     = true;
        _generation = generation;
        EditorAssetTypeRegistry::collectBrowserFilters( _listFilter );

        // 고른 필터를 종류로 다시 찾는다. 없으면 All(0) 이다.
        _selectedIndex = 0;
        for ( uint32 index = 0; index < static_cast<uint32>( _listFilter.size() ); ++index )
        {
            const EditorAssetBrowserFilter& filter = _listFilter[index];
            if ( filter._bOther == _bSelectedOther && filter._kind == _selectedKind )
            {
                _selectedIndex = index;
                return;
            }
        }
        _selectedKind   = EditorAssetType{};
        _bSelectedOther = false;
    }

    const utf8* ContentBrowserTypeFilter::getSelectedLabel() const
    {
        return _selectedIndex < _listFilter.size() ? _listFilter[_selectedIndex]._pLabel : "All";
    }

    void ContentBrowserTypeFilter::select( uint32 index )
    {
        if ( index >= _listFilter.size() )
            return;
        _selectedIndex  = index;
        _selectedKind   = _listFilter[index]._kind;
        _bSelectedOther = _listFilter[index]._bOther;
    }

    bool ContentBrowserTypeFilter::passes( string_view path ) const
    {
        if ( _bSelectedOther )
            return EditorAssetTypeRegistry::matchesOther( path );
        if ( _selectedKind.isUnknown() )
            return true;
        return EditorAssetTypeRegistry::matches( _selectedKind, path );
    }
} // namespace sw::editor
