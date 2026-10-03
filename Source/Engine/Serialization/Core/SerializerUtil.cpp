#include "pch.h"

#include "Engine/Serialization/Core/SerializerUtil.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    bool runVersionedDeserialize( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
                                  uint32 currentVersion, SchemaMigrateFn migrate,
                                  const TypeInfo* pLegacyTypeInfo, const SerializeContext& ctx,
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

        // 옛 TypeInfo 가 주어지면 그쪽으로도 한 벌 읽어 둔다. migrate 가 옛 필드를 그대로 보게 된다.
        if ( pLegacyTypeInfo != nullptr && pLegacyTypeInfo->_size > 0 )
        {
            uint32 legacyVersion{ 0 };
            if ( pLegacyPtr == nullptr || softDeserialize( pLegacyPtr, *pLegacyTypeInfo, listOrphan, legacyVersion ) == false )
                return false;
            if ( versionSource == SchemaVersionSource::Payload )
                outVersion = legacyVersion;
        }

        uint32 softVersion{ 0 };
        if ( softDeserialize( pInstance, typeInfo, listOrphan, softVersion ) == false )
            return false;

        // 레거시를 읽지 않았거나(그러면 이쪽이 유일한 출처다) 이번에 실제로 버전이 나왔으면 그것을 쓴다.
        if ( versionSource == SchemaVersionSource::Payload && ( pLegacyPtr == nullptr || softVersion != 0 ) )
            outVersion = softVersion;

        // **여기가 포맷마다 갈리던 한 줄이다.** 예전에는 세 벌의 복사본에 각자 다른 식이 적혀 있어서,
        // 다르다는 사실조차 나란히 놓고 보기 전에는 보이지 않았다.
        const bool bOrphanBlocks = ( orphanPolicy == SchemaOrphanPolicy::Reject ) && ( listOrphan.empty() == false );
        const bool bNeedsMigrate = ( outVersion != currentVersion ) || bOrphanBlocks;

        return runSchemaMigrateStep( outVersion, currentVersion, pInstance, typeInfo, pLegacyPtr, pLegacyTypeInfo,
                                     listOrphan, migrate, bNeedsMigrate, ctx );
    }

    namespace
    {
        struct SerializerUtilInternal
        {
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
             * @brief 열거자 항목 하나를 적습니다 — 이름 해시. 이름이 없으면(해시 0) 그 뒤에 int64 값을 잇습니다.
             * @details 값으로 싣는 것은 이름이 없는 값 · 비트뿐입니다(이름이 없으니 정체성도 값 말고는 없다).
             */
            static void appendEnumEntry( vector<uint8>& buffer, uint32 nameHash, int64 unnamedValue )
            {
                appendUint32( buffer, nameHash );
                if ( nameHash == 0 )
                    appendInt64( buffer, unnamedValue );
            }

            /**
             * @brief enum 값 하나를 열거자 정체성으로 적습니다(`SerializerUtil::serializeValueBinary` 설명의 형식).
             * @details 비트플래그의 이름은 글(`toStringFlags`)과 같은 규칙으로 모으고(`EnumInfo::collectFlagNames`) 해시 오름차순으로 적습니다 — 같은 값은
             *          같은 바이트라 바이트 비교(`arePropertyValuesEqual` · diff)가 맞습니다. 이름의 해시가 0 이면(사실상 없다) 값 전체를 값으로 싣습니다.
             */
            static void writeEnumBinary( const EnumInfo& enumInfo, const void* pValuePtr, vector<uint8>& buffer )
            {
                const int64 value = enumInfo.readValueFromMemory( pValuePtr );
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
                    appendEnumEntry( buffer, nameHash, 0 );
                if ( unnamedBits != 0 )
                    appendEnumEntry( buffer, 0, unnamedBits );
            }

            /** @brief 모르는 열거자 이름을 알립니다 — enum · 이름마다 한 번(같은 세이브의 오브젝트 천 개가 천 줄을 쓰지 않게). */
            static void warnUnknownEnumeratorOnce( const EnumInfo& enumInfo, uint32 nameHash )
            {
                static mutex                 s_mutex;
                static unordered_set<uint64> s_uniqueWarnedKeys;
                const uint64                 key = ( static_cast<uint64>( enumInfo._fullyQualifiedName.getHash() ) << 32 ) | nameHash;
                {
                    std::lock_guard<mutex> lock( s_mutex );
                    if ( s_uniqueWarnedKeys.insert( key ).second == false )
                        return;
                }
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
             * @brief enum 값 하나를 적힌 판으로 읽습니다. 옛 판은 int64 값, 지금 판은 열거자 정체성입니다.
             * @return 바이트가 모자라면 false. 모르는 열거자 이름이면 **값은 그대로 두고** false 입니다(바이트는 끝까지 읽었다 — XML 의 모르는 이름과 같은 칸
             *         실패). 옛 판의 값 읽기는 이름 표를 보지 않는다(그때 적힌 뜻이 값이었다).
             */
            [[nodiscard]] static bool readEnumBinary( const EnumInfo& enumInfo, void* pValuePtr, const uint8* pData, size_t dataSize, size_t& inoutOffset,
                                                      BinaryWireVersion wireVersion )
            {
                int64 value{ 0 };
                if ( wireVersion == BinaryWireVersion::EnumByValue )
                {
                    if ( readInt64( pData, dataSize, inoutOffset, value ) == false )
                        return false;
                    enumInfo.writeValueToMemory( pValuePtr, value );
                    return true;
                }

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
                    // 항목은 적어도 4 바이트다 — 남은 바이트로 담을 수 없는 수는 망가진 스트림이다.
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
                enumInfo.writeValueToMemory( pValuePtr, value );
                return true;
            }

            /**
             * @brief uint32 길이 머리와 그 길이만큼의 본문을 건너뜁니다. 본문 시작은 `outBlockStart`, 길이는 `outBlockSize` 이고 오프셋은 본문 뒤로 옮깁니다.
             * @return 머리나 본문이 버퍼를 넘으면 false 입니다. 구조체 · 텍스트 리더 값의 바이너리 읽기가 같은 여섯 줄을 두 벌 들고 있었습니다.
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
             * XML · JSON 은 태그 · 키 이름이 곧 런타임 타입이라 따로 적을 자리가 필요 없지만, 바이너리에는
             * 그런 자리가 없어 이름을 값으로 싣습니다. **본문 크기를 같이 적는 이유는 모르는 타입을
             * 건너뛰기 위해서입니다.** 없으면 낯선 컴포넌트 하나가 그 뒤 스트림을 통째로 어긋나게 합니다.
             * 빈 이름은 빈 자리를 뜻합니다(원소 개수를 앞에서 이미 적었으므로 자리는 남겨야 합니다).
             */
            static void writeOwnedPointerBinary( const void* pElemPtr, vector<uint8>& buffer, const SerializeContext& ctx )
            {
                const void* const* ppObj = static_cast<const void* const*>( pElemPtr );
                const void*        pObj  = ( ppObj != nullptr ) ? *ppObj : nullptr;

                // 맡아 둔 원소(모르는 타입)는 읽은 이름 · 본문 그대로 다시 쓴다.
                SerializeContext::OpaqueElementView opaque{};
                if ( ctx.queryOpaqueElement( pObj, opaque ) && opaque._format == SerializeContext::OpaqueFormat::Binary )
                {
                    appendUint32( buffer, static_cast<uint32>( opaque._typeName.size() ) );
                    const auto* pNameBytes = reinterpret_cast<const uint8*>( opaque._typeName.data() );
                    buffer.insert( buffer.end(), pNameBytes, pNameBytes + opaque._typeName.size() );
                    appendUint32( buffer, static_cast<uint32>( opaque._byteCount ) );
                    if ( opaque._pBytes != nullptr && opaque._byteCount > 0 )
                        buffer.insert( buffer.end(), opaque._pBytes, opaque._pBytes + opaque._byteCount );
                    return;
                }
                const TypeInfo* pRuntimeType = ( pObj != nullptr ) ? ctx.getRuntimeTypeInfo( pObj ) : nullptr;

                if ( pRuntimeType == nullptr )
                {
                    appendUint32( buffer, 0 ); // 이름 길이 0 = 빈 자리
                    appendUint32( buffer, 0 ); // 본문 길이 0
                    return;
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
                BinarySerializer::serialize( pObj, *pRuntimeType, buffer, ctx );
                const uint32 bodySize = static_cast<uint32>( buffer.size() - bodyStart );
                Memory::copy( buffer.data() + sizePos, &bodySize, sizeof( uint32 ) );
            }

            /**
             * @brief `writeOwnedPointerBinary` 가 적은 원소 하나를 되읽습니다.
             *
             * 컨테이너에 넣는 것은 **팩토리가** 합니다(`createOwnedPointer` 가 소유자에 붙입니다).
             * XML · JSON 도 같은 약속이라 여기서 `appendElement` 를 부르지 않습니다.
             */
            [[nodiscard]] static bool readOwnedPointerBinary( const uint8* pData, size_t dataSize, size_t& inoutOffset, const SerializeContext& ctx )
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
                inoutOffset += bodySize; // 어떤 갈래로 빠지든 다음 원소 자리는 여기다.

                if ( nameLen == 0 )
                    return true;

                // 찾기만 한다 — 파일의 이름을 intern 하면 전역 표가 파일 크기만큼 는다(`hashed_string::findInterned`).
                const hashed_string typeName = hashed_string::findInterned( typeNameText );
                const TypeInfo*     pType    = typeName.empty() ? nullptr : engine::getTypeRegistry().findType( typeName );
                void*               pObj     = ( pType != nullptr ) ? ctx.createOwnedPointer( typeName ) : nullptr;
                if ( pObj == nullptr )
                {
                    // 모르는(만들 수 없는) 타입이다 — 이름 · 본문을 맡긴다(다음 저장이 그대로 다시 쓴다). 맡을 곳이 없으면 예전처럼 건너뛴다(위에서 이미 그만큼 밀어 두었다).
                    SerializeContext::OpaqueElementView opaque{};
                    opaque._typeName  = typeNameText;
                    opaque._format    = SerializeContext::OpaqueFormat::Binary;
                    opaque._pBytes    = pData + bodyStart;
                    opaque._byteCount = bodySize;
                    (void)ctx.keepOpaqueElement( opaque );
                    return true;
                }

                return BinarySerializer::deserialize( pObj, *pType, pData + bodyStart, bodySize, ctx );
            }

            /**
             * @brief 텍스트(JSON · XML)를 임시 인스턴스로 읽어 바이너리로 다시 씁니다. `deserializeText` 가 텍스트 형식을 고릅니다.
             * @return 텍스트를 읽지 못하면 false 이고, 그때 `outBinary` 는 건드리지 않습니다.
             */
            template <typename DeserializeTextFunc>
            static bool transcodeTextToBinary( string_view text, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                               const SerializeContext& ctx, DeserializeTextFunc&& deserializeText )
            {
                if ( text.empty() || typeInfo._size == 0 )
                    return false;

                ScopedScratchInstance scratch( typeInfo );
                if ( scratch.isValid() == false || deserializeText( scratch.get() ) == false )
                    return false;

                BinarySerializer::serialize( scratch.get(), typeInfo, outBinary, ctx );
                return true;
            }

            /**
             * @brief 바이너리를 임시 인스턴스로 읽어 텍스트로 다시 씁니다. `serializeText` 가 텍스트 형식을 고릅니다.
             * @return 바이너리를 읽지 못하면 빈 문자열입니다.
             */
            template <typename SerializeTextFunc>
            static string transcodeBinaryToText( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, const SerializeContext& ctx,
                                                 SerializeTextFunc&& serializeText )
            {
                if ( pData == nullptr || dataSize == 0 || typeInfo._size == 0 )
                    return {};

                ScopedScratchInstance scratch( typeInfo );
                if ( scratch.isValid() == false || BinarySerializer::deserialize( scratch.get(), typeInfo, pData, dataSize, ctx ) == false )
                    return {};

                return serializeText( scratch.get() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const TypeInfo* SerializerUtil::findNestedObjectType( hashed_string typeName, const SerializeContext& ctx )
    {
        if ( ctx.findTextWriter( typeName ) != nullptr )
            return nullptr;
        if ( engine::getTypeRegistry().findEnum( typeName ) != nullptr )
            return nullptr;
        const TypeInfo* pTypeInfo = engine::getTypeRegistry().findType( typeName );
        if ( pTypeInfo == nullptr || pTypeInfo->isPrimitive() )
            return nullptr;
        return pTypeInfo;
    }

    hashed_string SerializerUtil::resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& ctx )
    {
        // 등록된 이름 그대로 핸들러가 있으면 그것이 정본이다.
        if ( ctx.findBinaryWriter( typeName ) != nullptr || ctx.findTextWriter( typeName ) != nullptr )
            return typeName;

        // 없으면 리플렉션이 아는 정본 이름(`_name`)으로 한 번 더 물어본다. Alias · 옛 이름으로 들어온 경우다.
        TypeRegistry&   registry  = engine::getTypeRegistry();
        const TypeInfo* pTypeInfo = registry.findType( typeName );
        if ( pTypeInfo != nullptr && pTypeInfo->_name.empty() == false &&
             ( ctx.findBinaryWriter( pTypeInfo->_name ) != nullptr || ctx.findTextWriter( pTypeInfo->_name ) != nullptr ) )
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

    const utf8* SerializerUtil::containerTypeTagName( hashed_string typeName )
    {
        const TypeInfo* pTypeInfo = engine::getTypeRegistry().findType( typeName );
        if ( pTypeInfo != nullptr )
        {
            if ( pTypeInfo->_name.empty() == false )
                return pTypeInfo->_name.c_str();
            if ( pTypeInfo->_fullyQualifiedName.empty() == false )
                return pTypeInfo->_fullyQualifiedName.c_str();
        }
        if ( typeName.empty() == false )
            return typeName.c_str();
        return nullptr;
    }

    void SerializerUtil::serializeValueBinary( const void* pValuePtr, const hashed_string& typeName,
                                               vector<uint8>& listBuffer, const SerializeContext& ctx )
    {
        const hashed_string                    resolved = SerializerUtil::resolveHandlerTypeName( typeName, ctx );
        const SerializeContext::BinaryWriteFn* pWriter  = ctx.findBinaryWriter( resolved );
        if ( pWriter != nullptr )
        {
            ( *pWriter )( pValuePtr, listBuffer );
            return;
        }

        const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
        {
            // 값이 아니라 열거자 정체성(이름 해시)으로 싣는다 — 예전에는 int64 값이라 열거자 순서가 바뀌면 세이브 · 스냅샷의 뜻이 바뀌었다.
            SerializerUtilInternal::writeEnumBinary( *pEnumInfo, pValuePtr, listBuffer );
            return;
        }

        const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                const size_t sizePos   = listBuffer.size();
                const uint32 dummySize = 0;
                const uint8* pDummy    = reinterpret_cast<const uint8*>( &dummySize );
                listBuffer.insert( listBuffer.end(), pDummy, pDummy + sizeof( uint32 ) );

                const size_t structStart = listBuffer.size();
                BinarySerializer::serialize( pValuePtr, *pStructInfo, listBuffer, ctx );
                const uint32 structSize = static_cast<uint32>( listBuffer.size() - structStart );

                Memory::copy( listBuffer.data() + sizePos, &structSize, sizeof( uint32 ) );
                return;
            }
        }

        const SerializeContext::TextWriteFn* pTextWriter = ctx.findTextWriter( resolved );
        if ( pTextWriter != nullptr )
        {
            string       str    = ( *pTextWriter )( pValuePtr );
            const uint32 size   = static_cast<uint32>( str.size() );
            const uint8* pBytes = reinterpret_cast<const uint8*>( &size );
            listBuffer.insert( listBuffer.end(), pBytes, pBytes + sizeof( uint32 ) );
            const auto* pStrBytes = reinterpret_cast<const uint8*>( str.data() );
            listBuffer.insert( listBuffer.end(), pStrBytes, pStrBytes + str.size() );
            return;
        }

        listBuffer.push_back( 0 );
    }

    bool SerializerUtil::deserializeValueBinary( void* pValuePtr, const hashed_string& typeName,
                                                 const uint8* pData, size_t dataSize, size_t& offset,
                                                 const SerializeContext& ctx, BinaryWireVersion wireVersion )
    {
        const hashed_string                   resolved = SerializerUtil::resolveHandlerTypeName( typeName, ctx );
        const SerializeContext::BinaryReadFn* pReader  = ctx.findBinaryReader( resolved );
        if ( pReader != nullptr )
            return ( *pReader )( pValuePtr, pData, dataSize, offset );

        const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
            return SerializerUtilInternal::readEnumBinary( *pEnumInfo, pValuePtr, pData, dataSize, offset, wireVersion );

        const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                size_t blockStart{ 0 };
                uint32 blockSize{ 0 };
                if ( SerializerUtilInternal::readSizedBlock( pData, dataSize, offset, blockStart, blockSize ) == false )
                    return false;
                return BinarySerializer::deserialize( pValuePtr, *pStructInfo, pData + blockStart, blockSize, ctx );
            }
        }

        const SerializeContext::TextReadFn* pTextReader = ctx.findTextReader( resolved );
        if ( pTextReader != nullptr )
        {
            size_t blockStart{ 0 };
            uint32 blockSize{ 0 };
            if ( SerializerUtilInternal::readSizedBlock( pData, dataSize, offset, blockStart, blockSize ) == false )
                return false;
            const string text( reinterpret_cast<const utf8*>( pData + blockStart ), blockSize );
            return ( *pTextReader )( pValuePtr, text );
        }

        if ( offset < dataSize )
        {
            ++offset;
            return true;
        }
        return false;
    }

    void SerializerUtil::serializeNestedContainerBinary( const void* pContainerPtr, const NestedContainerInfo& nested,
                                                         vector<uint8>& listBuffer, const SerializeContext& ctx )
    {
        if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
            return;

        // 다형 소유 포인터는 값이 아니라 **런타임 타입 + 본문**으로 실린다. XML · JSON 은 진작
        // 그렇게 하고 있었고 바이너리만 빠져 있었다. 빠진 쪽은 `serializeValueBinary` 로
        // 흘러 들어가 0 바이트 하나만 적고 컴포넌트를 통째로 버렸다.
        const bool bOwnedPtr = SerializerUtil::isOwnedPointerElementType( nested._elementTypeName );

        ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
        if ( pSeq != nullptr )
        {
            const size_t elementCount = pSeq->getSize( pContainerPtr );
            const uint32 count        = static_cast<uint32>( elementCount );
            const uint8* pByte        = reinterpret_cast<const uint8*>( &count );
            listBuffer.insert( listBuffer.end(), pByte, pByte + sizeof( uint32 ) );

            for ( size_t elemIndex = 0; elemIndex < elementCount; ++elemIndex )
            {
                const void* pElem = pSeq->getElementConst( pContainerPtr, elemIndex );
                if ( bOwnedPtr )
                    SerializerUtilInternal::writeOwnedPointerBinary( pElem, listBuffer, ctx );
                else if ( nested._elementNested != nullptr )
                    SerializerUtil::serializeNestedContainerBinary( pElem, *nested._elementNested, listBuffer, ctx );
                else
                    SerializerUtil::serializeValueBinary( pElem, nested._elementTypeName, listBuffer, ctx );
            }
            return;
        }

        IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
        if ( pMapWrap != nullptr )
        {
            const size_t elementCount = pMapWrap->getSize( pContainerPtr );
            const uint32 count        = static_cast<uint32>( elementCount );
            const uint8* pByte        = reinterpret_cast<const uint8*>( &count );
            listBuffer.insert( listBuffer.end(), pByte, pByte + sizeof( uint32 ) );

            pMapWrap->forEach( pContainerPtr, [&]( const void* pKey, const void* pVal )
            {
                SerializerUtil::serializeValueBinary( pKey, nested._keyTypeName, listBuffer, ctx );
                if ( nested._elementNested != nullptr )
                    SerializerUtil::serializeNestedContainerBinary( pVal, *nested._elementNested, listBuffer, ctx );
                else
                    SerializerUtil::serializeValueBinary( pVal, nested._elementTypeName, listBuffer, ctx );
            } );
        }
    }

    bool SerializerUtil::deserializeNestedContainerBinary( void* pContainerPtr, const NestedContainerInfo& nested,
                                                           const uint8* pData, size_t dataSize, size_t& offset,
                                                           const SerializeContext& ctx, BinaryWireVersion wireVersion )
    {
        if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
            return false;

        // 소유 포인터 컨테이너는 **비우지 않는다.** 원소를 넣는 것은 팩토리(소유자)의 일이고,
        // 여기서 비우면 팩토리가 방금 붙인 것까지 날아간다. XML · JSON 도 같은 예외를 둔다.
        const bool bOwnedPtr = SerializerUtil::isOwnedPointerElementType( nested._elementTypeName );
        if ( bOwnedPtr == false )
            nested._wrapper->clear( pContainerPtr );

        if ( offset + sizeof( uint32 ) > dataSize )
            return false;
        uint32 count{ 0 };
        Memory::copy( &count, pData + offset, sizeof( uint32 ) );
        offset += sizeof( uint32 );

        if ( ( dataSize - offset ) < count )
            return false;

        ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
        if ( pSeq != nullptr )
        {
            if ( bOwnedPtr )
            {
                for ( uint32 elemIndex = 0; elemIndex < count; ++elemIndex )
                {
                    if ( SerializerUtilInternal::readOwnedPointerBinary( pData, dataSize, offset, ctx ) == false )
                        return false;
                }
                return true;
            }

            pSeq->reserve( pContainerPtr, MathUtil::min( count, static_cast<uint32>( MathUtil::MaxUInt16 ) ) );

            // 모르는 열거자는 그 원소의 바이트를 끝까지 읽고 실패한다 — 그 원소만 기본값으로 두고 나머지를 읽는다(XML 과 같다). 예전에는 첫 실패에서
            // 멈춰 **그 뒤 원소를 모두 잃었다.** 칸은 여전히 실패로 알린다(스칼라 enum 과 같다). 그 밖의 실패는 스트림이 망가진 것이라 멈춘다.
            const bool bEnumElement = nested._elementNested == nullptr && engine::getTypeRegistry().findEnum( nested._elementTypeName ) != nullptr;
            bool       bElementFailed{ false };

            // 읽기는 여기서, **넣는 방법은 컨테이너가** 정한다. `set` 은 다 읽은 뒤 insert 해야 한다
            // (트리에 들어간 원소를 제자리에서 고치면 정렬 불변식이 깨진다).
            for ( uint32 elemIndex = 0; elemIndex < count; ++elemIndex )
            {
                const size_t elementStart = offset;
                const bool   bAppended    = pSeq->appendElement( pContainerPtr, elemIndex, SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
                     {
                    if ( nested._elementNested != nullptr )
                        return SerializerUtil::deserializeNestedContainerBinary( pElement, *nested._elementNested, pData, dataSize, offset, ctx, wireVersion );
                    return SerializerUtil::deserializeValueBinary( pElement, nested._elementTypeName, pData, dataSize, offset, ctx, wireVersion );
                } ) );
                if ( bAppended )
                    continue;
                if ( bEnumElement && offset > elementStart && offset <= dataSize )
                {
                    bElementFailed = true;
                    continue;
                }
                return false;
            }
            return bElementFailed == false;
        }

        IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
        if ( pMapWrap != nullptr )
        {
            vector<uint8> listKBuf( pMapWrap->getKeySize() );
            vector<uint8> listVBuf( pMapWrap->getValueSize() );
            // 시퀀스와 같은 규칙 — 모르는 열거자(키 · 값)인 항목만 빼고 나머지를 읽는다. 키가 모르는 열거자여도 값은 읽어야 다음 항목과 어긋나지 않는다.
            const bool bEnumKey   = engine::getTypeRegistry().findEnum( nested._keyTypeName ) != nullptr;
            const bool bEnumValue = nested._elementNested == nullptr && engine::getTypeRegistry().findEnum( nested._elementTypeName ) != nullptr;
            bool       bEntryFailed{ false };
            for ( uint32 entryIndex = 0; entryIndex < count; ++entryIndex )
            {
                pMapWrap->defaultConstructKey( listKBuf.data() );
                pMapWrap->defaultConstructValue( listVBuf.data() );
                const size_t keyStart        = offset;
                const bool   bKeyOk          = SerializerUtil::deserializeValueBinary( listKBuf.data(), nested._keyTypeName, pData, dataSize, offset, ctx, wireVersion );
                const bool   bKeySkippable   = bKeyOk == false && bEnumKey && offset > keyStart && offset <= dataSize;
                bool         bValueOk        = false;
                bool         bValueSkippable = false;
                if ( bKeyOk || bKeySkippable )
                {
                    const size_t valueStart = offset;
                    if ( nested._elementNested != nullptr )
                        bValueOk = SerializerUtil::deserializeNestedContainerBinary( listVBuf.data(), *nested._elementNested, pData, dataSize, offset, ctx, wireVersion );
                    else
                        bValueOk = SerializerUtil::deserializeValueBinary( listVBuf.data(), nested._elementTypeName, pData, dataSize, offset, ctx, wireVersion );
                    bValueSkippable = bValueOk == false && bEnumValue && offset > valueStart && offset <= dataSize;
                }
                const bool bInsert = bKeyOk && bValueOk;
                if ( bInsert )
                    pMapWrap->insertKeyValue( pContainerPtr, listKBuf.data(), listVBuf.data() );
                pMapWrap->destroyKey( listKBuf.data() );
                pMapWrap->destroyValue( listVBuf.data() );
                if ( bInsert )
                    continue;
                if ( ( bKeyOk || bKeySkippable ) && ( bValueOk || bValueSkippable ) )
                {
                    bEntryFailed = true;
                    continue;
                }
                return false;
            }
            return bEntryFailed == false;
        }
        return false;
    }

    void SerializerUtil::valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
                                      const SerializeContext& ctx )
    {
        const hashed_string                  resolved    = SerializerUtil::resolveHandlerTypeName( typeName, ctx );
        const SerializeContext::TextWriteFn* pTextWriter = ctx.findTextWriter( resolved );
        if ( pTextWriter != nullptr )
        {
            ss.append( ( *pTextWriter )( pValPtr ).c_str() );
            return;
        }

        const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
        {
            const int64 val = pEnumInfo->readValueFromMemory( pValPtr );
            if ( pEnumInfo->_bIsBitFlag )
                ss.append( pEnumInfo->toStringFlags( val ).c_str() );
            else
                ss.append( pEnumInfo->toString( val ).c_str() );
            return;
        }

        const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                ss.append( JsonSerializer::serialize( pValPtr, *pStructInfo, ctx ) );
                return;
            }
        }

        ss.append( "null" );
    }

    bool SerializerUtil::parseTextValue( void* pValPtr, const hashed_string& typeName, string_view valStr,
                                         const SerializeContext& ctx )
    {
        const hashed_string                 resolved    = SerializerUtil::resolveHandlerTypeName( typeName, ctx );
        const SerializeContext::TextReadFn* pTextReader = ctx.findTextReader( resolved );
        if ( pTextReader != nullptr )
            return ( *pTextReader )( pValPtr, valStr );

        const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
        {
            string_view flagsText = valStr;
            if ( flagsText.size() >= 2 && flagsText.front() == '"' && flagsText.back() == '"' )
                flagsText = flagsText.substr( 1, flagsText.size() - 2 );
            // 모르는 이름이면 쓰지 않고 실패를 돌려준다(값은 그대로 — 대개 멤버 초기값). 읽는 쪽이 orphan 으로 남기거나 실패로 알린다.
            int64 parsedValue{ 0 };
            if ( pEnumInfo->tryParseText( flagsText, parsedValue ) == false )
            {
                SW_LOG_WARNING( "'%#' is not a value of enum %# - the field keeps its current value", flagsText, pEnumInfo->_fullyQualifiedName.c_str() );
                return false;
            }
            pEnumInfo->writeValueToMemory( pValPtr, parsedValue );
            return true;
        }

        const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                string_view sv = StringUtil::trim( valStr );
                if ( sv.size() >= 2 && sv.front() == '"' && sv.back() == '"' )
                {
                    const string unescaped = JsonDocument::unescapeString( sv.substr( 1, sv.size() - 2 ) );
                    return JsonSerializer::deserialize( pValPtr, *pStructInfo, unescaped, ctx );
                }
                return JsonSerializer::deserialize( pValPtr, *pStructInfo, sv, ctx );
            }
        }

        return false;
    }

    void SerializerUtil::applyPropertyDefault( const PropertyInfo& prop, void* pInstance, const SerializeContext& ctx )
    {
        if ( pInstance == nullptr || prop._bIsContainer != SW_FALSE )
            return;
        if ( prop._metadata._defaultValue.empty() )
            return;
        if ( applyPropertyText( prop, pInstance, prop._metadata._defaultValue, ctx ) == false )
            SW_LOG_WARNING( "Default '%#' of property '%#' (%#) cannot be read - the field keeps its current value", prop._metadata._defaultValue,
                            prop._name.c_str(), prop._typeName.c_str() );
    }

    bool SerializerUtil::copyPropertyValue( const PropertyInfo& prop, const void* pSrcInstance, void* pDstInstance, const SerializeContext& ctx )
    {
        if ( pSrcInstance == nullptr || pDstInstance == nullptr )
            return false;
        if ( prop._bIsBitField == SW_TRUE )
        {
            const uint8 srcByte = *( static_cast<const uint8*>( pSrcInstance ) + prop._offset );
            uint8&      dstByte = *( static_cast<uint8*>( pDstInstance ) + prop._offset );
            dstByte             = ( ( srcByte & prop._bitMask ) != 0 ) ? static_cast<uint8>( dstByte | prop._bitMask ) : static_cast<uint8>( dstByte & ~prop._bitMask );
            return true;
        }

        const void* pSrc = prop.getRawPtr( pSrcInstance );
        void*       pDst = prop.getRawPtr( pDstInstance );
        if ( pSrc == nullptr || pDst == nullptr )
            return false;
        vector<uint8> bytes;
        size_t        offset{ 0 };
        if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
        {
            serializeNestedContainerBinary( pSrc, prop.getContainerShape(), bytes, ctx );
            return deserializeNestedContainerBinary( pDst, prop.getContainerShape(), bytes.data(), bytes.size(), offset, ctx );
        }
        serializeValueBinary( pSrc, prop._typeName, bytes, ctx );
        return deserializeValueBinary( pDst, prop._typeName, bytes.data(), bytes.size(), offset, ctx );
    }

    bool SerializerUtil::arePropertyValuesEqual( const PropertyInfo& prop, const void* pInstanceA, const void* pInstanceB, const SerializeContext& ctx )
    {
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
        if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
        {
            serializeNestedContainerBinary( pA, prop.getContainerShape(), bytesA, ctx );
            serializeNestedContainerBinary( pB, prop.getContainerShape(), bytesB, ctx );
        }
        else
        {
            serializeValueBinary( pA, prop._typeName, bytesA, ctx );
            serializeValueBinary( pB, prop._typeName, bytesB, ctx );
        }
        return bytesA == bytesB;
    }

    bool SerializerUtil::applyPropertyText( const PropertyInfo& prop, void* pInstance, string_view text, const SerializeContext& ctx )
    {
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
        return pValue != nullptr && parseTextValue( pValue, prop._typeName, text, ctx );
    }

    bool SerializerUtil::canCarryValueType( hashed_string typeName, const SerializeContext& ctx )
    {
        if ( typeName.empty() )
            return false;
        // 바이너리는 글 처리기로 물러나므로(`serializeValueBinary` 의 마지막 갈래) 글 처리기 짝이 있으면 세 형식 모두 된다. 바이너리 처리기만 있는
        // 타입은 XML · JSON 에 "null" 로 나간다 — 실어 나르지 못한다.
        const hashed_string resolved = resolveHandlerTypeName( typeName, ctx );
        if ( ctx.findTextWriter( resolved ) != nullptr && ctx.findTextReader( resolved ) != nullptr )
            return true;
        if ( engine::getTypeRegistry().findEnum( typeName ) != nullptr )
            return true;
        const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
        return pStructInfo != nullptr && pStructInfo->isPrimitive() == false;
    }

    bool SerializerUtil::canCarryProperty( const PropertyInfo& prop, const SerializeContext& ctx )
    {
        if ( prop._bIsBitField == SW_TRUE )
            return true;
        if ( prop._bIsContainer == SW_FALSE )
            return canCarryValueType( prop._typeName, ctx );
        if ( prop.hasContainerWrapper() == false )
            return false;

        // 원소가 다시 컨테이너면 안으로 내려간다. 소유 포인터 원소는 런타임 팩토리가 타입을 정한다(이름에 `*`).
        NestedContainerInfo shape = prop.getContainerShape();
        while ( true )
        {
            if ( shape._keyTypeName.empty() == false && canCarryValueType( shape._keyTypeName, ctx ) == false )
                return false;
            if ( shape._elementNested != nullptr )
            {
                const NestedContainerInfo inner = *shape._elementNested;
                shape                           = inner;
                continue;
            }
            return isOwnedPointerElementType( shape._elementTypeName ) || canCarryValueType( shape._elementTypeName, ctx );
        }
    }

    string SerializerUtil::formatPropertyText( const PropertyInfo& prop, const void* pInstance, const SerializeContext& ctx )
    {
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
        valueToText( ss, pValue, prop._typeName, ctx );
        return ss.c_str();
    }

    bool SerializerUtil::keysEqual( string_view left, string_view right, bool bIgnoreCase )
    {
        return StringUtil::equals( left, right, bIgnoreCase );
    }

    const PropertyInfo* SerializerUtil::matchProperty( const vector<PropertyInfo>& listProp, string_view keyRaw,
                                                       bool bIgnoreCaseKeys, bool& bCaseVariant )
    {
        const PropertyInfo* pMatched = nullptr;
        bCaseVariant                 = false;
        for ( const PropertyInfo& prop : listProp )
        {
            if ( SerializerUtil::keysEqual( keyRaw, prop._name.c_str(), bIgnoreCaseKeys ) )
                return &prop;
            bCaseVariant = bCaseVariant || SerializerUtil::keysEqual( keyRaw, prop._name.c_str(), true );
            for ( const hashed_string& alias : prop._listAlias )
            {
                if ( alias.empty() )
                    continue;
                if ( SerializerUtil::keysEqual( keyRaw, alias.c_str(), bIgnoreCaseKeys ) )
                    return &prop;
                bCaseVariant = bCaseVariant || SerializerUtil::keysEqual( keyRaw, alias.c_str(), true );
            }
        }
        return pMatched;
    }

    bool SerializerUtil::transcodeJsonToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                const SerializeContext& ctx )
    {
        return SerializerUtilInternal::transcodeTextToBinary( jsonStr, typeInfo, outBinary, ctx, [&]( void* pScratch )
        {
            return JsonSerializer::deserialize( pScratch, typeInfo, jsonStr, ctx );
        } );
    }

    string SerializerUtil::transcodeBinaryToJson( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty,
                                                  const SerializeContext& ctx )
    {
        return SerializerUtilInternal::transcodeBinaryToText( pData, dataSize, typeInfo, ctx, [&]( const void* pScratch )
        {
            return bPretty ? JsonSerializer::serializePretty( pScratch, typeInfo, 4, ctx ) : JsonSerializer::serialize( pScratch, typeInfo, ctx );
        } );
    }

    bool SerializerUtil::transcodeXmlToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                               const SerializeContext& ctx )
    {
        return SerializerUtilInternal::transcodeTextToBinary( xmlStr, typeInfo, outBinary, ctx, [&]( void* pScratch )
        {
            return XmlSerializer::deserialize( pScratch, typeInfo, xmlStr, ctx );
        } );
    }

    string SerializerUtil::transcodeBinaryToXml( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
                                                 const SerializeContext& ctx )
    {
        return SerializerUtilInternal::transcodeBinaryToText( pData, dataSize, typeInfo, ctx, [&]( const void* pScratch )
        {
            return XmlSerializer::serialize( pScratch, typeInfo, ctx );
        } );
    }

} // namespace sw
