#include "pch.h"

#include "Engine/UI/Style/UITheme.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "UITheme" );

    bool UIThemeCatalog::loadFromResource( const ResourceID& resourceID )
    {
        const string_view resourcePath = resourceID.view();
        if ( XMLSerializer::loadFile( resourceID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR( "[UI] UI theme catalog could not be read or holds unknown keys: %#", resourcePath );
            return false;
        }
        return true;
    }

    const UIThemeDesc* UIThemeCatalog::findTheme( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&UIThemeDesc::_name>( _listTheme, name );
    }

    /** @brief UI 테마 카탈로그 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( UIThemes, "uithemes", nullptr, nullptr, "uithemes.xml", &ResourceDataKindUtil::loadCatalog<UIThemeCatalog> );
} // namespace sw
