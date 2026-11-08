#include "pch.h"

#include "Editor/Common/Config/EditorComponentIconTable.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    bool EditorComponentIconTable::loadFromAsset( const AssetID& assetID )
    {
        *this = EditorComponentIconTable{};
        if ( XMLSerializer::loadFromAsset( assetID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "EditorComponentIcon.TableCannotRead", "Component icon table could not be read strictly: %#", assetID.view() );
            return false;
        }
        bool bValid = true;
        for ( const EditorComponentCategoryIcon& row : _listCategory )
        {
            if ( findCategory( row._category ) != &row )
            {
                SW_LOG_ERROR_ID( "EditorComponentIcon.DuplicateCategory", "Component icon table lists category '%#' twice", row._category.c_str() );
                bValid = false;
            }
        }
        if ( bValid == false )
            *this = EditorComponentIconTable{};
        return bValid;
    }

    const EditorComponentCategoryIcon* EditorComponentIconTable::findCategory( string_view category ) const
    {
        return VectorUtil::findBy<&EditorComponentCategoryIcon::_category>( _listCategory, category );
    }
} // namespace sw::editor
