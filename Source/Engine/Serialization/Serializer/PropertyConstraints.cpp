#include "pch.h"

#include "Engine/Serialization/Serializer/PropertyConstraints.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Serializer/ValueSerializer.h"

namespace sw
{
    namespace
    {
        struct PropertyConstraintsInternal
        {
            /**
             * @brief 숫자 칸 하나를 float64 로 읽습니다. 숫자 타입이 아니면 false 입니다(범위 검사가 보지 않는 칸).
             * @details 숫자 판정은 내장 타입 표 한 곳(`ReflectedValueUtil::readNumber`)이 한다 — 성분이 하나인 압축 타입(half · unorm · snorm)도 범위를 본다.
             */
            [[nodiscard]] static bool readRangeNumber( const ReflectBuiltinIndex builtinIndex, const ValueAddress value, float64& outValue )
            {
                return ReflectedValueUtil::readNumber( builtinIndex, value.get(), outValue ); // kNone 이면 false
            }

            /** @brief @p value 가 @p range 밖이면 "경로 = 값 is outside [아래, 위]" 한 줄을 더합니다. */
            static void appendRangeError( const string& path, const float64 value, const PropertyAllowedRange& range, vector<string>& outListError )
            {
                if ( range.contains( value ) )
                    return;
                const string                             lower = range._bHasMin ? to_string( range._min ) : string( "-" );
                const string                             upper = range._bHasMax ? to_string( range._max ) : string( "-" );
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%# = %# is outside [%#, %#]", path, value, lower, upper );
                outListError.push_back( string( textBuilder.view() ) );
            }

            /** @brief @p value(@p builtinIndex 의 숫자)가 @p range 안인지입니다. 숫자가 아니면 범위가 없으므로 true 입니다. */
            static bool isNumberWithinRange( const ReflectBuiltinIndex builtinIndex, const ValueAddress value, const PropertyAllowedRange& range )
            {
                float64 number = 0.0;
                if ( readRangeNumber( builtinIndex, value, number ) == false )
                    return true;
                return range.contains( number );
            }

