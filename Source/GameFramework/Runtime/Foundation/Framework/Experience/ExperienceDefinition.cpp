#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceDefinition.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogRoot.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ExperienceDefinitionInternal
        {
            /** @brief 루트 원소 이름입니다. */
            static constexpr const utf8* kRootName = "Experience";
            /** @brief `<GameMode>` 의 형식 속성 이름입니다(나머지 속성은 설정이다). */
            static constexpr const utf8* kGameModeTypeAttribute = "type";

            /** @brief 형식 @p typeInfo(상속 포함)에 속성 이름 @p name 의 칸이 있으면 true 입니다. */
            static bool hasProperty( const TypeInfo& typeInfo, string_view name )
            {
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    if ( XMLFields::getAttributeName( prop ) == name )
                        return true;
                }
                return false;
            }
        };
    } // namespace

    bool ExperienceDefinition::loadFromAsset( const AssetID& assetID, ExperienceDefinition& outDefinition )
    {
        ExperienceDefinition definition;
        if ( GameDataXML::loadFromAsset( definition, &ExperienceDefinition::loadRoot, assetID, ExperienceDefinitionInternal::kRootName ) == false )
            return false;
        outDefinition = std::move( definition );
        return true;
    }

    bool ExperienceDefinition::loadFromXMLText( string_view xmlText, string_view sourceName, ExperienceDefinition& outDefinition )
    {
        ExperienceDefinition definition;
        if ( GameDataXML::loadFromXMLText( definition, &ExperienceDefinition::loadRoot, xmlText, sourceName, ExperienceDefinitionInternal::kRootName ) == false )
            return false;
        outDefinition = std::move( definition );
        return true;
    }

    bool ExperienceDefinition::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 루트 속성은 스키마(빠진 필수 · 모르는 속성 · 깨진 값은 오류), 자식은 표(모르는 원소는 오류)다.
        bool bValid                            = root.readProperties( *this, sourceName );
        using Element                          = XMLCatalogElement<ExperienceDefinition>;
        static constexpr Element kArrElement[] = {
            Element::of<&ExperienceDefinition::loadGameMode>( "GameMode" ).uncounted(), // 없어도 되는 원소라 빈 파일 경고에 세지 않는다
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result.isClean() && bValid;
    }

    bool ExperienceDefinition::loadGameMode( const XMLNode& node, string_view sourceName )
    {
        if ( _gameMode.has_value() )
        {
            SW_LOG_ERROR_ID( "ExperienceDefinition.GameModeTwice", "%#: <GameMode> appears twice - an experience has one game mode", sourceName );
            return false;
        }
        if ( node.findChild() )
        {
            SW_LOG_ERROR_ID( "ExperienceDefinition.GameModeChild", "%#: <GameMode> has a child element - settings are attributes of <GameMode>", sourceName );
            return false;
        }
        ExperienceGameMode gameMode;
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            if ( StringUtil::equals( string_view( attribute.getName() ), string_view( ExperienceDefinitionInternal::kGameModeTypeAttribute ) ) )
                gameMode._type = hashed_string( StringUtil::trim( string_view( attribute.getValue() ) ) );
            else
                gameMode._listSetting.push_back( ExperienceSetting{ string( attribute.getName() ), string( attribute.getValue() ) } );
        }
        if ( gameMode._type.empty() )
        {
            SW_LOG_ERROR_ID( "ExperienceDefinition.GameModeNoType", "%#: <GameMode> needs type=\"<GameModeComponent type>\"", sourceName );
            return false;
        }
        _gameMode = std::move( gameMode );
        return true;
    }

    bool ExperienceDefinition::validate( const ExperienceKitRegistry& kits, const TypeRegistry& types, string_view sourceName ) const
    {
        bool bValid = true;
        for ( const hashed_string& kit : _listKit )
        {
            if ( kits.findKit( kit.view() ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "ExperienceDefinition.UnknownKit", "%#: kit '%#' is not registered (SW_EXPERIENCE_KIT) - check the name or the kit module", sourceName, kit.c_str() );
            bValid = false;
        }
        // 같은 이름을 두 번 켜면 같은 서비스를 두 번 걸어 둘째가 진다 — 읽을 때 막는다.
        for ( const vector<hashed_string>* pList : { &_listKit, &_listInputContext, &_listCatalog } )
        {
            const hashed_string* pDuplicate = VectorUtil::findDuplicate( *pList );
            if ( pDuplicate == nullptr )
                continue;
            SW_LOG_ERROR_ID( "ExperienceDefinition.DuplicateName", "%#: '%#' is listed twice", sourceName, pDuplicate->c_str() );
            bValid = false;
        }
        if ( _map.isSet() == false || AssetIO::hasAsset( AssetID{ _map.get() } ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceDefinition.MapMissing", "%#: map '%#' does not exist", sourceName, _map.isSet() ? _map.c_str() : "" );
            bValid = false;
        }
        for ( const string& document : _listUI )
        {
            if ( AssetIO::hasAsset( AssetID{ document } ) )
                continue;
            SW_LOG_ERROR_ID( "ExperienceDefinition.UIMissing", "%#: UI document '%#' does not exist", sourceName, document.c_str() );
            bValid = false;
        }
        return validateGameMode( types, sourceName ) && bValid;
    }

    bool ExperienceDefinition::validateGameMode( const TypeRegistry& types, string_view sourceName ) const
    {
        if ( _gameMode.has_value() == false )
            return true;
        const ExperienceGameMode& gameMode = *_gameMode;
        const TypeInfo* const     pType    = types.findType( gameMode._type );
        if ( pType == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceDefinition.GameModeUnknownType", "%#: <GameMode type=\"%#\"> is not a registered type", sourceName, gameMode._type.c_str() );
            return false;
        }
        if ( pType->isDerivedFrom( GameModeComponent::StaticType() ) == false || pType->canConstruct() == false )
        {
            SW_LOG_ERROR_ID( "ExperienceDefinition.GameModeNotGameMode", "%#: <GameMode type=\"%#\"> is not a constructible GameModeComponent type", sourceName, gameMode._type.c_str() );
            return false;
        }
        bool bValid = true;
        for ( const ExperienceSetting& setting : gameMode._listSetting )
        {
            if ( ExperienceDefinitionInternal::hasProperty( *pType, setting._name ) )
                continue;
            SW_LOG_ERROR_ID( "ExperienceDefinition.GameModeUnknownSetting", "%#: <GameMode type=\"%#\"> has no property '%#'", sourceName, gameMode._type.c_str(), setting._name.c_str() );
            bValid = false;
        }
        return bValid;
    }
} // namespace sw
