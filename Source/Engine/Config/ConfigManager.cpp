#include "pch.h"

#include "Engine/Config/ConfigManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "ConfigManager" );

        /** @brief 호스트가 알린 표입니다(`ConfigManager::setPrimary`). */
        ConfigManager* s_pPrimaryConfigManager = nullptr;

        struct ConfigManagerInternal
        {
            /** @brief @p fileObject 의 키마다 두 쪽 값이 같은 JSON 이면 경로를 담습니다. 파일 쪽이 객체면 안으로 들어간다(구조체 칸). */
            static void collectEqualMembers( const JsonValue& fileObject, const JsonValue& left, const JsonValue& right, const string& prefix,
                                             vector<string>& outListKey )
            {
                for ( const string& key : fileObject.getMemberNames() )
                {
                    const string    path       = prefix.empty() ? key : string( prefix + "." + key );
                    const JsonValue fileValue  = fileObject.get( key, false );
                    const JsonValue leftValue  = left.get( key, false );
                    const JsonValue rightValue = right.get( key, false );
                    if ( leftValue.isValid() == false || rightValue.isValid() == false )
                        continue;
                    if ( fileValue.isObject() && leftValue.isObject() && rightValue.isObject() && fileValue.getMemberNames().empty() == false )
                    {
                        collectEqualMembers( fileValue, leftValue, rightValue, path, outListKey );
                        continue;
                    }
                    if ( leftValue.dump() == rightValue.dump() )
                        outListKey.push_back( path );
                }
            }

            /** @brief 숫자 칸의 값을 float64 로 읽습니다. 숫자 타입이 아니면 false 입니다. */
            [[nodiscard]] static bool readNumber( const PropertyInfo& prop, const void* pInstance, float64& outValue )
            {
                const void* const pValue   = prop.getRawPtr( pInstance );
                const string_view typeName = prop._typeName.view();
                if ( typeName == "float32" || typeName == "float" )
                    outValue = static_cast<float64>( *static_cast<const float32*>( pValue ) );
                else if ( typeName == "float64" || typeName == "double" )
                    outValue = *static_cast<const float64*>( pValue );
                else if ( typeName == "int32" || typeName == "int" )
                    outValue = static_cast<float64>( *static_cast<const int32*>( pValue ) );
                else if ( typeName == "uint32" )
                    outValue = static_cast<float64>( *static_cast<const uint32*>( pValue ) );
                else if ( typeName == "int64" )
                    outValue = static_cast<float64>( *static_cast<const int64*>( pValue ) );
                else if ( typeName == "uint64" )
                    outValue = static_cast<float64>( *static_cast<const uint64*>( pValue ) );
                else if ( typeName == "int16" )
                    outValue = static_cast<float64>( *static_cast<const int16*>( pValue ) );
                else if ( typeName == "uint16" )
                    outValue = static_cast<float64>( *static_cast<const uint16*>( pValue ) );
                else if ( typeName == "int8" )
                    outValue = static_cast<float64>( *static_cast<const int8*>( pValue ) );
                else if ( typeName == "uint8" )
                    outValue = static_cast<float64>( *static_cast<const uint8*>( pValue ) );
                else
                    return false;
                return true;
            }

            /**
             * @brief `Min`/`Max` 밖 숫자 칸마다 "경로 = 값 is outside [아래, 위]" 한 줄을 모읍니다.
             * @details 구조체 칸은 들어가 본다. 컨테이너 · 비트필드 · 접근자 칸은 보지 않는다(원소 검사는 타입의 validate 몫).
             */
            static void collectRangeErrors( const TypeInfo& typeInfo, const void* pInstance, const string& prefix, vector<string>& outListError )
            {
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    if ( prop._bIsBitField == SW_TRUE || prop._bIsContainer == SW_TRUE || prop.hasValueAccessor() )
                        continue;
                    const string    path    = prefix.empty() ? string( prop._name.c_str() ) : prefix + "." + prop._name.c_str();
                    const TypeInfo* pNested = engine::getTypeRegistry().findType( prop._typeName );
                    if ( pNested != nullptr && pNested->isPrimitive() == false )
                    {
                        collectRangeErrors( *pNested, prop.getRawPtr( pInstance ), path, outListError );
                        continue;
                    }
                    const PropertyMetadata& meta = prop._metadata;
                    if ( meta._bHasMinRange == SW_FALSE && meta._bHasMaxRange == SW_FALSE )
                        continue;
                    float64 value{ 0.0 };
                    if ( readNumber( prop, pInstance, value ) == false )
                        continue;
                    const bool bBelow = meta._bHasMinRange == SW_TRUE && value < static_cast<float64>( meta._minRange );
                    const bool bAbove = meta._bHasMaxRange == SW_TRUE && value > static_cast<float64>( meta._maxRange );
                    if ( bBelow == false && bAbove == false )
                        continue;
                    const string lower = meta._bHasMinRange == SW_TRUE ? to_string( meta._minRange ) : string( "-" );
                    const string upper = meta._bHasMaxRange == SW_TRUE ? to_string( meta._maxRange ) : string( "-" );
                    outListError.push_back( path + " = " + to_string( value ) + " is outside [" + lower + ", " + upper + "]" );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ConfigManager::readConfigJson( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr, const utf8* pSourceLabel )
    {
        // 키는 대소문자까지 맞아야 한다 — 기본 문맥은 대소문자를 무시해 `_Width` 오타를 그대로 받아들인다.
        SerializeContext ctx = SerializeContext::deriveFromDefault();
        ctx.setIgnoreCaseKeys( false );
        vector<SchemaOrphanValue> listOrphan;
        if ( JsonSerializer::deserializeSoft( pInstance, typeInfo, jsonStr, &listOrphan, nullptr, ctx ) == false )
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
        vector<string> listRangeError;
        ConfigManagerInternal::collectRangeErrors( typeInfo, pInstance, string(), listRangeError );
        for ( const string& rangeError : listRangeError )
        {
            SW_LOG_ERROR( "Config %#: %#", pSourceLabel, rangeError.c_str() );
        }
        return listOrphan.empty() && listRangeError.empty();
    }

    bool ConfigManager::collectEqualKeys( const void* pLeft, const void* pRight, const TypeInfo& typeInfo, string_view jsonStr, vector<string>& outListKey )
    {
        JsonDocument fileDoc;
        JsonDocument leftDoc;
        JsonDocument rightDoc;
        if ( fileDoc.parse( jsonStr ) == false || leftDoc.parse( JsonSerializer::serialize( pLeft, typeInfo ) ) == false ||
             rightDoc.parse( JsonSerializer::serialize( pRight, typeInfo ) ) == false )
            return false;
        ConfigManagerInternal::collectEqualMembers( fileDoc.getRoot(), leftDoc.getRoot(), rightDoc.getRoot(), string(), outListKey );
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

    ConfigManager* ConfigManager::findPrimary()
    {
        return s_pPrimaryConfigManager;
    }

    void ConfigManager::setPrimary( ConfigManager* pManager )
    {
        s_pPrimaryConfigManager = pManager;
    }
} // namespace sw
