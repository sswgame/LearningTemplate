#include "pch.h"

#include "Engine/Object/Component/ComponentAssetPathUtil.h"

#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    string_view ComponentAssetPathUtil::readAssetPath( const PropertyInfo& prop, const Component* pComponent )
    {
        static const hashed_string s_string( "string" );
        static const hashed_string s_hashedString( "hashed_string" );
        if ( prop._metadata->_bAssetPath == SW_FALSE && prop._metadata->_assetType.empty() )
            return {};
        if ( prop._typeName == s_string )
        {
            const string* pValue = prop.getValuePtr<string>( pComponent );
            return pValue != nullptr ? string_view( *pValue ) : string_view{};
        }
        if ( prop._typeName == s_hashedString )
        {
            const hashed_string* pValue = prop.getValuePtr<hashed_string>( pComponent );
            return pValue != nullptr ? string_view( pValue->c_str() ) : string_view{};
        }
        return {};
    }
} // namespace sw
