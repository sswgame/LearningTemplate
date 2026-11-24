#include "pch.h"

#include "DevTools/Cook/CookReflectionFingerprint.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    uint64 CookReflectionFingerprint::compute( const TypeRegistry& registry )
    {
        vector<string> listTypeText;
        registry.forEachType( [&listTypeText]( const TypeInfo& type )
        {
            StringBuilder<constant::buffer::kMax1024> textBuilder;
            textBuilder.appendFormat( "%#|layout=%#", type._fullyQualifiedName.view(), type.getSchemaLayoutHash() );
            for ( const PropertyInfo& property : type._listProperty )
            {
                const PropertyMetadata& metadata   = *property._metadata;
                const uint32            mergeFlags = ( metadata._bRequired == SW_TRUE ? 1u : 0u ) | ( metadata._bInheritKey == SW_TRUE ? 2u : 0u ) | ( metadata._bAssetPath == SW_TRUE ? 4u : 0u );
                textBuilder.appendFormat( "|%#:%#:%#:%#", property._name.view(), mergeFlags, metadata._assetType, metadata._defaultValue );
                for ( const hashed_string& alias : property._listAlias )
                {
                    textBuilder.append( '~' ).append( alias.view() );
                }
            }
            listTypeText.push_back( string( textBuilder.view() ) );
        } );
        registry.forEachEnum( [&listTypeText]( const EnumInfo& info )
        {
            vector<string>                           listEntry;
            StringBuilder<constant::buffer::kMax256> entryBuilder;
            for ( const auto& [name, value] : info._mapNameToValue )
            {
                entryBuilder.clear();
                entryBuilder.appendFormat( "%#=%#", name.toString(), value );
                listEntry.push_back( string( entryBuilder.view() ) );
            }
            VectorUtil::sort( listEntry );
            StringBuilder<constant::buffer::kMax1024> textBuilder;
            textBuilder.appendFormat( "enum %#", info._fullyQualifiedName.view() );
            for ( const string& entry : listEntry )
            {
                textBuilder.append( '|' ).append( entry );
            }
            listTypeText.push_back( string( textBuilder.view() ) );
        } );
        VectorUtil::sort( listTypeText );
        string canonical;
        for ( const string& text : listTypeText )
        {
            canonical.append( text ).append( "\n" );
        }
        return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
    }
} // namespace sw
