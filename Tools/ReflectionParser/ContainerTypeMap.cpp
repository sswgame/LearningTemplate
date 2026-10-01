#include "pch.h"

#include "ReflectionParser/ContainerTypeMap.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/ReflectionEnumNames.h"

#include "ReflectionParser/ParserUtil.h"

SW_LOG_CALLER( "ContainerTypeMap" );
namespace sw
{
    ContainerTypeMap::ContainerTypeMap()
        : _listRule{}
        , _bLoaded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void ContainerTypeMap::clear()
    {
        _listRule.clear();
        _bLoaded = SW_FALSE;
    }

    void ContainerTypeMap::registerRule( const string& match, ContainerKind kind, const string& type )
    {
        if ( match.empty() || type.empty() )
            return;
        ContainerTypeRule rule;
        rule._match = match;
        rule._kind  = kind;
        rule._type  = type;
        _listRule.push_back( std::move( rule ) );
    }

    void ContainerTypeMap::registerRule( const string& match, const string& kindSpelling,
                                         const string& type )
    {
        ContainerKind kind = ContainerKind::Sequence;
        if ( kindSpelling.empty() == false && tryParseContainerKind( kindSpelling, kind ) == false )
            SW_LOG_WARNING( "Unknown kind '%#', using Sequence", kindSpelling );
        registerRule( match, kind, type );
    }

    const ContainerTypeRule* ContainerTypeMap::match( const string_view clangTypeSpelling ) const
    {
        // 바깥 템플릿 이름이 규칙과 **같아야** 맞는다(`ParserUtil::outerTemplateName` 설명 — 부분 문자열로 맞추던 때는 `TextureAsset` 이
        // set 이었다).
        const string_view templateName = ParserUtil::outerTemplateName( clangTypeSpelling );
        if ( templateName.empty() )
            return nullptr;
        for ( const ContainerTypeRule& rule : _listRule )
        {
            if ( templateName == string_view{ rule._match } )
                return &rule;
        }
        return nullptr;
    }
} // namespace sw