            /**
             * @brief `collectRangeErrors` 와 같은 칸을 같은 규칙으로 보되 글을 만들지 않고 첫 위반에서 멈춥니다. 읽기마다 도는 빠른 길입니다.
             * @details 범위를 선언하지 않은 숫자 칸은 값을 읽지 않습니다. 위반이 있을 때만 `collectRangeErrors` 가 경로가 붙은 글을 만듭니다.
             *          범위는 저장 목적과 관계없이 모든 칸을 본다 — 중첩 타입 판정(`findNestedObjectType`)에 내장 핸들러 표만 쓰므로 저작 목적의 기본 문맥을 쓴다.
             */
            static bool isEveryValueWithinRange( const TypeInfo& typeInfo, const ObjectAddress instance )
            {
                const void* const       pInstance = instance.get();
                const SerializeContext& context   = SerializeContext::getDefault( SerializePurpose::Authoring );
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    const bool bSkipProperty = prop._bIsBitField == SW_TRUE || prop.hasValueAccessor();
                    if ( bSkipProperty )
                        continue;
                    const void* const pValue = prop.getRawPtr( pInstance );
                    if ( prop._bIsContainer == SW_TRUE )
                    {
                        const bool bNestedContainer = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
                        const bool bSequence        = prop._containerWrapper != nullptr && prop._containerWrapper->asSequence() != nullptr && bNestedContainer == false;
                        if ( bSequence == false )
                            continue;
                        const TypeInfo* pElement     = ValueSerializer::findNestedObjectType( prop._elementTypeName, prop.findElementBinding(), context );
                        const bool      bNumberRange = pElement == nullptr && prop._metadata->hasAllowedRange();
                        if ( pElement == nullptr && bNumberRange == false )
                            continue;
                        // 필드 경계는 원소가 아니라 이 프로퍼티를 가진 객체에서 읽는다(인스펙터와 같다).
                        const PropertyAllowedRange       range          = bNumberRange ? prop._metadata->computeAllowedRange( pInstance ) : PropertyAllowedRange{};
                        const ReflectBuiltinIndex        elementBuiltin = prop.resolveElementFacts( context.getTypeRegistry() )._builtinIndex; // 칸마다 한 번
                        const ISequenceContainerWrapper* pSequence      = prop._containerWrapper->asSequence();
                        const size_t                     elementCount   = pSequence->getSize( pValue );
                        for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                        {
                            // 구조체 원소의 값 주소는 그 원소 타입의 객체 주소다.
                            const void* const pElementValue = pSequence->getElementConst( pValue, elementIndex );
                            const bool        bWithin       = pElement != nullptr ? isEveryValueWithinRange( *pElement, ObjectAddress{ pElementValue } )
                                                                                  : isNumberWithinRange( elementBuiltin, ValueAddress{ pElementValue }, range );
                            if ( bWithin == false )
                                return false;
                        }
                        continue;
                    }
                    // 숫자 칸은 범위를 선언했을 때만 본다. 숫자가 아닌 칸만 중첩 구조체인지 찾는다(숫자 칸마다 타입 조회를 하지 않는다).
                    float64 value = 0.0;
                    if ( readRangeNumber( prop.resolveValueFacts( context.getTypeRegistry() )._builtinIndex, ValueAddress{ pValue }, value ) )
                    {
                        if ( prop._metadata->hasAllowedRange() && prop._metadata->computeAllowedRange( pInstance ).contains( value ) == false )
                            return false;
                        continue;
                    }
                    const TypeInfo* pNested = ValueSerializer::findNestedObjectType( prop._typeName, prop.findValueBinding(), context );
                    if ( pNested != nullptr && isEveryValueWithinRange( *pNested, ObjectAddress{ pValue } ) == false )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PropertyConstraints::isNameValueType( const hashed_string& typeName )
    {
        // 이름 · 경로 형식은 빈 글이 "없음" 이다. `string` 은 넣지 않는다 — 글 칸은 빈 값이 뜻 있는 값일 수 있다(명령 표의 "단축키 없음" 같은 고정 열).
        // 다른 데이터를 가리키는 이름은 hashed_string 으로 둔다(리플렉션 파서가 기본값을(`PROPERTY( Required )` · `DefaultName`), 이 판정이 빈 값을 막는다).
        // 형식 이름은 파서가 정규화해 싣는다(`AssetRef<T>` 는 "AssetRef", `sw::` 없음) — 철자를 다듬지 않고 정규 이름(인턴된 이름)끼리 견준다.
        static const hashed_string kArrNameType[] = { hashed_string( PredefinedNameType::NameType_hashed_string ), hashed_string( PredefinedNameType::NameType_TagID ),
                                                      hashed_string( "DataPath" ),
                                                      hashed_string( "AssetID" ),
                                                      hashed_string( "CatalogKey" ),
                                                      hashed_string( "AssetRef" ) };
        for ( const hashed_string& nameType : kArrNameType )
        {
            if ( typeName == nameType )
                return true;
        }
        return false;
    }

    bool PropertyConstraints::isRequiredValueMissing( const PropertyInfo& prop, const bool bWritten, const string_view* pWrittenText, const RequiredScope scope )
    {
        const bool bRequiredHere = prop._metadata->_bRequired == SW_TRUE || ( prop._metadata->_bRequiredAtRoot == SW_TRUE && scope == RequiredScope::Root );
        if ( bRequiredHere == false )
            return false;
        if ( bWritten == false )
            return true;
        if ( pWrittenText == nullptr || prop._bIsContainer != SW_FALSE || isNameValueType( prop._typeName ) == false )
            return false;
        // 이름 형식은 "None" 도 빈 이름이다(FName 의 None — 읽으면 빈 이름이 된다). 글만 보면 적힌 것으로 보여 데이터의 빈 필수 칸을 놓친다.
        return pWrittenText->empty() || hashed_string( *pWrittenText ).empty();
    }

    void PropertyConstraints::collectRangeErrors( const TypeInfo& typeInfo, const ObjectAddress instance, const string& prefix, vector<string>& outListError,
                                                  const vector<RestoredPropertyValue>* pListRestored )
    {
        for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
        {
            collectPropertyRangeErrors( prop, instance, prefix, outListError, pListRestored );
        }
    }

    void PropertyConstraints::collectPropertyRangeErrors( const PropertyInfo& prop, const ObjectAddress instance, const string& prefix, vector<string>& outListError,
                                                          const vector<RestoredPropertyValue>* pListRestored )
    {
        using Internal                  = PropertyConstraintsInternal;
        const void* const pInstance     = instance.get();
        const bool        bSkipProperty = prop._bIsBitField == SW_TRUE || prop.hasValueAccessor();
        if ( bSkipProperty )
            return;
        const SerializeContext&                  context = SerializeContext::getDefault( SerializePurpose::Authoring );
        StringBuilder<constant::buffer::kMax256> pathBuilder;
        if ( prefix.empty() == false )
            pathBuilder.appendFormat( "%#.", prefix );
        pathBuilder.append( prop._name.c_str() );
        const string      path   = string( pathBuilder.view() );
        const void* const pValue = prop.getRawPtr( pInstance );
        // 소프트 읽기가 읽기 전 값으로 되돌린 칸은 파일 값이 아니다 — orphan 이 이미 알렸다.
        if ( pListRestored != nullptr && VectorUtil::contains( *pListRestored, RestoredPropertyValue{ pValue, &prop } ) )
            return;
        if ( prop._bIsContainer == SW_TRUE )
        {
            // 시퀀스 원소만 본다. 맵과 중첩 컨테이너는 원소 경로를 하나로 정할 수 없어 보지 않는다.
            const bool bNestedContainer = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
            const bool bSequence        = prop._containerWrapper != nullptr && prop._containerWrapper->asSequence() != nullptr && bNestedContainer == false;
            if ( bSequence == false )
                return;
            const ISequenceContainerWrapper* pSequence      = prop._containerWrapper->asSequence();
            const TypeInfo*                  pElement       = ValueSerializer::findNestedObjectType( prop._elementTypeName, prop.findElementBinding(), context );
            const ReflectBuiltinIndex        elementBuiltin = prop.resolveElementFacts( context.getTypeRegistry() )._builtinIndex;
            const size_t                     elementCount   = pSequence->getSize( pValue );
            // 필드 경계는 원소가 아니라 이 프로퍼티를 가진 객체에서 읽는다(인스펙터와 같다).
            const PropertyAllowedRange range = prop._metadata->hasAllowedRange() ? prop._metadata->computeAllowedRange( pInstance ) : PropertyAllowedRange{};
            for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
            {
                StringBuilder<constant::buffer::kMax256> elementPathBuilder;
                elementPathBuilder.appendFormat( "%#[%#]", path, elementIndex );
                const string      elementPath{ elementPathBuilder.view() };
                const void* const pElementValue = pSequence->getElementConst( pValue, elementIndex );
                float64           value         = 0.0;
                // 구조체 원소의 값 주소는 그 원소 타입의 객체 주소다.
                if ( pElement != nullptr )
                    collectRangeErrors( *pElement, ObjectAddress{ pElementValue }, elementPath, outListError, pListRestored );
                else if ( Internal::readRangeNumber( elementBuiltin, ValueAddress{ pElementValue }, value ) )
                    Internal::appendRangeError( elementPath, value, range, outListError );
            }
            return;
        }
        const TypeInfo* pNested = ValueSerializer::findNestedObjectType( prop._typeName, prop.findValueBinding(), context );
        if ( pNested != nullptr )
        {
            collectRangeErrors( *pNested, ObjectAddress{ pValue }, path, outListError, pListRestored );
            return;
        }
        float64 value = 0.0;
        if ( prop._metadata->hasAllowedRange() && Internal::readRangeNumber( prop.resolveValueFacts( context.getTypeRegistry() )._builtinIndex, ValueAddress{ pValue }, value ) )
            Internal::appendRangeError( path, value, prop._metadata->computeAllowedRange( pInstance ), outListError );
    }

    bool PropertyConstraints::isWithinDeclaredRanges( const TypeInfo& typeInfo, const ObjectAddress instance, const vector<RestoredPropertyValue>& listRestored )
    {
        if ( listRestored.empty() )
            return isWithinDeclaredRanges( typeInfo, instance );
        // 되돌린 칸이 있는 실패 길이라 빠른 길 없이 칸마다 모은다.
        vector<string> listRangeError;
        collectRangeErrors( typeInfo, instance, string(), listRangeError, &listRestored );
        for ( const string& rangeError : listRangeError )
        {
            SW_LOG_ERROR_ID( "Serialization.ValueOutOfRange", "%#: %# - the read fails", typeInfo._fullyQualifiedName.c_str(), rangeError.c_str() );
        }
        return listRangeError.empty();
    }

    bool PropertyConstraints::areValuesWithinDeclaredRanges( const TypeInfo& typeInfo, const ObjectAddress instance )
    {
        return PropertyConstraintsInternal::isEveryValueWithinRange( typeInfo, instance );
    }

    bool PropertyConstraints::isWithinDeclaredRanges( const TypeInfo& typeInfo, const ObjectAddress instance )
    {
        if ( PropertyConstraintsInternal::isEveryValueWithinRange( typeInfo, instance ) )
            return true;
        vector<string> listRangeError;
        collectRangeErrors( typeInfo, instance, string(), listRangeError );
        for ( const string& rangeError : listRangeError )
        {
            SW_LOG_ERROR_ID( "Serialization.ValueOutOfRange", "%#: %# - the read fails", typeInfo._fullyQualifiedName.c_str(), rangeError.c_str() );
        }
        return false;
    }
} // namespace sw
