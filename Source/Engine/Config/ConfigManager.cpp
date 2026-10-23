#include "pch.h"

#include "Engine/Config/ConfigManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

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
                    if ( prop._metadata->_bRequired != SW_FALSE )
                        outListPath.push_back( path );
                    continue;
                }
                const bool      bContainer = prop._bIsContainer == SW_TRUE;
                const TypeInfo* pNested    = SerializerUtil::findNestedObjectType( bContainer ? prop._elementTypeName : prop._typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
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

        /**
         * @brief `PROPERTY( Required )` 칸 가운데 @p element 에 적히지 않은 것을 경로(`_listBone[2]._parentIndex`)로 모읍니다. `collectMissingRequired` 의 XML 판입니다.
         * @details 적는 자리는 속성이거나 자식 요소다. 구조체 칸은 이름이 칸 이름인 자식 요소, 구조체 목록은 칸 이름의 자식 요소 안에 원소가 차례로 들어 있다(`XMLSerializer`).
         *          그 안으로 들어가 원소마다 본다 — 최상위만 보면 목록 원소 안의 빠진 칸이 초기값으로 읽힌다.
         */
        void collectMissingRequiredXML( const XMLNode& element, const TypeInfo& typeInfo, const string& prefix, vector<string>& outListPath )
        {
            for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
            {
                const string  path     = prefix.empty() ? string( prop._name.c_str() ) : prefix + "." + prop._name.c_str();
                const XMLNode child    = element.findChild( prop._name.c_str(), false );
                const bool    bWritten = element.findAttribute( prop._name.c_str(), false ) != nullptr || child.isValid();
                if ( bWritten == false )
                {
                    if ( prop._metadata->_bRequired != SW_FALSE )
                        outListPath.push_back( path );
                    continue;
                }
                if ( child.isValid() == false )
                    continue;
                const bool      bContainer = prop._bIsContainer == SW_TRUE;
                const TypeInfo* pNested    = SerializerUtil::findNestedObjectType( bContainer ? prop._elementTypeName : prop._typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
                if ( pNested == nullptr )
                    continue;
                if ( bContainer == false )
                {
                    collectMissingRequiredXML( child, *pNested, path, outListPath );
                    continue;
                }
                size_t elementIndex{ 0 };
                for ( XMLNode item = child.findChild(); item.isValid(); item = item.findNextSibling(), ++elementIndex )
                {
                    collectMissingRequiredXML( item, *pNested, path + "[" + to_string( static_cast<uint64>( elementIndex ) ) + "]", outListPath );
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
        SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
        context.setIgnoreCaseKeys( false );
        // JSON 객체가 아닌 본문(`SoftReadResult::NotParsed`)만 여기서 멈춘다. 범위 밖 값(`Rejected`)에서 바로 돌아가면
        // 그 값 하나가 모르는 키 · 빠진 필수 칸의 오류를 삼킨다 — 모두 알린다는 계약(헤더)이 깨진다.
        JSONDocument document;
        if ( document.parse( jsonStr ) == false || document.getRoot().isObject() == false )
        {
            SW_LOG_ERROR( "Config %#: not a JSON object for %#", pSourceLabel, typeInfo._fullyQualifiedName.c_str() );
            return false;
        }
        vector<SchemaOrphanValue> listOrphan;
        // 본문은 위에서 객체로 읽혔으므로 `Read` 가 아니면 범위 밖 값이다 — 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 칸마다 이미 알렸다.
        const bool bWithinRange = JSONSerializer::deserializeSoft( pInstance, typeInfo, jsonStr, &listOrphan, nullptr, context ) == SoftReadResult::Read;
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

    bool ConfigManager::readConfigXMLElement( void* pInstance, const TypeInfo& typeInfo, string_view elementXML, const utf8* pSourceLabel )
    {
        XMLDocument document;
        if ( document.parse( elementXML, pSourceLabel ) == false || document.getRoot().isValid() == false )
        {
            SW_LOG_ERROR( "Config %#: not an XML element for %#", pSourceLabel, typeInfo._fullyQualifiedName.c_str() );
            return false;
        }
        // 키는 대소문자까지 맞아야 한다(`readConfigJSON` 과 같다). 범위 밖 값은 칸마다 읽기 공통 끝이 이미 알렸다.
        SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
        context.setIgnoreCaseKeys( false );
        vector<SchemaOrphanValue> listOrphan;
        const bool                bWithinRange = XMLSerializer::deserializeSoft( pInstance, typeInfo, elementXML, &listOrphan, nullptr, context ) == SoftReadResult::Read;
        for ( const SchemaOrphanValue& orphan : listOrphan )
        {
            const utf8* const pName = orphan._writtenName.empty() ? orphan._name.c_str() : orphan._writtenName.c_str();
            SW_LOG_ERROR( "Config %#: '%#' is unknown to %# or its value %# cannot be read", pSourceLabel, pName, typeInfo._fullyQualifiedName.c_str(), orphan._text.c_str() );
        }
        // 필수 칸은 속성이나 자식으로 적혀 있어야 한다(구조체 칸 · 목록 원소 안까지) — 초기값은 형식이지 기본값이 아니다.
        vector<string> listMissing;
        collectMissingRequiredXML( document.getRoot(), typeInfo, string(), listMissing );
        for ( const string& missingPath : listMissing )
        {
            SW_LOG_ERROR( "Config %#: required '%#' of %# is missing", pSourceLabel, missingPath.c_str(), typeInfo._fullyQualifiedName.c_str() );
        }
        return bWithinRange && listOrphan.empty() && listMissing.empty();
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

    ReloadOutcome ConfigManager::reloadConfigFile( string_view changedPath )
    {
        for ( const auto& [key, source] : _mapSource )
        {
            (void)key;
            if ( source._pReload != nullptr && FileUtil::pathsEqualNormalized( source._resolvedPath, changedPath ) )
                return source._pReload( *this, source._resolvedPath ) ? ReloadOutcome::Reloaded : ReloadOutcome::Failed;
        }
        return ReloadOutcome::NotTracked;
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
        // Shipping 은 디스크의 Config/ 를 보지 않고 구운 JSON 만 읽는다. 그 밖은 디스크 파일이 필수다 — 없으면 구운 사본이나 `T{}` 로 서지 않고 멈춘다.
        const ConfigSource engineSource = ConfigSource::baked( shipping_host::kEngineConfigJSON );
        const ConfigSource gameSource   = ConfigSource::baked( shipping_host::kGameConfigJSON );
#else
        const ConfigSource engineSource = ConfigSource::disk( config::kFileRuntimeEngineConfig );
        const ConfigSource gameSource   = ConfigSource::disk( GameSelection::getConfigPath() );
#endif
        const EngineConfig* pEngineConfig = loadHostConfig<EngineConfig>( engineSource );
        if ( pEngineConfig == nullptr )
            return SubsystemInitResult::Failed;
        EngineConfig::setActive( *pEngineConfig );
        const GameConfig* pGameConfig = loadHostConfig<GameConfig>( gameSource );
        if ( pGameConfig == nullptr )
            return SubsystemInitResult::Failed;
        GameConfig::setActive( *pGameConfig );
        return SubsystemInitResult::Succeeded;
    }
} // namespace sw
