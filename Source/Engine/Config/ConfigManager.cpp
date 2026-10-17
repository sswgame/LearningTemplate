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

        /**
         * @brief `PROPERTY( Required )` 칸 가운데 @p object 에 적히지 않은 것을 경로(`_listBone[2]._parentIndex`)로 모읍니다.
         * @details 구조체 칸과 구조체 목록(배열)의 원소 안으로 들어간다. 필수 칸의 초기값은 형식일 뿐이라, 빠진 칸을 초기값으로 읽으면
         *          "적지 않음" 이 조용히 그럴듯한 값(루트 본의 부모 -1 같은)이 된다.
         */
        void collectMissingRequired( const JSONValue& object, const TypeInfo& typeInfo, const string& prefix, vector<string>& outListPath )
        {
            for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
            {
                if ( prop._bIsBitField == SW_TRUE )
                    continue;
                const string    path  = prefix.empty() ? string( prop._name.c_str() ) : prefix + "." + prop._name.c_str();
                const JSONValue value = object.get( prop._name.c_str(), false );
                if ( value.isValid() == false )
                {
                    if ( prop._metadata._bRequired != SW_FALSE )
                        outListPath.push_back( path );
                    continue;
                }
                const bool      bContainer = prop._bIsContainer == SW_TRUE;
                const TypeInfo* pNested    = SerializerUtil::findNestedObjectType( bContainer ? prop._elementTypeName : prop._typeName, SerializeContext::getDefault() );
                if ( pNested == nullptr )
                    continue;
                if ( bContainer == false && value.isObject() )
                {
                    collectMissingRequired( value, *pNested, path, outListPath );
                    continue;
                }
                if ( bContainer == false || value.isArray() == false )
                    continue;
                for ( size_t elementIndex = 0; elementIndex < value.size(); ++elementIndex )
                {
                    const JSONValue element = value.at( elementIndex );
                    if ( element.isObject() )
                        collectMissingRequired( element, *pNested, path + "[" + to_string( static_cast<uint64>( elementIndex ) ) + "]", outListPath );
                }
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
        // JSON 객체가 아닌 본문만 여기서 멈춘다. 소프트 읽기의 실패는 그 둘(본문 · 범위 밖 값)을 가르지 않으므로, 실패에서 바로 돌아가면
        // 범위 밖 값 하나가 모르는 키 · 빠진 필수 칸의 오류를 삼킨다 — 모두 알린다는 계약(헤더)이 깨진다.
        JSONDocument document;
        if ( document.parse( jsonStr ) == false || document.getRoot().isObject() == false )
        {
            SW_LOG_ERROR( "Config %#: not a JSON object for %#", pSourceLabel, typeInfo._fullyQualifiedName.c_str() );
            return false;
        }
        vector<SchemaOrphanValue> listOrphan;
        // 여기서의 실패는 범위 밖 값이다 — 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 칸마다 이미 알렸다.
        const bool bWithinRange = JSONSerializer::deserializeSoft( pInstance, typeInfo, jsonStr, &listOrphan, nullptr, context );
        for ( const SchemaOrphanValue& orphan : listOrphan )
        {
            const utf8* const pName = orphan._writtenName.empty() ? orphan._name.c_str() : orphan._writtenName.c_str();
            SW_LOG_ERROR( "Config %#: key '%#' is unknown to %# or its value %# cannot be read", pSourceLabel, pName, typeInfo._fullyQualifiedName.c_str(),
                          orphan._text.c_str() );
        }
        // 필수 칸(`PROPERTY( Required )`)이 빠졌으면 초기값으로 읽지 않고 오류다 — 초기값은 형식이지 기본값이 아니다.
        vector<string> listMissing;
        collectMissingRequired( document.getRoot(), typeInfo, string(), listMissing );
        for ( const string& missingPath : listMissing )
        {
            SW_LOG_ERROR( "Config %#: required key '%#' of %# is missing", pSourceLabel, missingPath.c_str(), typeInfo._fullyQualifiedName.c_str() );
        }
        return bWithinRange && listOrphan.empty() && listMissing.empty();
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
