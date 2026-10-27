#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GameSettings.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "GameSettings" );

    bool GameSettings::loadFromResource( string_view assetRelativePath )
    {
        // 게임별 기본 경로를 엔진이 알 필요는 없다. 경로가 없으면 로드할 것도 없다.
        if ( assetRelativePath.empty() )
            return false;

        const string path = string( assetRelativePath );
        if ( ResourceIO::hasResource( ResourceID{ path } ) == false )
            return false;

        // **먼저 비운다.**
        // 주의: 안 비우면 팩을 바꿔 다시 읽을 때 앞 팩의 커스텀 프로퍼티가 그대로 남아,
        // 새 팩에 없는 키를 물으면 **없어진 팩의 값**이 나온다.
        *this = GameSettings{};
        return GameDataXML::loadFile( *this, &GameSettings::loadRoot, path, "GameSettings" );
    }

    bool GameSettings::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = GameSettings{};
        return GameDataXML::loadText( *this, &GameSettings::loadRoot, xmlText, sourceName, "GameSettings" );
    }

    namespace
    {
        /** @brief 칸 @p prop 의 XML 자식 요소 이름입니다 — 필드 이름에서 앞의 `_` 를 뗀 것(`_startMap` → `startMap`). */
        string_view makeElementName( const PropertyInfo& prop )
        {
            string_view name = prop._name.view();
            if ( name.empty() == false && name.front() == '_' )
                name.remove_prefix( 1 );
            return name;
        }
    } // namespace

    bool GameSettings::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸 이름의 원본은 리플렉션 칸 표 하나다. 경로 칸은 `DataPath` 처리기만 값을 넣을 수 있어 이 텍스트 읽기를 거친다.
        const SerializeContext& context      = SerializeContext::getDefault( SerializePurpose::Authoring );
        const auto&             listProperty = StaticType()->getPropertiesWithBase();
        bool                    bOk          = true;
        for ( XMLNode child = root.findChild(); child.isValid() == true; child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::isNullOrEmpty( pName ) )
                continue;

            // 컴포넌트 기본값 — `ComponentDefaults` 가 같은 파일을 읽는다.
            if ( StringUtil::equals( pName, "Defaults" ) )
                continue;
            // 엔진 칸(창 제목 · 스키마 · UI 규칙) — 엔진의 `PackEngineSettings` 가 같은 파일에서 읽는다. 이 구조체는 읽지 않는다.
            if ( StringUtil::equals( pName, PackEngineSettings::kElementName ) )
                continue;

            const PropertyInfo* pFound = nullptr;
            for ( const PropertyInfo& prop : listProperty )
            {
                if ( makeElementName( prop ) == string_view( pName ) )
                    pFound = &prop;
            }
            if ( pFound == nullptr )
            {
                SW_LOG_ERROR( "GameSettings %#: unknown element <%#> - game data goes in the game's own catalogs (there is no free-form key-value section)", sourceName, pName );
                bOk = false;
                continue;
            }
            const utf8* const pText = child.getText() != nullptr ? child.getText() : "";
            if ( SerializerUtil::applyPropertyText( *pFound, MutableObjectAddress{ this }, pText, context ) == false )
            {
                SW_LOG_ERROR( "GameSettings %#: <%#>%#</%#> cannot be read as %#", sourceName, pName, pText, pName, pFound->_typeName.c_str() );
                bOk = false;
            }
        }

        // 필수 칸이 빠지면 오류다. 대신 쓸 값은 없다.
        const hashed_string dataPathType( "DataPath" );
        for ( const PropertyInfo& prop : listProperty )
        {
            if ( prop._metadata->_bRequired == SW_FALSE )
                continue;
            const DataPath* pPath    = prop._typeName == dataPathType ? prop.getValuePtr<DataPath>( this ) : nullptr;
            const bool      bMissing = pPath != nullptr && pPath->isSet() == false;
            if ( bMissing == false )
                continue;
            SW_LOG_ERROR( "GameSettings %#: <%#> is required (a resource id such as game/<pack>/maps/start.scene.xml)", sourceName, makeElementName( prop ) );
            bOk = false;
        }
        if ( bOk == false )
            return false;

        SW_LOG_INFO( "Loaded from %# (start=%#)", sourceName, _startMap.c_str() );
        return true;
    }

    string BootstrapConfig::resolve( string_view packRelative ) const
    {
        const string pack = FileUtil::trimTrailingSlashes( _packRoot.get() );
        return FileUtil::joinPath( pack, FileUtil::normalizePath( packRelative ) );
    }

    bool BootstrapConfig::load( string_view gameSettingsFileName )
    {
        return _data.loadFromResource( resolve( gameSettingsFileName ) );
    }
} // namespace sw
