#include "pch.h"

#include "Engine/Serialization/Base/SerializerUtil.h"

#include "Core/Container/InlineAllocator.h"
#include "Core/Container/StringUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Base/ContainerVisitor.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    bool runVersionedDeserialize( uint32& outVersion, const MutableObjectAddress instance, const TypeInfo& typeInfo,
                                  uint32          currentVersion,
                                  const TypeInfo* pLegacyTypeInfo, const SerializeContext& context,
                                  SchemaVersionSource versionSource, SchemaOrphanPolicy orphanPolicy,
                                  const SoftDeserializeFn& softDeserialize )
    {
        if ( softDeserialize.isBound() == false )
            return false;

        vector<SchemaOrphanValue> listOrphan;
        ScopedScratchInstance     scratchLegacy( pLegacyTypeInfo );
        void* const               pLegacyPtr = scratchLegacy.get();

        if ( versionSource == SchemaVersionSource::Payload )
            outVersion = 0;

        // 옛 TypeInfo 가 주어지면 그쪽으로도 한 벌 읽어 둔다. migrate 가 옛 필드를 그대로 볼 수 있게 하려는 것이다.
        if ( pLegacyTypeInfo != nullptr && pLegacyTypeInfo->_size > 0 )
        {
            uint32 legacyVersion{ 0 };
            if ( pLegacyPtr == nullptr || softDeserialize( MutableObjectAddress{ pLegacyPtr }, *pLegacyTypeInfo, listOrphan, legacyVersion ) == false )
                return false;
            if ( versionSource == SchemaVersionSource::Payload )
                outVersion = legacyVersion;
        }

        uint32 softVersion{ 0 };
        if ( softDeserialize( instance, typeInfo, listOrphan, softVersion ) == false )
            return false;

        // 레거시를 읽지 않았거나(그러면 이쪽이 유일한 출처다) 이번에 실제로 버전이 나왔으면 그것을 쓴다.
        if ( versionSource == SchemaVersionSource::Payload && ( pLegacyPtr == nullptr || softVersion != 0 ) )
            outVersion = softVersion;

        // 포맷마다 갈리는 한 줄이다. 텍스트는 orphan 을 버리고 통과시키고, 바이너리는 이관 함수 없이는 거절한다(`SchemaOrphanPolicy` 의 계약).
        const bool bOrphanBlocks = ( orphanPolicy == SchemaOrphanPolicy::Reject ) && ( listOrphan.empty() == false );
        const bool bNeedsMigrate = ( outVersion != currentVersion ) || bOrphanBlocks;

        return runSchemaMigrateStep( outVersion, currentVersion, instance.get(), typeInfo, pLegacyPtr, pLegacyTypeInfo,
                                     listOrphan, bNeedsMigrate, context );
    }

    namespace
    {
        struct SerializerUtilInternal
        {
            /** @brief 숫자 칸 하나를 float64 로 읽습니다. 숫자 타입이 아니면 false 입니다(범위 검사가 보지 않는 칸). */
            [[nodiscard]] static bool readRangeNumber( const string_view typeName, const ValueAddress value, float64& outValue )
            {
                const void* const pValue = value.get();
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

            /** @brief @p value 가 @p range 밖이면 "경로 = 값 is outside [아래, 위]" 한 줄을 더합니다. */
            static void appendRangeError( const string& path, const float64 value, const PropertyAllowedRange& range, vector<string>& outListError )
            {
                if ( range.contains( value ) )
                    return;
                const string lower = range._bHasMin ? to_string( range._min ) : string( "-" );
                const string upper = range._bHasMax ? to_string( range._max ) : string( "-" );
                outListError.push_back( path + " = " + to_string( value ) + " is outside [" + lower + ", " + upper + "]" );
            }

            /** @brief @p value(@p typeName 의 숫자)가 @p range 안인지입니다. 숫자가 아니면 범위가 없으므로 true 입니다. */
            static bool isNumberWithinRange( const string_view typeName, const ValueAddress value, const PropertyAllowedRange& range )
            {
                float64 number = 0.0;
                if ( readRangeNumber( typeName, value, number ) == false )
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
                const void* const pInstance = instance.get();
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
                        const TypeInfo* pElement     = SerializerUtil::findNestedObjectType( prop._elementTypeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
                        const bool      bNumberRange = pElement == nullptr && prop._metadata.hasAllowedRange();
                        if ( pElement == nullptr && bNumberRange == false )
                            continue;
                        // 필드 경계는 원소가 아니라 이 프로퍼티를 가진 객체에서 읽는다(인스펙터와 같다).
                        const PropertyAllowedRange range        = bNumberRange ? prop._metadata.computeAllowedRange( pInstance ) : PropertyAllowedRange{};
                        ISequenceContainerWrapper* pSequence    = prop._containerWrapper->asSequence();
                        const size_t               elementCount = pSequence->getSize( pValue );
                        for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                        {
                            // 구조체 원소의 값 주소는 그 원소 타입의 객체 주소다.
                            const void* const pElementValue = pSequence->getElementConst( pValue, elementIndex );
                            const bool        bWithin       = pElement != nullptr ? isEveryValueWithinRange( *pElement, ObjectAddress{ pElementValue } )
                                                                                  : isNumberWithinRange( prop._elementTypeName.view(), ValueAddress{ pElementValue }, range );
                            if ( bWithin == false )
                                return false;
                        }
                        continue;
                    }
                    // 숫자 칸은 범위를 선언했을 때만 본다. 숫자가 아닌 칸만 중첩 구조체인지 찾는다(숫자 칸마다 타입 조회를 하지 않는다).
                    float64 value = 0.0;
                    if ( readRangeNumber( prop._typeName.view(), ValueAddress{ pValue }, value ) )
                    {
                        if ( prop._metadata.hasAllowedRange() && prop._metadata.computeAllowedRange( pInstance ).contains( value ) == false )
                            return false;
                        continue;
                    }
                    const TypeInfo* pNested = SerializerUtil::findNestedObjectType( prop._typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
                    if ( pNested != nullptr && isEveryValueWithinRange( *pNested, ObjectAddress{ pValue } ) == false )
                        return false;
                }
                return true;
            }

            /** @brief uint32 하나를 리틀 엔디언 그대로 덧붙입니다. */
            static void appendUint32( vector<uint8>& buffer, uint32 value )
            {
                const uint8* pBytes = reinterpret_cast<const uint8*>( &value );
                buffer.insert( buffer.end(), pBytes, pBytes + sizeof( uint32 ) );
            }

            /** @brief uint32 하나를 읽고 오프셋을 그만큼 옮깁니다. */
            [[nodiscard]] static bool readUint32( const uint8* pData, size_t dataSize, size_t& inoutOffset, uint32& outValue )
            {
                if ( inoutOffset + sizeof( uint32 ) > dataSize )
                    return false;
                Memory::copy( &outValue, pData + inoutOffset, sizeof( uint32 ) );
                inoutOffset += sizeof( uint32 );
                return true;
            }

            /** @brief int64 하나를 리틀 엔디언 그대로 덧붙입니다. */
            static void appendInt64( vector<uint8>& buffer, int64 value )
            {
                const uint8* pBytes = reinterpret_cast<const uint8*>( &value );
                buffer.insert( buffer.end(), pBytes, pBytes + sizeof( int64 ) );
            }

            /** @brief int64 하나를 읽고 오프셋을 그만큼 옮깁니다. */
            [[nodiscard]] static bool readInt64( const uint8* pData, size_t dataSize, size_t& inoutOffset, int64& outValue )
            {
                if ( inoutOffset + sizeof( int64 ) > dataSize )
                    return false;
                Memory::copy( &outValue, pData + inoutOffset, sizeof( int64 ) );
                inoutOffset += sizeof( int64 );
                return true;
            }

            /**
             * @brief 열거자 항목 하나를 적습니다. 이름 해시를 적고, 이름이 없으면(해시 0) 그 뒤에 int64 값을 이어 적습니다.
             * @details 값으로 싣는 것은 이름이 없는 값과 비트뿐이다(이름이 없으니 정체성도 값 말고는 없다).
             */
            static void appendEnumEntry( vector<uint8>& buffer, uint32 nameHash, int64 unnamedValue )
            {
                appendUint32( buffer, nameHash );
                if ( nameHash == 0 )
                    appendInt64( buffer, unnamedValue );
            }

            /**
             * @brief enum 값 하나를 열거자 정체성으로 적습니다(`SerializerUtil::serializeValueBinary` 설명의 형식).
             * @details 비트플래그의 이름은 텍스트(`toStringFlags`)와 같은 규칙으로 모으고(`EnumInfo::collectFlagNames`) 해시 오름차순으로 적는다. 같은 값은
             *          같은 바이트라 바이트 비교(`arePropertyValuesEqual`, diff)가 맞는다. 이름의 해시가 0 이면(사실상 없다) 값 전체를 값으로 싣는다.
             */
            static void writeEnumBinary( const EnumInfo& enumInfo, const ValueAddress address, vector<uint8>& buffer )
            {
                const int64 value = enumInfo.readValueFromMemory( address.get() );
                if ( enumInfo._bIsBitFlag == SW_FALSE )
                {
                    appendEnumEntry( buffer, enumInfo.toString( value ).getHash(), value );
                    return;
                }

                vector<hashed_string> listName;
                const int64           unnamedBits = enumInfo.collectFlagNames( value, listName );
                vector<uint32>        listNameHash;
                listNameHash.reserve( listName.size() );
                bool bHashless = false;
                for ( const hashed_string& name : listName )
                {
                    listNameHash.push_back( name.getHash() );
                    bHashless = bHashless || name.getHash() == 0;
                }
                if ( bHashless )
                {
                    appendUint32( buffer, 1 );
                    appendEnumEntry( buffer, 0, value );
                    return;
                }
                std::sort( listNameHash.begin(), listNameHash.end() );
                appendUint32( buffer, static_cast<uint32>( listNameHash.size() + ( unnamedBits != 0 ? 1 : 0 ) ) );
                for ( const uint32 nameHash : listNameHash )
                {
                    appendEnumEntry( buffer, nameHash, 0 );
                }
                if ( unnamedBits != 0 )
                    appendEnumEntry( buffer, 0, unnamedBits );
            }

            /** @brief 모르는 열거자 이름을 알립니다. enum 과 이름마다 한 번만 알립니다(같은 세이브의 오브젝트 천 개가 천 줄을 쓰지 않게 하려는 것입니다). */
            static void warnUnknownEnumeratorOnce( const EnumInfo& enumInfo, uint32 nameHash )
            {
                // "한 번만"의 상태는 범위(테스트 케이스 · 프로세스)에 속한다(`LogOnce`). 함수 안 정적 집합이면 테스트 순서가 로그 판정을 바꾼다.
                const uint64 key = ( static_cast<uint64>( enumInfo._fullyQualifiedName.getHash() ) << 32 ) | nameHash;
                if ( LogOnce::tryClaim( LogOnceKind::UnknownEnumerator, key ) == false )
                    return;
                SW_LOG_WARNING( "%#: a saved enumerator (name hash %#) is not a value of it any more - the field keeps its current value (once per enumerator)",
                                enumInfo._fullyQualifiedName.c_str(), nameHash );
            }

            /** @brief 열거자 항목 하나를 읽습니다. 이름을 모르면 `outKnown` 이 false 이고 바이트는 읽었습니다. 바이트가 모자라면 false 입니다. */
            [[nodiscard]] static bool readEnumEntry( const EnumInfo& enumInfo, const uint8* pData, size_t dataSize, size_t& inoutOffset, int64& outValue, bool& outKnown )
            {
                uint32 nameHash{ 0 };
                if ( readUint32( pData, dataSize, inoutOffset, nameHash ) == false )
                    return false;
                outKnown = true;
                if ( nameHash == 0 )
                    return readInt64( pData, dataSize, inoutOffset, outValue );
                outKnown = enumInfo.findValueByNameHash( nameHash, outValue );
                if ( outKnown == false )
                    warnUnknownEnumeratorOnce( enumInfo, nameHash );
                return true;
            }

            /**
             * @brief enum 값 하나를 열거자 정체성(이름 해시)으로 읽습니다.
             * @return 바이트가 모자라면 false 입니다. 모르는 열거자 이름이면 **값은 그대로 두고** false 입니다(바이트는 끝까지 읽었으며, XML 의 모르는 이름과 같은 필드
             *         실패입니다).
             */
            [[nodiscard]] static bool readEnumBinary( const EnumInfo& enumInfo, const MutableValueAddress address, const uint8* pData, size_t dataSize, size_t& inoutOffset,
                                                      BinaryWireVersion wireVersion )
            {
                (void)wireVersion; // 현재 읽는 버전은 하나뿐이다(`BinaryWireVersion`). 버전이 늘면 여기서 분기한다
                int64 value{ 0 };

                bool bKnown = true;
                if ( enumInfo._bIsBitFlag == SW_FALSE )
                {
                    if ( readEnumEntry( enumInfo, pData, dataSize, inoutOffset, value, bKnown ) == false )
                        return false;
                }
                else
                {
                    uint32 entryCount{ 0 };
                    if ( readUint32( pData, dataSize, inoutOffset, entryCount ) == false )
                        return false;
                    // 항목은 적어도 4 바이트다. 남은 바이트로 담을 수 없는 수는 망가진 스트림이다.
                    if ( entryCount > ( dataSize - inoutOffset ) / sizeof( uint32 ) )
                        return false;
                    for ( uint32 entryIndex = 0; entryIndex < entryCount; ++entryIndex )
                    {
                        int64 entryValue{ 0 };
                        bool  bEntryKnown = true;
                        if ( readEnumEntry( enumInfo, pData, dataSize, inoutOffset, entryValue, bEntryKnown ) == false )
                            return false;
                        value |= entryValue;
                        bKnown = bKnown && bEntryKnown;
                    }
                }
                if ( bKnown == false )
                    return false;
                enumInfo.writeValueToMemory( address.get(), value );
                return true;
            }

            /**
             * @brief uint32 길이 머리와 그 길이만큼의 본문을 건너뜁니다. 본문 시작은 `outBlockStart`, 길이는 `outBlockSize` 이고 오프셋은 본문 뒤로 옮깁니다.
             * @return 머리나 본문이 버퍼를 넘으면 false 입니다. 구조체와 텍스트 리더 값의 바이너리 읽기가 함께 씁니다.
             */
            [[nodiscard]] static bool readSizedBlock( const uint8* pData, size_t dataSize, size_t& inoutOffset, size_t& outBlockStart, uint32& outBlockSize )
            {
                if ( readUint32( pData, dataSize, inoutOffset, outBlockSize ) == false )
                    return false;
                // 뺄셈으로 비교한다. 스트림에서 읽은 크기가 크면 덧셈이 넘친다.
                if ( outBlockSize > dataSize - inoutOffset )
                    return false;
                outBlockStart = inoutOffset;
                inoutOffset += outBlockSize;
                return true;
            }

            /**
             * @brief 다형 소유 포인터 원소 하나를 `[이름][본문크기][본문]` 으로 적습니다.
             *
             * XML, JSON 은 태그와 키 이름이 곧 런타임 타입이라 따로 적을 위치가 필요 없지만, 바이너리에는
             * 그런 위치가 없어 이름을 값으로 싣습니다. **본문 크기를 같이 적는 이유는 모르는 타입을
             * 건너뛰기 위해서입니다.** 없으면 낯선 컴포넌트 하나가 그 뒤 스트림을 통째로 어긋나게 합니다.
             * 빈 이름은 빈 슬롯을 뜻합니다(원소 개수를 앞에서 이미 적었으므로 슬롯은 남겨야 합니다).
             */
            [[nodiscard]] static bool writeOwnedPointerBinary( const ObjectAddress object, vector<uint8>& buffer, const SerializeContext& context )
            {
                // 주소는 소유 포인터가 가리키는 객체다(널이면 빈 슬롯).
                const void* const pObject = object.get();
                // 보관해 둔 원소(모르는 타입)는 읽은 이름과 본문 그대로 다시 쓴다.
                SerializeContext::OpaqueElementView opaque{};
                if ( context.queryOpaqueElement( pObject, opaque ) && opaque._format == SerializeContext::OpaqueFormat::Binary )
                {
                    appendUint32( buffer, static_cast<uint32>( opaque._typeName.size() ) );
                    const auto* pNameBytes = reinterpret_cast<const uint8*>( opaque._typeName.data() );
                    buffer.insert( buffer.end(), pNameBytes, pNameBytes + opaque._typeName.size() );
                    appendUint32( buffer, static_cast<uint32>( opaque._byteCount ) );
                    if ( opaque._pBytes != nullptr && opaque._byteCount > 0 )
                        buffer.insert( buffer.end(), opaque._pBytes, opaque._pBytes + opaque._byteCount );
                    return true;
                }
                const TypeInfo* pRuntimeType = ( pObject != nullptr ) ? context.getRuntimeTypeInfo( pObject ) : nullptr;

                if ( pRuntimeType == nullptr )
                {
                    appendUint32( buffer, 0 ); // 이름 길이 0 은 빈 슬롯을 뜻한다
                    appendUint32( buffer, 0 ); // 본문 길이 0
                    return true;
                }

                const utf8*  pName   = pRuntimeType->_name.c_str();
                const uint32 nameLen = ( pName != nullptr ) ? static_cast<uint32>( pRuntimeType->_name.size() ) : 0;
                appendUint32( buffer, nameLen );
                if ( nameLen > 0 )
                {
                    const auto* pNameBytes = reinterpret_cast<const uint8*>( pName );
                    buffer.insert( buffer.end(), pNameBytes, pNameBytes + nameLen );
                }

                const size_t sizePos = buffer.size();
                appendUint32( buffer, 0 );

                const size_t bodyStart = buffer.size();
                const bool   bWritten  = BinarySerializer::serialize( pObject, *pRuntimeType, buffer, context );
                const uint32 bodySize  = static_cast<uint32>( buffer.size() - bodyStart );
                Memory::copy( buffer.data() + sizePos, &bodySize, sizeof( uint32 ) );
                return bWritten;
            }

            /**
             * @brief `writeOwnedPointerBinary` 가 적은 원소 하나를 되읽습니다.
             *
             * 컨테이너에 넣는 것은 **팩토리가** 합니다(`createOwnedPointer` 가 소유자에 붙입니다).
             * XML, JSON 도 같은 약속이라 여기서 `appendElement` 를 부르지 않습니다.
             */
            [[nodiscard]] static bool readOwnedPointerBinary( const uint8* pData, size_t dataSize, size_t& inoutOffset, const SerializeContext& context )
            {
                uint32 nameLen{ 0 };
                if ( readUint32( pData, dataSize, inoutOffset, nameLen ) == false )
                    return false;
                if ( inoutOffset + nameLen > dataSize )
                    return false;

                // 스트림 위에서 그대로 intern 한다. 이름 하나 읽자고 string 을 만들지 않는다.
                const string_view typeNameText( reinterpret_cast<const utf8*>( pData + inoutOffset ), nameLen );
                inoutOffset += nameLen;

                uint32 bodySize{ 0 };
                if ( readUint32( pData, dataSize, inoutOffset, bodySize ) == false )
                    return false;
                if ( inoutOffset + bodySize > dataSize )
                    return false;

                const size_t bodyStart = inoutOffset;
                inoutOffset += bodySize; // 어떤 경우로 빠지든 다음 원소 위치는 여기다.

                if ( nameLen == 0 )
                    return true;

                // 찾기만 한다. 파일의 이름을 intern 하면 전역 테이블이 파일 크기만큼 늘어난다(`hashed_string::findInterned`).
                const hashed_string typeName = hashed_string::findInterned( typeNameText );
                const TypeInfo*     pType    = typeName.empty() ? nullptr : context.getTypeRegistry().findType( typeName );
                void*               pObj     = ( pType != nullptr ) ? context.createOwnedPointer( typeName ) : nullptr;
                if ( pObj == nullptr )
                {
                    // 모르는(만들 수 없는) 타입이다. 이름과 본문을 보관한다(다음 저장이 그대로 다시 쓴다). 보관할 곳이 없으면 건너뛴다(위에서 이미 그만큼 밀어 두었다).
                    SerializeContext::OpaqueElementView opaque{};
                    opaque._typeName  = typeNameText;
                    opaque._format    = SerializeContext::OpaqueFormat::Binary;
                    opaque._pBytes    = pData + bodyStart;
                    opaque._byteCount = bodySize;
                    (void)context.keepOpaqueElement( opaque );
                    return true;
                }

                return BinarySerializer::deserialize( pObj, *pType, pData + bodyStart, bodySize, context );
            }

            /**
             * @brief 텍스트(JSON, XML)를 임시 인스턴스로 읽어 바이너리로 다시 씁니다. `deserializeText` 가 텍스트 형식을 고릅니다.
             * @return 텍스트를 읽지 못하면 false 이고, 그때 `outBinary` 는 건드리지 않습니다.
             */
            template <typename DeserializeTextFunc>
            static bool transcodeTextToBinary( string_view text, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                               const SerializeContext& context, DeserializeTextFunc&& deserializeText )
            {
                if ( text.empty() || typeInfo._size == 0 )
                    return false;

                ScopedScratchInstance scratch( typeInfo );
                if ( scratch.isValid() == false || deserializeText( scratch.get() ) == false )
                    return false;

                return BinarySerializer::serialize( scratch.get(), typeInfo, outBinary, context );
            }

            /**
             * @brief 바이너리를 임시 인스턴스로 읽어 텍스트로 다시 씁니다. `serializeText` 가 텍스트 형식을 고릅니다.
             * @return 바이너리를 읽지 못하면 빈 문자열입니다.
             */
            template <typename SerializeTextFunc>
            static string transcodeBinaryToText( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, const SerializeContext& context,
                                                 SerializeTextFunc&& serializeText )
            {
                const bool bEmptyTranscodeInput = pData == nullptr || dataSize == 0 || typeInfo._size == 0;
                if ( bEmptyTranscodeInput )
                    return {};

                ScopedScratchInstance scratch( typeInfo );
                if ( scratch.isValid() == false || BinarySerializer::deserialize( scratch.get(), typeInfo, pData, dataSize, context ) == false )
                    return {};

                return serializeText( scratch.get() );
            }

            /** @brief 컨테이너를 바이너리로 적습니다. 원소 수(uint32) 뒤에 원소를 적고, 맵은 항목마다 키와 값을 적습니다. 소유 포인터 원소는 `[이름][본문크기][본문]` 입니다. */
            class ContainerWriter final : public IContainerWriter
            {
            public:
                ContainerWriter( vector<uint8>& buffer, const SerializeContext& context )
                    : _listBuffer{ buffer }
                    , _context{ context }
                    , _bAllWritten{ true }
                {
                }

                void beginSequence( const size_t elementCount ) override { appendUint32( _listBuffer, static_cast<uint32>( elementCount ) ); }
                void endSequence() override {}
                void beginMap( const size_t entryCount ) override { appendUint32( _listBuffer, static_cast<uint32>( entryCount ) ); }
                void endMap() override {}
                void beginMapEntry( const ValueAddress key, const hashed_string& keyTypeName ) override
                {
                    recordWritten( SerializerUtil::serializeValueBinary( key, keyTypeName, _listBuffer, _context ) );
                }
                void endMapEntry() override {}
                void beginNestedContainer( ContainerSlot ) override {}
                void endNestedContainer( ContainerSlot ) override {}
                void writeOwnedPointer( const ObjectAddress object ) override { recordWritten( writeOwnedPointerBinary( object, _listBuffer, _context ) ); }

                // 값 구조체도 값 하나를 적는 경로로 적는다. 등록된 바이너리 핸들러가 구조체보다 먼저다(`serializeValueBinary` 의 순서).
                void writeValueObject( const ValueAddress value, const hashed_string& typeName, const TypeInfo&, ContainerSlot ) override
                {
                    recordWritten( SerializerUtil::serializeValueBinary( value, typeName, _listBuffer, _context ) );
                }

                void writeScalar( const ValueAddress value, const hashed_string& typeName, ContainerSlot ) override
                {
                    recordWritten( SerializerUtil::serializeValueBinary( value, typeName, _listBuffer, _context ) );
                }

                /** @brief 원소 · 키 · 값을 하나도 빠짐없이 썼는가입니다. 방문자의 콜백은 결과를 돌려주지 못하므로 여기에 모읍니다. */
                bool isAllWritten() const { return _bAllWritten; }

            private:
                void recordWritten( const bool bWritten )
                {
                    if ( bWritten == false )
                        _bAllWritten = false;
                }

                vector<uint8>&          _listBuffer;
                const SerializeContext& _context;
                bool                    _bAllWritten;
            };

            /** @brief 바이너리 컨테이너를 읽습니다. 신뢰할 수 없는 바이트의 경계 검사가 이 안에 있습니다(원소 수, 값, 소유 포인터 본문). */
            class ContainerReader final : public IContainerReader
            {
            public:
                ContainerReader( const uint8* pData, const size_t dataSize, size_t* pInOutOffset, const SerializeContext& context, const BinaryWireVersion wireVersion )
                    : _listOpenCount{}
                    , _pData{ pData }
                    , _dataSize{ dataSize }
                    , _pInOutOffset{ pInOutOffset }
                    , _context{ context }
                    , _wireVersion{ wireVersion }
                {
                }

                ContainerReadResult beginSequence( size_t& outCountHint ) override { return beginCounted( outCountHint ); }
                ContainerReadResult beginMap( size_t& outCountHint ) override { return beginCounted( outCountHint ); }

                ContainerReadResult forEachElement( const ContainerElementVisitDelegate& visit ) override
                {
                    // 원소가 또 컨테이너면 그 안에서 begin* 이 다시 쌓으므로, 이 컨테이너의 수는 돌기 전에 꺼낸다.
                    const uint32 elementCount = _listOpenCount.back();
                    _listOpenCount.pop_back();
                    ContainerReadResult total{ ContainerReadResult::Read };
                    for ( uint32 elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                    {
                        total = ContainerVisitor::mergeResult( total, visit( elementIndex ) );
                        if ( ContainerVisitor::stopsReading( total ) )
                            break;
                    }
                    return total;
                }

                ContainerReadResult readMapKey( const MutableValueAddress key, const hashed_string& keyTypeName ) override { return readValue( key, keyTypeName ); }

                // 본문 크기가 있어 모르는 타입은 건너뛰지만, 아는 타입의 본문을 못 읽으면 스트림이 망가진 것이다.
                ContainerReadResult readOwnedPointer() override
                {
                    return readOwnedPointerBinary( _pData, _dataSize, *_pInOutOffset, _context ) ? ContainerReadResult::Read : ContainerReadResult::StreamBroken;
                }

                ContainerReadResult readValueObject( const MutableValueAddress value, const hashed_string& typeName, const TypeInfo&, ContainerSlot ) override { return readValue( value, typeName ); }
                ContainerReadResult readScalar( const MutableValueAddress value, const hashed_string& typeName, ContainerSlot ) override { return readValue( value, typeName ); }

                // 바이너리는 원소 크기를 적지 않는다. 넣을 슬롯이 없는 원소는 지나갈 수 없다.
                ContainerReadResult skipElement( size_t ) override { return ContainerReadResult::StreamBroken; }

            private:
                ContainerReadResult beginCounted( size_t& outCountHint )
                {
                    uint32 count{ 0 };
                    if ( readUint32( _pData, _dataSize, *_pInOutOffset, count ) == false )
                        return ContainerReadResult::StreamBroken;
                    // 원소마다 적어도 한 바이트다. 남은 바이트보다 많은 원소 수는 거짓이다(손상된 개수 필드 하나로 큰 할당을 하지 않는다).
                    if ( _dataSize - *_pInOutOffset < count )
                        return ContainerReadResult::StreamBroken;
                    _listOpenCount.push_back( count );
                    outCountHint = count;
                    return ContainerReadResult::Read;
                }

                /** @brief 값 하나를 읽습니다. 모르는 열거자는 바이트를 끝까지 읽고 실패하므로 그 원소만 빼고(FieldFailed), 그 밖의 실패는 위치를 알 수 없습니다. */
                ContainerReadResult readValue( const MutableValueAddress value, const hashed_string& typeName ) const
                {
                    const size_t valueStart = *_pInOutOffset;
                    if ( SerializerUtil::deserializeValueBinary( value, typeName, _pData, _dataSize, *_pInOutOffset, _context, _wireVersion ) )
                        return ContainerReadResult::Read;
                    const bool bAdvanced  = valueStart < *_pInOutOffset && *_pInOutOffset <= _dataSize;
                    const bool bSkippable = bAdvanced && _context.getTypeRegistry().findEnum( typeName ) != nullptr;
                    return bSkippable ? ContainerReadResult::FieldFailed : ContainerReadResult::StreamBroken;
                }

                vector<uint32, InlineAllocator<uint32, 4>> _listOpenCount;
                const uint8*                               _pData;
                size_t                                     _dataSize;
                size_t*                                    _pInOutOffset;
                const SerializeContext&                    _context;
                BinaryWireVersion                          _wireVersion;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SerializerUtil::reportForbiddenProperty( const TypeInfo& owner, const PropertyInfo& prop )
    {
        SW_LOG_ERROR( "%#::%# is PROPERTY( RuntimeState ) but the authoring file writes it - the read fails. Rewrite the file without the field "
                      "(authoring files never hold runtime state).",
                      owner._fullyQualifiedName.c_str(), prop._name.c_str() );
    }

    const TypeInfo* SerializerUtil::findNestedObjectType( hashed_string typeName, const SerializeContext& context )
    {
        if ( context.findTextWriter( typeName ) != nullptr )
            return nullptr;
        if ( context.getTypeRegistry().findEnum( typeName ) != nullptr )
            return nullptr;
        const TypeInfo* pTypeInfo = context.getTypeRegistry().findType( typeName );
        if ( pTypeInfo == nullptr || pTypeInfo->isPrimitive() )
            return nullptr;
        return pTypeInfo;
    }

    hashed_string SerializerUtil::resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& context )
    {
        // 등록된 이름 그대로 핸들러가 있으면 그것이 원본이다.
        if ( context.findBinaryWriter( typeName ) != nullptr || context.findTextWriter( typeName ) != nullptr )
            return typeName;

        // 없으면 리플렉션이 아는 원본 이름(`_name`)으로 한 번 더 물어본다. Alias 나 옛 이름으로 들어온 경우다.
        const TypeRegistry& registry         = context.getTypeRegistry();
        const TypeInfo*     pTypeInfo        = registry.findType( typeName );
        const bool          bHasCustomWriter = pTypeInfo != nullptr && pTypeInfo->_name.empty() == false &&
                                      ( context.findBinaryWriter( pTypeInfo->_name ) != nullptr || context.findTextWriter( pTypeInfo->_name ) != nullptr );
        if ( bHasCustomWriter )
            return pTypeInfo->_name;

        return typeName;
    }

    bool SerializerUtil::isOwnedPointerElementType( hashed_string elementTypeName )
    {
        // 판정 기준은 이름에 `*` 가 있는지 하나다. 리플렉션이 포인터 원소를 그렇게 적는다.
        const utf8* pName = elementTypeName.c_str();
        if ( pName == nullptr )
            return false;

        for ( const utf8* pCursor = pName; *pCursor != 0; ++pCursor )
        {
            if ( *pCursor == '*' )
                return true;
        }
        return false;
    }

    bool SerializerUtil::hasBinaryValueCodec( const hashed_string& typeName, const SerializeContext& context )
    {
        // `serializeValueBinary` · `deserializeValueBinary` 가 고르는 순서와 같다. 바이너리 처리기, 열거형, 구조체, 텍스트 처리기다.
        const hashed_string resolved = SerializerUtil::resolveHandlerTypeName( typeName, context );
        if ( context.findBinaryWriter( resolved ) != nullptr && context.findBinaryReader( resolved ) != nullptr )
            return true;
        if ( context.getTypeRegistry().findEnum( typeName ) != nullptr )
            return true;
        const TypeInfo* pStructInfo = context.getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr && pStructInfo->isPrimitive() == false )
            return true;
        return context.findTextWriter( resolved ) != nullptr && context.findTextReader( resolved ) != nullptr;
    }

    bool SerializerUtil::serializeValueBinary( ValueAddress value, const hashed_string& typeName,
                                               vector<uint8>& listBuffer, const SerializeContext& context )
    {
        const void* const                      pValuePtr = value.get();
        const hashed_string                    resolved  = SerializerUtil::resolveHandlerTypeName( typeName, context );
        const SerializeContext::BinaryWriteFn* pWriter   = context.findBinaryWriter( resolved );
        if ( pWriter != nullptr )
        {
            ( *pWriter )( pValuePtr, listBuffer );
            return true;
        }

        const EnumInfo* pEnumInfo = context.getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
        {
            // 값이 아니라 열거자 정체성(이름 해시)으로 싣는다. 값으로 실으면 열거자 순서가 바뀔 때 세이브와 스냅샷의 뜻이 바뀐다.
            SerializerUtilInternal::writeEnumBinary( *pEnumInfo, value, listBuffer );
            return true;
        }

        const TypeInfo* pStructInfo = context.getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                const size_t sizePos   = listBuffer.size();
                const uint32 dummySize = 0;
                const uint8* pDummy    = reinterpret_cast<const uint8*>( &dummySize );
                listBuffer.insert( listBuffer.end(), pDummy, pDummy + sizeof( uint32 ) );

                const size_t structStart = listBuffer.size();
                const bool   bWritten    = BinarySerializer::serialize( pValuePtr, *pStructInfo, listBuffer, context );
                const uint32 structSize  = static_cast<uint32>( listBuffer.size() - structStart );

                Memory::copy( listBuffer.data() + sizePos, &structSize, sizeof( uint32 ) );
                return bWritten;
            }
        }

        const SerializeContext::TextWriteFn* pTextWriter = context.findTextWriter( resolved );
        if ( pTextWriter != nullptr )
        {
            string       str    = ( *pTextWriter )( pValuePtr );
            const uint32 size   = static_cast<uint32>( str.size() );
            const uint8* pBytes = reinterpret_cast<const uint8*>( &size );
            listBuffer.insert( listBuffer.end(), pBytes, pBytes + sizeof( uint32 ) );
            const auto* pStrBytes = reinterpret_cast<const uint8*>( str.data() );
            listBuffer.insert( listBuffer.end(), pStrBytes, pStrBytes + str.size() );
            return true;
        }

        // 실을 수 없는 타입은 아무 바이트도 쓰지 않고 실패를 돌려준다. 이유는 부른 쪽이 프로퍼티 이름과 함께 알린다(`BinarySerializer::serialize`).
        return false;
    }

    bool SerializerUtil::deserializeValueBinary( MutableValueAddress value, const hashed_string& typeName,
                                                 const uint8* pData, size_t dataSize, size_t& offset,
                                                 const SerializeContext& context, BinaryWireVersion wireVersion )
    {
        void* const                           pValuePtr = value.get();
        const hashed_string                   resolved  = SerializerUtil::resolveHandlerTypeName( typeName, context );
        const SerializeContext::BinaryReadFn* pReader   = context.findBinaryReader( resolved );
        if ( pReader != nullptr )
            return ( *pReader )( pValuePtr, pData, dataSize, offset );

        const EnumInfo* pEnumInfo = context.getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
            return SerializerUtilInternal::readEnumBinary( *pEnumInfo, value, pData, dataSize, offset, wireVersion );

        const TypeInfo* pStructInfo = context.getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                size_t blockStart{ 0 };
                uint32 blockSize{ 0 };
                if ( SerializerUtilInternal::readSizedBlock( pData, dataSize, offset, blockStart, blockSize ) == false )
                    return false;
                return BinarySerializer::deserialize( pValuePtr, *pStructInfo, pData + blockStart, blockSize, context );
            }
        }

        const SerializeContext::TextReadFn* pTextReader = context.findTextReader( resolved );
        if ( pTextReader != nullptr )
        {
            size_t blockStart{ 0 };
            uint32 blockSize{ 0 };
            if ( SerializerUtilInternal::readSizedBlock( pData, dataSize, offset, blockStart, blockSize ) == false )
                return false;
            const string text( reinterpret_cast<const utf8*>( pData + blockStart ), blockSize );
            return ( *pTextReader )( pValuePtr, text );
        }

        // 실을 수 없는 타입이다. 바이트를 건너뛰지 않고 실패를 돌려준다. 알림은 부른 쪽의 규칙을 따른다(엄격 읽기는 실패, 소프트 읽기는 orphan).
        return false;
    }

    bool SerializerUtil::serializeNestedContainerBinary( ValueAddress container, const NestedContainerInfo& nested,
                                                         vector<uint8>& listBuffer, const SerializeContext& context )
    {
        SerializerUtilInternal::ContainerWriter writer( listBuffer, context );
        ContainerVisitor::write( container, nested, writer, context );
        return writer.isAllWritten();
    }

    bool SerializerUtil::deserializeNestedContainerBinary( MutableValueAddress container, const NestedContainerInfo& nested,
                                                           const uint8* pData, size_t dataSize, size_t& offset,
                                                           const SerializeContext& context, BinaryWireVersion wireVersion )
    {
        SerializerUtilInternal::ContainerReader reader( pData, dataSize, &offset, context, wireVersion );
        return ContainerVisitor::read( container, nested, reader, context ) == ContainerReadResult::Read;
    }

    void SerializerUtil::valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, ValueAddress value, const hashed_string& typeName,
                                      const SerializeContext& context )
    {
        const void* const                    pValPtr     = value.get();
        const hashed_string                  resolved    = SerializerUtil::resolveHandlerTypeName( typeName, context );
        const SerializeContext::TextWriteFn* pTextWriter = context.findTextWriter( resolved );
        if ( pTextWriter != nullptr )
        {
            ss.append( ( *pTextWriter )( pValPtr ).c_str() );
            return;
        }

        const EnumInfo* pEnumInfo = context.getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
        {
            const int64 val = pEnumInfo->readValueFromMemory( pValPtr );
            if ( pEnumInfo->_bIsBitFlag )
                ss.append( pEnumInfo->toStringFlags( val ).c_str() );
            else
                ss.append( pEnumInfo->toString( val ).c_str() );
            return;
        }

        const TypeInfo* pStructInfo = context.getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                ss.append( JSONSerializer::serialize( pValPtr, *pStructInfo, context ) );
                return;
            }
        }

        ss.append( "null" );
    }

    bool SerializerUtil::parseTextValue( MutableValueAddress value, const hashed_string& typeName, string_view valStr,
                                         const SerializeContext& context )
    {
        void* const                         pValPtr     = value.get();
        const hashed_string                 resolved    = SerializerUtil::resolveHandlerTypeName( typeName, context );
        const SerializeContext::TextReadFn* pTextReader = context.findTextReader( resolved );
        if ( pTextReader != nullptr )
            return ( *pTextReader )( pValPtr, valStr );

        const EnumInfo* pEnumInfo = context.getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
        {
            string_view flagsText    = valStr;
            const bool  bQuotedFlags = flagsText.size() >= 2 && flagsText.front() == '"' && flagsText.back() == '"';
            if ( bQuotedFlags )
                flagsText = flagsText.substr( 1, flagsText.size() - 2 );
            // 모르는 이름이면 쓰지 않고 실패를 반환한다(값은 그대로이며 대개 멤버 초기값이다). 읽는 쪽이 orphan 으로 남기거나 실패로 알린다.
            int64 parsedValue{ 0 };
            if ( pEnumInfo->tryParseText( flagsText, parsedValue ) == false )
            {
                SW_LOG_WARNING( "'%#' is not a value of enum %# - the field keeps its current value", flagsText, pEnumInfo->_fullyQualifiedName.c_str() );
                return false;
            }
            pEnumInfo->writeValueToMemory( pValPtr, parsedValue );
            return true;
        }

        const TypeInfo* pStructInfo = context.getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                string_view sv            = StringUtil::trim( valStr );
                const bool  bQuotedStruct = sv.size() >= 2 && sv.front() == '"' && sv.back() == '"';
                if ( bQuotedStruct )
                {
                    const string unescaped = JSONDocument::unescapeString( sv.substr( 1, sv.size() - 2 ) );
                    return JSONSerializer::deserialize( pValPtr, *pStructInfo, unescaped, context );
                }
                return JSONSerializer::deserialize( pValPtr, *pStructInfo, sv, context );
            }
        }

        return false;
    }

    void SerializerUtil::applyPropertyDefault( const PropertyInfo& prop, MutableObjectAddress instance, const SerializeContext& context )
    {
        if ( instance.get() == nullptr || prop._bIsContainer != SW_FALSE )
            return;
        if ( prop._metadata._defaultValue.empty() )
            return;
        if ( applyPropertyText( prop, instance, prop._metadata._defaultValue, context ) == false )
            SW_LOG_WARNING( "Default '%#' of property '%#' (%#) cannot be read - the field keeps its current value", prop._metadata._defaultValue,
                            prop._name.c_str(), prop._typeName.c_str() );
    }

    bool SerializerUtil::copyPropertyValue( const PropertyInfo& prop, ObjectAddress srcInstance, MutableObjectAddress dstInstance, const SerializeContext& context )
    {
        const void* const pSrcInstance = srcInstance.get();
        void* const       pDstInstance = dstInstance.get();
        if ( pSrcInstance == nullptr || pDstInstance == nullptr )
            return false;
        if ( prop._bIsBitField == SW_TRUE )
        {
            const uint8 srcByte = *( static_cast<const uint8*>( pSrcInstance ) + prop._offset );
            uint8&      dstByte = *( static_cast<uint8*>( pDstInstance ) + prop._offset );
            dstByte             = ( ( srcByte & prop._bitMask ) != 0 ) ? static_cast<uint8>( dstByte | prop._bitMask ) : static_cast<uint8>( dstByte & ~prop._bitMask );
            return true;
        }

        vector<uint8> bytes;
        return writePropertyBinary( prop, srcInstance, bytes, context ) && applyPropertyBinary( prop, dstInstance, bytes.data(), bytes.size(), context );
    }

    bool SerializerUtil::writePropertyBinary( const PropertyInfo& prop, ObjectAddress instance, vector<uint8>& outBytes, const SerializeContext& context )
    {
        const void* const pInstance = instance.get();
        if ( pInstance == nullptr )
            return false;
        if ( prop._bIsBitField == SW_TRUE )
        {
            // 비트필드는 주소를 가질 수 없어 값으로 꺼내 bool 로 쓴다. 짝(`applyPropertyBinary`)도 bool 로 읽는다.
            const bool bValue = prop.getValue<bool>( pInstance );
            return serializeValueBinary( ValueAddress{ &bValue }, hashed_string( "bool" ), outBytes, context );
        }
        const void* pValue = prop.getRawPtr( pInstance );
        if ( pValue == nullptr )
            return false;
        if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
            return serializeNestedContainerBinary( ValueAddress{ pValue }, prop.getContainerShape(), outBytes, context );
        return serializeValueBinary( ValueAddress{ pValue }, prop._typeName, outBytes, context );
    }

    bool SerializerUtil::arePropertyValuesEqual( const PropertyInfo& prop, ObjectAddress instanceA, ObjectAddress instanceB, const SerializeContext& context )
    {
        const void* const pInstanceA = instanceA.get();
        const void* const pInstanceB = instanceB.get();
        if ( pInstanceA == nullptr || pInstanceB == nullptr )
            return pInstanceA == pInstanceB;
        if ( prop._bIsBitField == SW_TRUE )
        {
            const uint8 byteA = *( static_cast<const uint8*>( pInstanceA ) + prop._offset );
            const uint8 byteB = *( static_cast<const uint8*>( pInstanceB ) + prop._offset );
            return ( byteA & prop._bitMask ) == ( byteB & prop._bitMask );
        }

        const void* pA = prop.getRawPtr( pInstanceA );
        const void* pB = prop.getRawPtr( pInstanceB );
        if ( pA == nullptr || pB == nullptr )
            return pA == pB;
        vector<uint8> bytesA;
        vector<uint8> bytesB;
        bool          bWritten = false;
        if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
        {
            bWritten = serializeNestedContainerBinary( ValueAddress{ pA }, prop.getContainerShape(), bytesA, context ) &&
                       serializeNestedContainerBinary( ValueAddress{ pB }, prop.getContainerShape(), bytesB, context );
        }
        else
        {
            bWritten = serializeValueBinary( ValueAddress{ pA }, prop._typeName, bytesA, context ) && serializeValueBinary( ValueAddress{ pB }, prop._typeName, bytesB, context );
        }
        // 바이트로 옮기지 못한 값은 같은지 알 수 없다. 같다고 하면 바뀐 값을 놓치므로 다르다고 본다.
        return bWritten && bytesA == bytesB;
    }

    bool SerializerUtil::applyPropertyText( const PropertyInfo& prop, MutableObjectAddress instance, string_view text, const SerializeContext& context )
    {
        void* const pInstance = instance.get();
        if ( pInstance == nullptr || prop._bIsContainer == SW_TRUE )
            return false;
        if ( prop._bIsBitField == SW_TRUE )
        {
            bool bValue = false;
            if ( StringUtil::tryParseBool( text, bValue ) == false )
                return false;
            uint8& byte = *( static_cast<uint8*>( pInstance ) + prop._offset );
            byte        = bValue ? static_cast<uint8>( byte | prop._bitMask ) : static_cast<uint8>( byte & ~prop._bitMask );
            return true;
        }
        void* pValue = prop.getRawPtr( pInstance );
        return pValue != nullptr && parseTextValue( MutableValueAddress{ pValue }, prop._typeName, text, context );
    }

    bool SerializerUtil::applyPropertyBinary( const PropertyInfo& prop, MutableObjectAddress instance, const uint8* pPayload, size_t payloadSize, const SerializeContext& context,
                                              BinaryWireVersion wireVersion )
    {
        void* const pInstance = instance.get();
        if ( pInstance == nullptr )
            return false;
        size_t offset{ 0 };
        if ( prop._bIsBitField == SW_TRUE )
        {
            // 비트필드는 주소를 가질 수 없어 값으로 읽고 setter 로 넣는다.
            bool bValue = false;
            if ( deserializeValueBinary( MutableValueAddress{ &bValue }, hashed_string( "bool" ), pPayload, payloadSize, offset, context, wireVersion ) == false || offset != payloadSize )
                return false;
            prop.setValue<bool>( pInstance, bValue );
            return true;
        }
        void* pValue = prop.getRawPtr( pInstance );
        if ( pValue == nullptr )
            return false;
        const bool bRead = ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
                             ? deserializeNestedContainerBinary( MutableValueAddress{ pValue }, prop.getContainerShape(), pPayload, payloadSize, offset, context, wireVersion )
                             : deserializeValueBinary( MutableValueAddress{ pValue }, prop._typeName, pPayload, payloadSize, offset, context, wireVersion );
        return bRead && offset == payloadSize;
    }

    bool SerializerUtil::canCarryValueType( hashed_string typeName, const SerializeContext& context )
    {
        if ( typeName.empty() )
            return false;
        // 바이너리는 텍스트 처리기로 물러나므로(`serializeValueBinary` 의 마지막 분기) 텍스트 처리기 짝이 있으면 세 형식 모두 된다. 바이너리 처리기만 있는
        // 타입은 XML, JSON 에 "null" 로 나간다. 실어 나르지 못한다.
        const hashed_string resolved = resolveHandlerTypeName( typeName, context );
        if ( context.findTextWriter( resolved ) != nullptr && context.findTextReader( resolved ) != nullptr )
            return true;
        if ( context.getTypeRegistry().findEnum( typeName ) != nullptr )
            return true;
        const TypeInfo* pStructInfo = context.getTypeRegistry().findType( typeName );
        return pStructInfo != nullptr && pStructInfo->isPrimitive() == false;
    }

    bool SerializerUtil::canCarryProperty( const PropertyInfo& prop, const SerializeContext& context )
    {
        if ( prop._bIsBitField == SW_TRUE )
            return true;
        if ( prop._bIsContainer == SW_FALSE )
            return canCarryValueType( prop._typeName, context );
        if ( prop.hasContainerWrapper() == false )
            return false;

        // 원소가 다시 컨테이너면 안으로 내려간다. 소유 포인터 원소는 런타임 팩토리가 타입을 정한다(이름에 `*`).
        NestedContainerInfo shape = prop.getContainerShape();
        while ( true )
        {
            if ( shape._keyTypeName.empty() == false && canCarryValueType( shape._keyTypeName, context ) == false )
                return false;
            if ( shape._elementNested != nullptr )
            {
                const NestedContainerInfo inner = *shape._elementNested;
                shape                           = inner;
                continue;
            }
            return isOwnedPointerElementType( shape._elementTypeName ) || canCarryValueType( shape._elementTypeName, context );
        }
    }

    string SerializerUtil::formatPropertyText( const PropertyInfo& prop, ObjectAddress instance, const SerializeContext& context )
    {
        const void* const pInstance = instance.get();
        if ( pInstance == nullptr )
            return {};
        if ( prop._bIsBitField == SW_TRUE )
            return prop.getValue<bool>( pInstance ) ? "true" : "false";
        const void* pValue = prop.getRawPtr( pInstance );
        if ( pValue == nullptr )
            return {};
        StringBuilder<constant::kMaxBuffer8192> ss;
        if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
        {
            const NestedContainerInfo shape = prop.getContainerShape();
            ss.append( "[" );
            ss.append( static_cast<uint64>( shape._wrapper != nullptr ? shape._wrapper->getSize( pValue ) : 0 ) );
            ss.append( "]" );
            return ss.c_str();
        }
        return formatValueText( ValueAddress{ pValue }, prop._typeName, context );
    }

    string SerializerUtil::formatValueText( ValueAddress value, const hashed_string& typeName, const SerializeContext& context )
    {
        StringBuilder<constant::kMaxBuffer8192> ss;
        valueToText( ss, value, typeName, context );
        return ss.c_str();
    }

    bool SerializerUtil::keysEqual( string_view left, string_view right, bool bIgnoreCase )
    {
        return StringUtil::equals( left, right, bIgnoreCase );
    }

    const PropertyInfo* SerializerUtil::matchProperty( const vector<PropertyInfo>& listProp, string_view keyRaw,
                                                       bool bIgnoreCaseKeys, bool& bCaseVariant )
    {
        // 지금 이름을 모두 본 뒤에만 옛 이름(Alias)을 본다. 앞 프로퍼티의 옛 이름이 뒤 프로퍼티의 지금 이름을 가로채지 않는다.
        bCaseVariant = false;
        for ( const PropertyInfo& prop : listProp )
        {
            if ( SerializerUtil::keysEqual( keyRaw, prop._name.c_str(), bIgnoreCaseKeys ) )
                return &prop;
            bCaseVariant = bCaseVariant || SerializerUtil::keysEqual( keyRaw, prop._name.c_str(), true );
        }
        for ( const PropertyInfo& prop : listProp )
        {
            for ( const hashed_string& alias : prop._listAlias )
            {
                if ( alias.empty() )
                    continue;
                if ( SerializerUtil::keysEqual( keyRaw, alias.c_str(), bIgnoreCaseKeys ) )
                    return &prop;
                bCaseVariant = bCaseVariant || SerializerUtil::keysEqual( keyRaw, alias.c_str(), true );
            }
        }
        return nullptr;
    }

    size_t SerializerUtil::findPropertyIndexByNameHash( const vector<PropertyInfo>& listProp, uint32 nameHash )
    {
        for ( size_t propIndex = 0; propIndex < listProp.size(); ++propIndex )
        {
            if ( listProp[propIndex].getNameHash() == nameHash )
                return propIndex;
        }
        for ( size_t propIndex = 0; propIndex < listProp.size(); ++propIndex )
        {
            if ( listProp[propIndex].matchesRedirectHash( nameHash ) )
                return propIndex;
        }
        return listProp.size();
    }

    bool SerializerUtil::transcodeJSONToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                const SerializeContext& context )
    {
        return SerializerUtilInternal::transcodeTextToBinary( jsonStr, typeInfo, outBinary, context, [&]( void* pScratch )
        {
            return JSONSerializer::deserialize( pScratch, typeInfo, jsonStr, context );
        } );
    }

    string SerializerUtil::transcodeBinaryToJSON( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty,
                                                  const SerializeContext& context )
    {
        return SerializerUtilInternal::transcodeBinaryToText( pData, dataSize, typeInfo, context, [&]( const void* pScratch )
        {
            return bPretty ? JSONSerializer::serializePretty( pScratch, typeInfo, 4, context ) : JSONSerializer::serialize( pScratch, typeInfo, context );
        } );
    }

    bool SerializerUtil::transcodeXMLToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                               const SerializeContext& context )
    {
        return SerializerUtilInternal::transcodeTextToBinary( xmlStr, typeInfo, outBinary, context, [&]( void* pScratch )
        {
            return XMLSerializer::deserialize( pScratch, typeInfo, xmlStr, context );
        } );
    }

    string SerializerUtil::transcodeBinaryToXML( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
                                                 const SerializeContext& context )
    {
        return SerializerUtilInternal::transcodeBinaryToText( pData, dataSize, typeInfo, context, [&]( const void* pScratch )
        {
            return XMLSerializer::serialize( pScratch, typeInfo, context );
        } );
    }

    void SerializerUtil::collectRangeErrors( const TypeInfo& typeInfo, const ObjectAddress instance, const string& prefix, vector<string>& outListError )
    {
        using Internal              = SerializerUtilInternal;
        const void* const pInstance = instance.get();
        for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
        {
            const bool bSkipProperty = prop._bIsBitField == SW_TRUE || prop.hasValueAccessor();
            if ( bSkipProperty )
                continue;
            const string      path   = prefix.empty() ? string( prop._name.c_str() ) : prefix + "." + prop._name.c_str();
            const void* const pValue = prop.getRawPtr( pInstance );
            if ( prop._bIsContainer == SW_TRUE )
            {
                // 시퀀스 원소만 본다. 맵과 중첩 컨테이너는 원소 경로를 하나로 정할 수 없어 보지 않는다.
                const bool bNestedContainer = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
                const bool bSequence        = prop._containerWrapper != nullptr && prop._containerWrapper->asSequence() != nullptr && bNestedContainer == false;
                if ( bSequence == false )
                    continue;
                ISequenceContainerWrapper* pSequence    = prop._containerWrapper->asSequence();
                const TypeInfo*            pElement     = findNestedObjectType( prop._elementTypeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
                const size_t               elementCount = pSequence->getSize( pValue );
                // 필드 경계는 원소가 아니라 이 프로퍼티를 가진 객체에서 읽는다(인스펙터와 같다).
                const PropertyAllowedRange range = prop._metadata.hasAllowedRange() ? prop._metadata.computeAllowedRange( pInstance ) : PropertyAllowedRange{};
                for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                {
                    const string      elementPath   = path + "[" + to_string( elementIndex ) + "]";
                    const void* const pElementValue = pSequence->getElementConst( pValue, elementIndex );
                    float64           value         = 0.0;
                    // 구조체 원소의 값 주소는 그 원소 타입의 객체 주소다.
                    if ( pElement != nullptr )
                        collectRangeErrors( *pElement, ObjectAddress{ pElementValue }, elementPath, outListError );
                    else if ( Internal::readRangeNumber( prop._elementTypeName.view(), ValueAddress{ pElementValue }, value ) )
                        Internal::appendRangeError( elementPath, value, range, outListError );
                }
                continue;
            }
            const TypeInfo* pNested = findNestedObjectType( prop._typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
            if ( pNested != nullptr )
            {
                collectRangeErrors( *pNested, ObjectAddress{ pValue }, path, outListError );
                continue;
            }
            float64 value = 0.0;
            if ( prop._metadata.hasAllowedRange() && Internal::readRangeNumber( prop._typeName.view(), ValueAddress{ pValue }, value ) )
                Internal::appendRangeError( path, value, prop._metadata.computeAllowedRange( pInstance ), outListError );
        }
    }

    bool SerializerUtil::isWithinDeclaredRanges( const TypeInfo& typeInfo, const ObjectAddress instance )
    {
        if ( SerializerUtilInternal::isEveryValueWithinRange( typeInfo, instance ) )
            return true;
        vector<string> listRangeError;
        collectRangeErrors( typeInfo, instance, string(), listRangeError );
        for ( const string& rangeError : listRangeError )
        {
            SW_LOG_ERROR( "%#: %# - the read fails", typeInfo._fullyQualifiedName.c_str(), rangeError.c_str() );
        }
        return false;
    }
} // namespace sw

namespace sw
{
    PropertyReadRollback::PropertyReadRollback()
        : _bytes{}
        , _pOwnerType{ nullptr }
        , _pProperty{ nullptr }
        , _bOwnedPointerList{ false }
    {
    }

    bool PropertyReadRollback::keepValueBeforeRead( const TypeInfo& ownerType, const PropertyInfo& prop, ObjectAddress instance, const SerializeContext& context )
    {
        _pOwnerType        = &ownerType;
        _pProperty         = &prop;
        _bOwnedPointerList = prop._bIsContainer == SW_TRUE && SerializerUtil::isOwnedPointerElementType( prop._elementTypeName );
        _bytes.clear();
        // 소유 포인터 목록은 담지 않는다 — 되돌리면 컴포넌트를 새로 만든다. 읽다 말면 `restoreValueBeforeRead` 가 실패다.
        if ( _bOwnedPointerList )
            return true;
        if ( SerializerUtil::writePropertyBinary( prop, instance, _bytes, context ) )
            return true;
        SW_LOG_ERROR( "%#::%# (%#): the value before the read cannot be kept, so a failed read could not be undone - the object is not read", ownerType._name.c_str(),
                      prop._name.c_str(), prop._typeName.c_str() );
        return false;
    }

    bool PropertyReadRollback::restoreValueBeforeRead( MutableObjectAddress instance, const SerializeContext& context ) const
    {
        SW_ASSERT( _pOwnerType != nullptr && _pProperty != nullptr ); // keepValueBeforeRead 를 먼저 부른다
        if ( _bOwnedPointerList )
        {
            SW_LOG_ERROR( "%#::%# (%#): the owned pointer list could not be read and is half filled - the object is not read", _pOwnerType->_name.c_str(),
                          _pProperty->_name.c_str(), _pProperty->_typeName.c_str() );
            return false;
        }
        // 이 빌드가 방금 쓴 바이트라 되읽지 못하면 코덱의 결함이다 — 읽다 만 값을 받지 않고 멈춘다.
        if ( SerializerUtil::applyPropertyBinary( *_pProperty, instance, _bytes.data(), _bytes.size(), context ) )
            return true;
        SW_LOG_ERROR( "%#::%# (%#): the value before the failed read could not be put back - the object is not read", _pOwnerType->_name.c_str(),
                      _pProperty->_name.c_str(), _pProperty->_typeName.c_str() );
        return false;
    }
} // namespace sw
