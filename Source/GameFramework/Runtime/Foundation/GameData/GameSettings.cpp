#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Input/Map/InputMapBundle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogRoot.h"

namespace sw
{
    bool GameSettings::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetRelativePath = assetID.view();
        // 게임별 기본 경로를 엔진이 알 필요는 없다. 경로가 없으면 로드할 것도 없다.
        if ( assetRelativePath.empty() )
            return false;

        if ( AssetIO::hasAsset( assetID ) == false )
            return false;

        // **먼저 비운다.**
        // 주의: 안 비우면 팩을 바꿔 다시 읽을 때 앞 팩의 커스텀 프로퍼티가 그대로 남아,
        // 새 팩에 없는 키를 물으면 **없어진 팩의 값**이 나온다.
        *this = GameSettings{};
        return GameDataXML::loadFromAsset( *this, &GameSettings::loadRoot, assetID, "GameSettings" );
    }

    bool GameSettings::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = GameSettings{};
        return GameDataXML::loadFromXMLText( *this, &GameSettings::loadRoot, xmlText, sourceName, "GameSettings" );
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

    bool GameSettings::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸 이름의 원본은 리플렉션 칸 표 하나다. 경로 칸은 `DataPath` 처리기만 값을 넣을 수 있어 이 텍스트 읽기를 거친다.
        // 손 읽기: 원소 이름이 리플렉션 칸 표에서 나오고 다른 읽는 쪽(엔진 설정 · 입력 맵 · 기본값)의 원소를 건너뛴다 — 고정 원소 표로 적을 수 없다.
        const XMLNode&          rootNode     = root.getNodeForHandRead();
        const SerializeContext& context      = SerializeContext::getDefault( SerializePurpose::Authoring );
        const auto&             listProperty = StaticType()->getPropertiesWithBase();
        bool                    bOk          = true;
        for ( XMLNode child = rootNode.findChild(); child.isValid() == true; child = child.findNextSibling() )
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
            // 입력 맵 묶음 — 엔진의 `InputMapBundle` 이 같은 파일에서 읽는다. 게임 시작 · 쿠킹 · 패널이 같은 길을 쓴다.
            if ( StringUtil::equals( pName, InputMapBundle::kSettingsElement ) )
                continue;
            // 팩 밖에서 읽는 필수 데이터 — 종류 하나에 팩 하나다. 자리는 읽는 쪽(`GameInstance::makeRequiredDataPath`)이 등록 줄의 규칙으로 만든다.
            if ( StringUtil::equals( pName, "RequiredData" ) )
            {
                RequiredDataParams line;
                const bool         bRead = XMLFields::readProperties( child, line, sourceName ); // 두 칸 모두 필수
                if ( bRead == false || line._kind.empty() || line._pack.empty() )
                {
                    SW_LOG_ERROR_ID( "GameSettings.GameSettingsRequiredDataNeedsBoth", "GameSettings %#: <RequiredData> needs both kind and pack (kind='%#' pack='%#')", sourceName, line._kind.c_str(), line._pack.c_str() );
                    bOk = false;
                }
                else if ( _mapRequiredDataPack.emplace( line._kind, line._pack ).second == false )
                {
                    SW_LOG_ERROR_ID( "GameSettings.GameSettingsRequiredDataKindAppears", "GameSettings %#: <RequiredData kind=\"%#\"> appears twice", sourceName, line._kind.c_str() );
                    bOk = false;
                }
                continue;
            }

            const PropertyInfo* pFound = nullptr;
            for ( const PropertyInfo& prop : listProperty )
            {
                if ( makeElementName( prop ) == string_view( pName ) )
                    pFound = &prop;
            }
            if ( pFound == nullptr )
            {
                SW_LOG_ERROR_ID( "GameSettings.GameSettingsUnknownElementGame", "GameSettings %#: unknown element <%#> - game data goes in the game's own catalogs (there is no free-form key-value section)", sourceName, pName );
                bOk = false;
                continue;
            }
            const utf8* const pText = child.getText() != nullptr ? child.getText() : "";
            if ( SerializerUtil::applyPropertyText( *pFound, MutableObjectAddress{ this }, pText, context ) == false )
            {
                SW_LOG_ERROR_ID( "GameSettings.GameSettingsCannotRead", "GameSettings %#: <%#>%#</%#> cannot be read as %#", sourceName, pName, pText, pName, pFound->_typeName.c_str() );
                bOk = false;
            }
        }

        // 필수 칸은 적혀 있어야 하고, 이름 · 경로 형식은 빈 글도 빠진 것이다 — 판정은 직렬화기 · XMLFields 와 한 함수다(`SerializerUtil::isRequiredValueMissing`).
        for ( const PropertyInfo& prop : listProperty )
        {
            const string      elementName{ makeElementName( prop ) };
            const XMLNode     element = rootNode.findChild( elementName.c_str() );
            const string_view text    = element.isValid() && element.getText() != nullptr ? string_view( element.getText() ) : string_view{};
            if ( SerializerUtil::isRequiredValueMissing( prop, element.isValid(), element.isValid() ? &text : nullptr, RequiredScope::Root ) == false )
                continue;
            SW_LOG_ERROR_ID( "GameSettings.GameSettingsRequiredResourceId", "GameSettings %#: <%#> is required (PROPERTY( Required ) - the initial value is not a default)", sourceName, elementName.c_str() );
            bOk = false;
        }
        if ( bOk == false )
            return false;

        SW_LOG_INFO_ID( "GameSettings.LoadedStart", "Loaded from %# (start=%#)", sourceName, _startMap.c_str() );
        return true;
    }

    string GamePackConfig::resolve( string_view packRelative ) const
    {
        const string pack = FileUtil::trimTrailingSlashes( _packRoot.get() );
        return FileUtil::joinPath( pack, FileUtil::normalizePath( packRelative ).str() );
    }

    bool GamePackConfig::load( string_view gameSettingsFileName )
    {
        return _data.loadFromAsset( AssetID{ resolve( gameSettingsFileName ) } );
    }
} // namespace sw
