#include "pch.h"

#include "Programs/ReflectionParser/ContainerTypeMap.h"

#include "Core/Log/Logger.h"

#include "Programs/ReflectionParser/ParserUtil.h"

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

    bool ContainerTypeMap::registerRule( const ContainerTypeRule& rule )
    {
        if ( rule._match.empty() || rule._type.empty() )
            return false;
        const ContainerTypeRule* pSameStem = findByWrapperStem( rule._type );
        const bool               bMismatch = pSameStem != nullptr && ( pSameStem->_kind != rule._kind || pSameStem->_bState != rule._bState || pSameStem->_bFixed != rule._bFixed );
        if ( bMismatch )
        {
            SW_LOG_ERROR_ID( "ContainerTypeMap.WrapperStemDisagrees", "'%#' and '%#' share the wrapper %#Wrapper but differ in kind or roles - one wrapper has one shape",
                             pSameStem->_match, rule._match, rule._type );
            return false;
        }
        _listRule.push_back( rule );
        return true;
    }

    const ContainerTypeRule* ContainerTypeMap::findByMatch( const string_view match ) const
    {
        for ( const ContainerTypeRule& rule : _listRule )
        {
            if ( string_view{ rule._match } == match )
                return &rule;
        }
        return nullptr;
    }

    const ContainerTypeRule* ContainerTypeMap::findByWrapperStem( const string_view wrapperStem ) const
    {
        for ( const ContainerTypeRule& rule : _listRule )
        {
            if ( string_view{ rule._type } == wrapperStem )
                return &rule;
        }
        return nullptr;
    }

    const ContainerTypeRule* ContainerTypeMap::match( const string_view clangTypeSpelling ) const
    {
        // 바깥 템플릿 이름이 규칙과 **같아야** 맞는다(`ParserUtil::outerTemplateName` 설명 — 부분 문자열로 맞추면 `TextureAsset` 이
        // set 이 된다).
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
