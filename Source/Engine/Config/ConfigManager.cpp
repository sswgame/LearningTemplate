#include "pch.h"

#include "Engine/Config/ConfigManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

#if defined( SW_SHIPPING )
    #include "sw/config/ShippingGameDefaults.h" // 구운 게임의 프리셋 — Dev 는 디스크에서 고른 게임의 것을 읽어 활성 게임과 무관하다
#endif

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "ConfigManager" );

        /** @brief 호스트가 알린 표입니다(`ConfigManager::setPrimary`). */
        ConfigManager* s_pPrimaryConfigManager = nullptr;

        /** @brief @p fileObject 의 키마다 두 쪽 값이 같은 JSON 이면 경로를 담습니다. 파일 쪽이 객체면 안으로 들어간다(구조체 칸). */
        void collectEqualMembers( const JSONValue& fileObject, const JSONValue& left, const JSONValue& right, const string& prefix,
                                  vector<string>& outListKey )
        {
            for ( const string& key : fileObject.getMemberNames() )
            {
                const string    path       = prefix.empty() ? key : string( prefix + "." + key );
                const JSONValue fileValue  = fileObject.get( key, false );
                const JSONValue leftValue  = left.get( key, false );
                const JSONValue rightValue = right.get( key, false );
                if ( leftValue.isValid() == false || rightValue.isValid() == false )
                    continue;
                const bool bRecurseIntoMembers = fileValue.isObject() && leftValue.isObject() && rightValue.isObject() && fileValue.getMemberNames().empty() == false;
                if ( bRecurseIntoMembers )
                {
                    collectEqualMembers( fileValue, leftValue, rightValue, path, outListKey );
                    continue;
                }
                if ( leftValue.dump() == rightValue.dump() )
                    outListKey.push_back( path );
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool ConfigManager::readConfigJSON( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr, const utf8* pSourceLabel )
    {
        // 키는 대소문자까지 맞아야 한다 — 기본 문맥은 대소문자를 무시해 `_Width` 오타를 그대로 받아들인다.
        SerializeContext context = SerializeContext::deriveFromDefault();
        context.setIgnoreCaseKeys( false );
        vector<SchemaOrphanValue> listOrphan;
        if ( JSONSerializer::deserializeSoft( pInstance, typeInfo, jsonStr, &listOrphan, nullptr, context ) == false )
        {
            SW_LOG_ERROR( "Config %#: not a JSON object for %#", pSourceLabel, typeInfo._fullyQualifiedName.c_str() );
            return false;
        }
        for ( const SchemaOrphanValue& orphan : listOrphan )
        {
            const utf8* const pName = orphan._writtenName.empty() ? orphan._name.c_str() : orphan._writtenName.c_str();
            SW_LOG_ERROR( "Config %#: key '%#' is unknown to %# or its value %# cannot be read", pSourceLabel, pName, typeInfo._fullyQualifiedName.c_str(),
                          orphan._text.c_str() );
        }
        const bool bWithinRange = isWithinRange( pInstance, typeInfo, pSourceLabel );
        return listOrphan.empty() && bWithinRange;
    }

    bool ConfigManager::isWithinRange( const void* pInstance, const TypeInfo& typeInfo, const utf8* pSourceLabel )
    {
        vector<string> listRangeError;
        SerializerUtil::collectRangeErrors( typeInfo, pInstance, string(), listRangeError );
        for ( const string& rangeError : listRangeError )
        {
            SW_LOG_ERROR( "Config %# (%#): %#", pSourceLabel, typeInfo._fullyQualifiedName.c_str(), rangeError.c_str() );
        }
        return listRangeError.empty();
    }

    bool ConfigManager::collectEqualKeys( const void* pLeft, const void* pRight, const TypeInfo& typeInfo, string_view jsonStr, vector<string>& outListKey )
    {
        JSONDocument fileDoc;
        JSONDocument leftDoc;
        JSONDocument rightDoc;
        const bool   bParseFailed = fileDoc.parse( jsonStr ) == false || leftDoc.parse( JSONSerializer::serialize( pLeft, typeInfo ) ) == false ||
                                  rightDoc.parse( JSONSerializer::serialize( pRight, typeInfo ) ) == false;
        if ( bParseFailed )
            return false;
        collectEqualMembers( fileDoc.getRoot(), leftDoc.getRoot(), rightDoc.getRoot(), string(), outListKey );
        return true;
    }

    bool ConfigManager::reloadConfigFile( string_view changedPath )
    {
        for ( const auto& [key, source] : _mapSource )
        {
            (void)key;
            if ( source._pReload != nullptr && FileUtil::pathsEqualNormalized( source._resolvedPath, changedPath ) )
                return source._pReload( *this, source._resolvedPath );
        }
        return false;
    }

    ConfigManager::~ConfigManager()
    {
        if ( s_pPrimaryConfigManager == this )
            s_pPrimaryConfigManager = nullptr;
    }

    ConfigManager* ConfigManager::findPrimary()
    {
        return s_pPrimaryConfigManager;
    }

    void ConfigManager::setPrimary( ConfigManager* pManager )
    {
        s_pPrimaryConfigManager = pManager;
    }

} // namespace sw

namespace sw
{
    SubsystemInitResult ConfigManager::initializeSubsystem()
    {
        setRootDirectory( ResourcePaths::getProjectFolderPath() );
        setPrimary( this ); // 거두는 것은 이 표의 소멸자다
#if defined( SW_SHIPPING )
        EngineConfig probe;
        if ( JSONSerializer::deserialize( &probe, *EngineConfig::StaticType(), shipping_host::kEngineConfigJSON ) == false )
        {
            SW_LOG_ERROR( "The generated EngineConfig JSON does not deserialize - a reflection registrar was dropped at link time" );
            return SubsystemInitResult::Failed;
        }
        const utf8* const pGameConfigJSON = shipping_host::kGameConfigJSON;
#else
        const utf8* const pGameConfigJSON = nullptr;
#endif
        const EngineConfig* pEngineConfig = ensureConfig<EngineConfig>( config::kFileRuntimeEngineConfig, shipping_host::kEngineConfigJSON );
        if ( pEngineConfig == nullptr )
            return SubsystemInitResult::Failed;
        EngineConfig::setActive( *pEngineConfig );
        const GameConfig* pGameConfig = ensureConfig<GameConfig>( GameSelection::getConfigPath(), pGameConfigJSON );
        if ( pGameConfig == nullptr )
            return SubsystemInitResult::Failed;
        GameConfig::setActive( *pGameConfig );
        return SubsystemInitResult::Succeeded;
    }
} // namespace sw
