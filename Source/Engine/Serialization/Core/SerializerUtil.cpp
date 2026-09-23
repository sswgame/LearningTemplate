#include "pch.h"

#include "Engine/Serialization/Core/SerializerUtil.h"

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

        // 옛 TypeInfo 가 주어지면 그쪽으로도 한 벌 읽어 둔다 — migrate 가 옛 필드를 그대로 보게 된다.
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
            /** @brief uint32 하나를 리틀엔디언 그대로 덧붙입니다. */
            static void appendUint32( vector<uint8>& buffer, uint32 value )
            {
                const uint8* pBytes = reinterpret_cast<const uint8*>( &value );
                buffer.insert( buffer.end(), pBytes, pBytes + sizeof( uint32 ) );
            }

            /** @brief uint32 하나를 읽고 오프셋을 밀어 줍니다. */
            static bool readUint32( const uint8* pData, size_t dataSize, size_t& inoutOffset, uint32& outValue )
            {
                if ( inoutOffset + sizeof( uint32 ) > dataSize )
                    return false;
                Memory::copy( &outValue, pData + inoutOffset, sizeof( uint32 ) );
                inoutOffset += sizeof( uint32 );
                return true;
            }

            /**
             * @brief 다형 소유 포인터 원소 하나를 `[이름][본문크기][본문]` 으로 적습니다.
             *
             * XML·JSON 은 태그·키 이름이 곧 런타임 타입이라 따로 적을 자리가 필요 없지만, 바이너리에는
             * 그런 자리가 없어 이름을 값으로 싣는다. **본문 크기를 같이 적는 이유는 모르는 타입을
             * 건너뛰기 위해서다** — 없으면 낯선 컴포넌트 하나가 그 뒤 스트림을 통째로 어긋낸다.
             * 빈 이름은 빈 자리를 뜻한다(원소 개수를 앞에서 이미 적었으므로 자리는 남겨야 한다).
             */
            static void writeOwnedPointerBinary( const void* pElemPtr, vector<uint8>& buffer, const SerializeContext& ctx )
            {
                const void* const* ppObj        = static_cast<const void* const*>( pElemPtr );
                const void*        pObj         = ( ppObj != nullptr ) ? *ppObj : nullptr;
                const TypeInfo*    pRuntimeType = ( pObj != nullptr ) ? ctx.getRuntimeTypeInfo( pObj ) : nullptr;

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
             * 컨테이너에 넣는 것은 **팩토리가** 한다(`createOwnedPointer` 가 소유자에 붙인다) —
             * XML·JSON 도 같은 약속이라 여기서 `appendElement` 를 부르지 않는다.
             */
            static bool readOwnedPointerBinary( const uint8* pData, size_t dataSize, size_t& inoutOffset, const SerializeContext& ctx )
            {
                uint32 nameLen{ 0 };
                if ( readUint32( pData, dataSize, inoutOffset, nameLen ) == false )
                    return false;
                if ( inoutOffset + nameLen > dataSize )
                    return false;

                // 스트림 위에서 그대로 intern 한다 - 이름 하나 읽자고 string 을 짓지 않는다.
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

                const hashed_string typeName( typeNameText );
                void*               pObj  = ctx.createOwnedPointer( typeName );
                const TypeInfo*     pType = engine::getTypeRegistry().findType( typeName );
                if ( pObj == nullptr || pType == nullptr )
                    return true; // 모르는 타입은 건너뛴다 — 위에서 이미 그만큼 밀어 놨다.

                return BinarySerializer::deserialize( pObj, *pType, pData + bodyStart, bodySize, ctx );
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

        // 없으면 리플렉션이 아는 정본 이름(`_name`)으로 한 번 더 물어본다 — Alias·옛 이름으로 들어온 경우다.
        TypeRegistry&   registry  = engine::getTypeRegistry();
        const TypeInfo* pTypeInfo = registry.findType( typeName );
        if ( pTypeInfo != nullptr && pTypeInfo->_name.empty() == false &&
             ( ctx.findBinaryWriter( pTypeInfo->_name ) != nullptr || ctx.findTextWriter( pTypeInfo->_name ) != nullptr ) )
            return pTypeInfo->_name;

        return typeName;
    }

    bool SerializerUtil::isOwnedPointerElementType( hashed_string elementTypeName )
    {
        // 판정 기준은 이름에 `*` 가 있는가 하나다 — 리플렉션이 포인터 원소를 그렇게 적는다.
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
            int64        val = pEnumInfo->readValueFromMemory( pValuePtr );
            const uint8* pB  = reinterpret_cast<const uint8*>( &val );
            listBuffer.insert( listBuffer.end(), pB, pB + sizeof( int64 ) );
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
                                                 const SerializeContext& ctx )
    {
        const hashed_string                   resolved = SerializerUtil::resolveHandlerTypeName( typeName, ctx );
        const SerializeContext::BinaryReadFn* pReader  = ctx.findBinaryReader( resolved );
        if ( pReader != nullptr )
            return ( *pReader )( pValuePtr, pData, dataSize, offset );

        const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( typeName );
        if ( pEnumInfo != nullptr )
        {
            if ( offset + sizeof( int64 ) > dataSize )
                return false;
            int64 val{ 0 };
            Memory::copy( &val, pData + offset, sizeof( int64 ) );
            pEnumInfo->writeValueToMemory( pValuePtr, val );
            offset += sizeof( int64 );
            return true;
        }

        const TypeInfo* pStructInfo = engine::getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr )
        {
            if ( pStructInfo->isPrimitive() == false )
            {
                if ( offset + sizeof( uint32 ) > dataSize )
                    return false;
                uint32 size{ 0 };
                Memory::copy( &size, pData + offset, sizeof( uint32 ) );
                offset += sizeof( uint32 );
                if ( offset + size > dataSize )
                    return false;
                const bool bOk = BinarySerializer::deserialize( pValuePtr, *pStructInfo, pData + offset, size, ctx );
                offset += size;
                return bOk;
            }
        }

        const SerializeContext::TextReadFn* pTextReader = ctx.findTextReader( resolved );
        if ( pTextReader != nullptr )
        {
            if ( offset + sizeof( uint32 ) > dataSize )
                return false;
            uint32 size{ 0 };
            Memory::copy( &size, pData + offset, sizeof( uint32 ) );
            offset += sizeof( uint32 );
            if ( offset + size > dataSize )
                return false;
            string str( reinterpret_cast<const utf8*>( pData + offset ), size );
            offset += size;
            return ( *pTextReader )( pValuePtr, str );
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

        // 다형 소유 포인터는 값이 아니라 **런타임 타입 + 본문**으로 실린다. XML·JSON 은 진작
        // 그렇게 하고 있었고 바이너리만 빠져 있었다 — 빠진 쪽은 `serializeValueBinary` 로
        // 흘러 들어가 0 바이트 하나만 적고 컴포넌트를 통째로 버렸다.
        const bool bOwnedPtr = SerializerUtil::isOwnedPointerElementType( nested._elementTypeName );

        ISequenceContainerWrapper* pSeq = nested._wrapper->asSequence();
        if ( pSeq != nullptr )
        {
            const size_t elementCount = pSeq->getSize( pContainerPtr );
            const uint32 count        = static_cast<uint32>( elementCount );
            const uint8* pB           = reinterpret_cast<const uint8*>( &count );
            listBuffer.insert( listBuffer.end(), pB, pB + sizeof( uint32 ) );

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
            const uint8* pB           = reinterpret_cast<const uint8*>( &count );
            listBuffer.insert( listBuffer.end(), pB, pB + sizeof( uint32 ) );

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
                                                           const SerializeContext& ctx )
    {
        if ( pContainerPtr == nullptr || nested._wrapper == nullptr )
            return false;

        // 소유 포인터 컨테이너는 **비우지 않는다** — 원소를 넣는 것은 팩토리(소유자)의 일이고,
        // 여기서 비우면 팩토리가 방금 붙인 것까지 날아간다. XML·JSON 도 같은 예외를 둔다.
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

            // 읽기는 여기서, **넣는 방법은 컨테이너가** 정한다 — `set` 은 다 읽은 뒤 insert 해야 한다
            // (트리에 들어간 원소를 제자리에서 고치면 정렬 불변식이 깨진다).
            for ( uint32 elemIndex = 0; elemIndex < count; ++elemIndex )
            {
                const bool bAppended = pSeq->appendElement( pContainerPtr, SW_DELEGATE_LAMBDA( ElementFillDelegate,
                                                                                               [&]( void* pElement ) -> bool
                {
                    if ( nested._elementNested != nullptr )
                        return SerializerUtil::deserializeNestedContainerBinary( pElement, *nested._elementNested, pData, dataSize, offset, ctx );
                    return SerializerUtil::deserializeValueBinary( pElement, nested._elementTypeName, pData, dataSize, offset, ctx );
                } ) );
                if ( bAppended == false )
                    return false;
            }
            return true;
        }

        IMapContainerWrapper* pMapWrap = nested._wrapper->asMap();
        if ( pMapWrap != nullptr )
        {
            vector<uint8> listKBuf( pMapWrap->getKeySize() );
            vector<uint8> listVBuf( pMapWrap->getValueSize() );
            for ( uint32 entryIndex = 0; entryIndex < count; ++entryIndex )
            {
                pMapWrap->defaultConstructKey( listKBuf.data() );
                pMapWrap->defaultConstructValue( listVBuf.data() );
                bool bOk = SerializerUtil::deserializeValueBinary( listKBuf.data(), nested._keyTypeName, pData, dataSize, offset, ctx );
                if ( bOk )
                {
                    if ( nested._elementNested != nullptr )
                        bOk = SerializerUtil::deserializeNestedContainerBinary( listVBuf.data(), *nested._elementNested, pData, dataSize, offset, ctx );
                    else
                        bOk = SerializerUtil::deserializeValueBinary( listVBuf.data(), nested._elementTypeName, pData, dataSize, offset, ctx );
                }
                if ( bOk )
                    pMapWrap->insertKeyValue( pContainerPtr, listKBuf.data(), listVBuf.data() );
                pMapWrap->destroyKey( listKBuf.data() );
                pMapWrap->destroyValue( listVBuf.data() );
                if ( bOk == false )
                    return false;
            }
            return true;
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
            const int64 flagsValue = pEnumInfo->stringFlagsToValue( flagsText );
            pEnumInfo->writeValueToMemory( pValPtr, flagsValue );
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

    bool SerializerUtil::applyPropertyDefault( void* pPropPtr, const PropertyInfo& prop, const SerializeContext& ctx )
    {
        if ( pPropPtr == nullptr || prop._bIsContainer != SW_FALSE )
            return false;
        if ( prop._metadata._defaultValue.empty() )
            return false;
        return SerializerUtil::parseTextValue( pPropPtr, prop._typeName, prop._metadata._defaultValue, ctx );
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
        if ( jsonStr.empty() || typeInfo._size == 0 )
            return false;

        ScopedScratchInstance scratch( typeInfo );
        if ( scratch.isValid() == false )
            return false;

        if ( JsonSerializer::deserialize( scratch.get(), typeInfo, jsonStr, ctx ) == false )
            return false;

        BinarySerializer::serialize( scratch.get(), typeInfo, outBinary, ctx );
        return true;
    }

    string SerializerUtil::transcodeBinaryToJson( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty,
                                                  const SerializeContext& ctx )
    {
        if ( pData == nullptr || dataSize == 0 || typeInfo._size == 0 )
            return {};

        ScopedScratchInstance scratch( typeInfo );
        if ( scratch.isValid() == false )
            return {};

        if ( BinarySerializer::deserialize( scratch.get(), typeInfo, pData, dataSize, ctx ) == false )
            return {};

        return bPretty ? JsonSerializer::serializePretty( scratch.get(), typeInfo, 4, ctx )
                       : JsonSerializer::serialize( scratch.get(), typeInfo, ctx );
    }

    bool SerializerUtil::transcodeXmlToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                               const SerializeContext& ctx )
    {
        if ( xmlStr.empty() || typeInfo._size == 0 )
            return false;

        ScopedScratchInstance scratch( typeInfo );
        if ( scratch.isValid() == false )
            return false;

        if ( XmlSerializer::deserialize( scratch.get(), typeInfo, xmlStr, ctx ) == false )
            return false;

        BinarySerializer::serialize( scratch.get(), typeInfo, outBinary, ctx );
        return true;
    }

    string SerializerUtil::transcodeBinaryToXml( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
                                                 const SerializeContext& ctx )
    {
        if ( pData == nullptr || dataSize == 0 || typeInfo._size == 0 )
            return {};

        ScopedScratchInstance scratch( typeInfo );
        if ( scratch.isValid() == false )
            return {};

        if ( BinarySerializer::deserialize( scratch.get(), typeInfo, pData, dataSize, ctx ) == false )
            return {};

        return XmlSerializer::serialize( scratch.get(), typeInfo, ctx );
    }

} // namespace sw
