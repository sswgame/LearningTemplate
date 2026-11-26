#include "pch.h"

#include "Engine/Serialization/Serializer/ValueSerializer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/InlineAllocator.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/SchemaMigrate.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/Serializer/ContainerVisitor.h"
#include "Engine/Serialization/Serializer/NestedObjectCodec.h"

namespace sw
{
    namespace
    {
        struct ValueSerializerInternal
        {
            /** @brief uint32 하나를 읽고 오프셋을 그만큼 옮깁니다. */
            [[nodiscard]] static bool readUint32( const uint8* pData, size_t dataSize, size_t& inoutOffset, uint32& outValue )
            {
                if ( inoutOffset > dataSize )
                    return false;
                ByteReader   reader( pData + inoutOffset, dataSize - inoutOffset );
                const uint32 value = reader.readUint32(); // 실패하면 출력을 건드리지 않는다
                if ( reader.hasOverflowed() )
                    return false;
                outValue = value;
                inoutOffset += sizeof( uint32 );
                return true;
            }

            /** @brief int64 하나를 읽고 오프셋을 그만큼 옮깁니다. */
            [[nodiscard]] static bool readInt64( const uint8* pData, size_t dataSize, size_t& inoutOffset, int64& outValue )
            {
                if ( inoutOffset > dataSize )
                    return false;
                ByteReader  reader( pData + inoutOffset, dataSize - inoutOffset );
                const int64 value = reader.readInt64(); // 실패하면 출력을 건드리지 않는다
                if ( reader.hasOverflowed() )
                    return false;
                outValue = value;
                inoutOffset += sizeof( int64 );
                return true;
            }

            /**
             * @brief 열거자 항목 하나를 적습니다. 이름 해시를 적고, 이름이 없으면(해시 0) 그 뒤에 int64 값을 이어 적습니다.
             * @details 값으로 싣는 것은 이름이 없는 값과 비트뿐이다(이름이 없으니 정체성도 값 말고는 없다).
             */
            static void appendEnumEntry( vector<uint8>& buffer, uint32 nameHash, int64 unnamedValue )
            {
                LittleEndianUtil::appendUint32( buffer, nameHash );
                if ( nameHash == 0 )
                    LittleEndianUtil::appendInt64( buffer, unnamedValue );
            }

            /**
             * @brief enum 값 하나를 열거자 정체성으로 적습니다(`ValueSerializer::serializeValueBinary` 설명의 형식).
             * @details 비트플래그의 이름은 텍스트(`toStringFlags`)와 같은 규칙으로 모으고(`EnumInfo::collectFlagNames`) 해시 오름차순으로 적는다. 같은 값은
             *          같은 바이트라 바이트 비교(`arePropertyValuesEqual`, diff)가 맞는다. 이름의 해시가 0 이면(사실상 없다) 값 전체를 값으로 싣는다.
             */
            static void writeEnumBinary( const EnumInfo& enumInfo, const ValueAddress address, vector<uint8>& buffer )
            {
                const int64 value = enumInfo.readValueFromMemory( address.get() );
                if ( enumInfo._bIsBitFlag == SW_FALSE )
                {
                    appendEnumEntry( buffer, enumInfo.getNameByValue( value ).getHash(), value );
                    return;
                }

                vector<hashed_string> listName;
                const int64           unnamedBits = enumInfo.collectFlagNames( value, listName );
                vector<uint32>        listNameHash;
                listNameHash.reserve( listName.size() );
                bool bUnhashed = false;
                for ( const hashed_string& name : listName )
                {
                    listNameHash.push_back( name.getHash() );
                    bUnhashed = bUnhashed || name.getHash() == 0;
                }
                if ( bUnhashed )
                {
                    LittleEndianUtil::appendUint32( buffer, 1 );
                    appendEnumEntry( buffer, 0, value );
                    return;
                }
                VectorUtil::sort( listNameHash );
                LittleEndianUtil::appendUint32( buffer, static_cast<uint32>( listNameHash.size() + ( unnamedBits != 0 ? 1 : 0 ) ) );
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
                SW_LOG_WARNING_ID( "Serialization.UnknownEnumeratorHash", "%#: a saved enumerator (name hash %#) is not a value of it any more - the field keeps its current value (once per enumerator)",
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
             * @return 바이트가 모자라면 false 입니다. 모르는 열거자 이름이면 값은 그대로 두고 false 입니다(바이트는 끝까지 읽었으며, XML 의 모르는 이름과 같은 필드
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
                    LittleEndianUtil::appendUint32( buffer, static_cast<uint32>( opaque._typeName.size() ) );
                    const auto* pNameBytes = reinterpret_cast<const uint8*>( opaque._typeName.data() );
                    buffer.insert( buffer.end(), pNameBytes, pNameBytes + opaque._typeName.size() );
                    LittleEndianUtil::appendUint32( buffer, static_cast<uint32>( opaque._byteCount ) );
                    if ( opaque._pBytes != nullptr && opaque._byteCount > 0 )
                        buffer.insert( buffer.end(), opaque._pBytes, opaque._pBytes + opaque._byteCount );
                    return true;
                }
                const TypeInfo* pRuntimeType = ( pObject != nullptr ) ? context.getRuntimeTypeInfo( pObject ) : nullptr;

                if ( pRuntimeType == nullptr )
                {
                    LittleEndianUtil::appendUint32( buffer, 0 ); // 이름 길이 0 은 빈 슬롯을 뜻한다
                    LittleEndianUtil::appendUint32( buffer, 0 ); // 본문 길이 0
                    return true;
                }

                const utf8*  pName   = pRuntimeType->_name.c_str();
                const uint32 nameLen = ( pName != nullptr ) ? static_cast<uint32>( pRuntimeType->_name.size() ) : 0;
                LittleEndianUtil::appendUint32( buffer, nameLen );
                if ( nameLen > 0 )
                {
                    const auto* pNameBytes = reinterpret_cast<const uint8*>( pName );
                    buffer.insert( buffer.end(), pNameBytes, pNameBytes + nameLen );
                }

                const size_t sizePos = buffer.size();
                LittleEndianUtil::appendUint32( buffer, 0 );

                const size_t bodyStart = buffer.size();
                const bool   bWritten  = NestedObjectCodec::writeBinary( pObject, *pRuntimeType, buffer, context );
                const uint32 bodySize  = static_cast<uint32>( buffer.size() - bodyStart );
                LittleEndianUtil::writeUint32( buffer.data() + sizePos, bodySize );
                return bWritten;
            }

            /**
             * @brief `writeOwnedPointerBinary` 가 적은 원소 하나를 되읽습니다.
             *
             * 컨테이너에 넣는 것은 팩토리가 합니다(`createOwnedPointer` 가 소유자에 붙입니다).
             * XML, JSON 도 같은 약속이라 여기서 `appendElement` 를 부르지 않습니다.
             * 본문 크기가 있어 모르는 타입은 건너뛰지만, 아는 타입의 본문을 못 읽으면 스트림이 망가진 것입니다(`StreamBroken`).
             * 본문 안에 객체째 실패여야 할 것(금지된 필드)이 있으면 `ObjectRejected` 로 올립니다 — 원소 실패로 낮추지 않습니다.
             */
            [[nodiscard]] static ContainerReadResult readOwnedPointerBinary( const uint8* pData, size_t dataSize, size_t& inoutOffset, const SerializeContext& context )
            {
                size_t nameStart{ 0 };
                uint32 nameLen{ 0 };
                if ( readSizedBlock( pData, dataSize, inoutOffset, nameStart, nameLen ) == false )
                    return ContainerReadResult::StreamBroken;

                // 스트림 위에서 그대로 intern 한다. 이름 하나 읽자고 string 을 만들지 않는다.
                const string_view typeNameText( reinterpret_cast<const utf8*>( pData + nameStart ), nameLen );

                // 어떤 경우로 빠지든 다음 원소 위치는 본문 뒤다.
                size_t bodyStart{ 0 };
                uint32 bodySize{ 0 };
                if ( readSizedBlock( pData, dataSize, inoutOffset, bodyStart, bodySize ) == false )
                    return ContainerReadResult::StreamBroken;

                if ( nameLen == 0 )
                    return ContainerReadResult::Read;

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
                    return ContainerReadResult::Read;
                }

                switch ( NestedObjectCodec::readBinary( pObj, *pType, pData + bodyStart, bodySize, context ) )
                {
                    case FieldReadResult::Read:
                        return ContainerReadResult::Read;
                    case FieldReadResult::FieldFailed:
                        return ContainerReadResult::StreamBroken;
                    case FieldReadResult::ObjectRejected:
                        return ContainerReadResult::ObjectRejected;
                }
                return ContainerReadResult::ObjectRejected;
            }

            /** @brief 컨테이너를 바이너리로 적습니다. 원소 수(uint32) 뒤에 원소를 적고, 맵은 항목마다 키와 값을 적습니다. 소유 포인터 원소는 `[이름][본문크기][본문]` 입니다. */
            class ContainerWriter final : public IContainerWriter
            {
            public:
                ContainerWriter( vector<uint8>& buffer, const SerializeContext& context, const NestedContainerInfo& shape )
                    : _listBuffer{ buffer }
                    , _context{ context }
                    , _shape{ shape }
                    , _bAllWritten{ SW_TRUE }
                {
                }

                void beginSequence( const size_t elementCount ) override { LittleEndianUtil::appendUint32( _listBuffer, static_cast<uint32>( elementCount ) ); }
                void endSequence() override {}
                void beginMap( const size_t entryCount ) override { LittleEndianUtil::appendUint32( _listBuffer, static_cast<uint32>( entryCount ) ); }
                void endMap() override {}
                void beginMapEntry( const ValueAddress key, const hashed_string& keyTypeName ) override
                {
                    recordWritten( ValueSerializer::serializeValueBinary( key, keyTypeName, _shape.findBinding( keyTypeName ), _listBuffer, _context ) );
                }
                void endMapEntry() override {}
                void beginNestedContainer( ContainerSlot ) override {}
                void endNestedContainer( ContainerSlot ) override {}
                void writeOwnedPointer( const ObjectAddress object ) override { recordWritten( writeOwnedPointerBinary( object, _listBuffer, _context ) ); }

                // 값 구조체도 값 하나를 적는 경로로 적는다. 등록된 바이너리 핸들러가 구조체보다 먼저다(`serializeValueBinary` 의 순서).
                void writeValueObject( const ValueAddress value, const hashed_string& typeName, const TypeInfo&, ContainerSlot ) override
                {
                    recordWritten( ValueSerializer::serializeValueBinary( value, typeName, _shape.findBinding( typeName ), _listBuffer, _context ) );
                }

                void writeScalar( const ValueAddress value, const hashed_string& typeName, ContainerSlot ) override
                {
                    recordWritten( ValueSerializer::serializeValueBinary( value, typeName, _shape.findBinding( typeName ), _listBuffer, _context ) );
                }

                /** @brief 원소 · 키 · 값을 하나도 빠짐없이 썼는가입니다. 방문자의 콜백은 결과를 돌려주지 못하므로 여기에 모읍니다. */
                bool isAllWritten() const { return _bAllWritten; }

            private:
                void recordWritten( const bool bWritten )
                {
                    if ( bWritten == false )
                        _bAllWritten = SW_FALSE;
                }

            private:
                vector<uint8>&          _listBuffer;
                const SerializeContext& _context;
                NestedContainerInfo     _shape;
                uint8                   _bAllWritten : 1;
            };

            /** @brief 바이너리 컨테이너를 읽습니다. 신뢰할 수 없는 바이트의 경계 검사가 이 안에 있습니다(원소 수, 값, 소유 포인터 본문). */
            class ContainerReader final : public IContainerReader
            {
            public:
                ContainerReader( const uint8* pData, const size_t dataSize, size_t* pInOutOffset, const SerializeContext& context, const BinaryWireVersion wireVersion,
                                 const NestedContainerInfo& shape )
                    : _listOpenCount{}
                    , _pData{ pData }
                    , _dataSize{ dataSize }
                    , _pInOutOffset{ pInOutOffset }
                    , _context{ context }
                    , _shape{ shape }
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
                        if ( ContainerVisitor::shouldStopReading( total ) )
                            break;
                    }
                    return total;
                }

                ContainerReadResult readMapKey( const MutableValueAddress key, const hashed_string& keyTypeName ) override { return readValue( key, keyTypeName ); }

                ContainerReadResult readOwnedPointer() override { return readOwnedPointerBinary( _pData, _dataSize, *_pInOutOffset, _context ); }

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

                /**
                 * @brief 값 하나를 읽습니다. 모르는 열거자는 바이트를 끝까지 읽고 실패하므로 그 원소만 빼고(FieldFailed), 그 밖의 실패는 위치를 알 수 없습니다.
                 *        값 구조체 안의 객체째 실패는 원소 실패로 낮추지 않고 그대로 올립니다(ObjectRejected).
                 */
                ContainerReadResult readValue( const MutableValueAddress value, const hashed_string& typeName ) const
                {
                    const size_t valueStart = *_pInOutOffset;
                    switch ( ValueSerializer::deserializeValueBinary( value, typeName, _shape.findBinding( typeName ), _pData, _dataSize, *_pInOutOffset, _context, _wireVersion ) )
                    {
                        case FieldReadResult::Read:
                            return ContainerReadResult::Read;
                        case FieldReadResult::ObjectRejected:
                            return ContainerReadResult::ObjectRejected;
                        case FieldReadResult::FieldFailed:
                            break;
                    }
                    const bool bAdvanced  = valueStart < *_pInOutOffset && *_pInOutOffset <= _dataSize;
                    const bool bSkippable = bAdvanced && _context.getTypeRegistry().findEnum( typeName ) != nullptr;
                    return bSkippable ? ContainerReadResult::FieldFailed : ContainerReadResult::StreamBroken;
                }

            private:
                vector<uint32, InlineAllocator<uint32, 4>> _listOpenCount;
                const uint8*                               _pData;
                size_t                                     _dataSize;
                size_t*                                    _pInOutOffset;
                const SerializeContext&                    _context;
                NestedContainerInfo                        _shape;
                BinaryWireVersion                          _wireVersion;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    const TypeInfo* ValueSerializer::findNestedObjectType( const hashed_string& typeName, const ReflectValueBinding* pBinding, const SerializeContext& context )
    {
        const ValueCodecs codecs = findValueCodecs( typeName, pBinding, context );
        if ( codecs.findTextWriter( context ) != nullptr || codecs.findEnum( context ) != nullptr )
            return nullptr;
        return codecs.findStructType( context );
    }

    hashed_string ValueSerializer::resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& context )
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

    bool ValueSerializer::isOwnedPointerElementType( hashed_string elementTypeName )
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

    bool ValueSerializer::hasBinaryValueCodec( const hashed_string& typeName, const SerializeContext& context )
    {
        // `serializeValueBinary` · `deserializeValueBinary` 가 고르는 순서와 같다. 바이너리 처리기, 열거형, 구조체, 텍스트 처리기다.
        const hashed_string resolved = ValueSerializer::resolveHandlerTypeName( typeName, context );
        if ( context.findBinaryWriter( resolved ) != nullptr && context.findBinaryReader( resolved ) != nullptr )
            return true;
        if ( context.getTypeRegistry().findEnum( typeName ) != nullptr )
            return true;
        const TypeInfo* pStructInfo = context.getTypeRegistry().findType( typeName );
        if ( pStructInfo != nullptr && pStructInfo->isPrimitive() == false )
            return true;
        return context.findTextWriter( resolved ) != nullptr && context.findTextReader( resolved ) != nullptr;
    }

    const SerializeContext::BinaryWriteFn* ValueSerializer::ValueCodecs::findBinaryWriter( const SerializeContext& context ) const
    {
        return _builtinIndex != ReflectBuiltinIndex::kNone ? context.findBinaryWriter( _builtinIndex ) : context.findBinaryWriter( _handlerName );
    }

    const SerializeContext::BinaryReadFn* ValueSerializer::ValueCodecs::findBinaryReader( const SerializeContext& context ) const
    {
        return _builtinIndex != ReflectBuiltinIndex::kNone ? context.findBinaryReader( _builtinIndex ) : context.findBinaryReader( _handlerName );
    }

    const SerializeContext::TextWriteFn* ValueSerializer::ValueCodecs::findTextWriter( const SerializeContext& context ) const
    {
        return _builtinIndex != ReflectBuiltinIndex::kNone ? context.findTextWriter( _builtinIndex ) : context.findTextWriter( _handlerName );
    }

    const SerializeContext::TextReadFn* ValueSerializer::ValueCodecs::findTextReader( const SerializeContext& context ) const
    {
        return _builtinIndex != ReflectBuiltinIndex::kNone ? context.findTextReader( _builtinIndex ) : context.findTextReader( _handlerName );
    }

    const hashed_string& ValueSerializer::ValueCodecs::getHandlerName() const
    {
        return _builtinIndex != ReflectBuiltinIndex::kNone ? ReflectedValueUtil::getBuiltinTypeName( _builtinIndex ) : _handlerName;
    }

    const EnumInfo* ValueSerializer::ValueCodecs::findEnum( const SerializeContext& context ) const
    {
        return _bByName ? context.getTypeRegistry().findEnum( _typeName ) : _pEnum;
    }

    const TypeInfo* ValueSerializer::ValueCodecs::findStructType( const SerializeContext& context ) const
    {
        const TypeInfo* pType = _bByName ? context.getTypeRegistry().findType( _typeName ) : _pType;
        return pType != nullptr && pType->isPrimitive() == false ? pType : nullptr;
    }

    ValueSerializer::ValueCodecs ValueSerializer::findValueCodecs( const hashed_string& typeName, const ReflectValueBinding* pBinding, const SerializeContext& context )
    {
        ValueCodecs codecs;
        codecs._typeName     = typeName;
        codecs._builtinIndex = ReflectBuiltinIndex::kNone;
        // 묶음이 있으면 레지스트리 사실은 세대 캐시에서, 내장형 처리기는 문맥의 색인 배열에서 — 이름 해시 조회 · 공유 잠금이 없다.
        // 묶음은 엔진 레지스트리의 사실이라 레지스트리를 따로 준 문맥은 이름으로 찾는다.
        if ( pBinding != nullptr && context.hasCustomTypeRegistry() == false )
        {
            const ReflectValueFacts facts = pBinding->resolve( typeName, context.getTypeRegistry() );
            codecs._pEnum                 = facts._pEnum;
            codecs._pType                 = facts._pType;
            if ( facts._builtinIndex != ReflectBuiltinIndex::kNone )
            {
                codecs._builtinIndex = facts._builtinIndex;
                return codecs;
            }
            // 내장형이 아닌 처리기 형식(`ReflectedAny` 등) · 별칭 — `resolveHandlerTypeName` 과 같은 규칙이되 형식은 묶음의 것을 쓴다.
            const bool bOwnHandler  = context.findBinaryWriter( typeName ) != nullptr || context.findTextWriter( typeName ) != nullptr;
            const bool bTypeHandler = bOwnHandler == false && facts._pType != nullptr && facts._pType->_name.empty() == false &&
                                      ( context.findBinaryWriter( facts._pType->_name ) != nullptr || context.findTextWriter( facts._pType->_name ) != nullptr );
            codecs._handlerName = bTypeHandler ? facts._pType->_name : typeName;
            return codecs;
        }
        codecs._handlerName = ValueSerializer::resolveHandlerTypeName( typeName, context );
        codecs._bByName     = true;
        return codecs;
    }

    bool ValueSerializer::serializeValueBinary( ValueAddress value, const hashed_string& typeName, vector<uint8>& listBuffer, const SerializeContext& context )
    {
        return serializeValueBinary( value, typeName, nullptr, listBuffer, context );
    }

    bool ValueSerializer::serializeValueBinary( ValueAddress value, const hashed_string& typeName, const ReflectValueBinding* pBinding, vector<uint8>& listBuffer,
                                                const SerializeContext& context )
    {
        const void* const                      pValuePtr = value.get();
        const ValueCodecs                      codecs    = findValueCodecs( typeName, pBinding, context );
        const SerializeContext::BinaryWriteFn* pWriter   = codecs.findBinaryWriter( context );
        if ( pWriter != nullptr )
        {
            ( *pWriter )( pValuePtr, listBuffer );
            return true;
        }

        if ( const EnumInfo* pEnumInfo = codecs.findEnum( context ) )
        {
            // 값이 아니라 열거자 정체성(이름 해시)으로 싣는다. 값으로 실으면 열거자 순서가 바뀔 때 세이브와 스냅샷의 뜻이 바뀐다.
            ValueSerializerInternal::writeEnumBinary( *pEnumInfo, value, listBuffer );
            return true;
        }

        if ( const TypeInfo* pStructInfo = codecs.findStructType( context ) )
        {
            const size_t sizePos = listBuffer.size();
            LittleEndianUtil::appendUint32( listBuffer, 0 );

            const size_t structStart = listBuffer.size();
            const bool   bWritten    = NestedObjectCodec::writeBinary( pValuePtr, *pStructInfo, listBuffer, context );
            const uint32 structSize  = static_cast<uint32>( listBuffer.size() - structStart );

            LittleEndianUtil::writeUint32( listBuffer.data() + sizePos, structSize );
            return bWritten;
        }

        const SerializeContext::TextWriteFn* pTextWriter = codecs.findTextWriter( context );
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

        // 실을 수 없는 타입은 아무 바이트도 쓰지 않고 실패를 돌려준다. 이유는 부른 쪽이 프로퍼티 이름과 함께 알린다(바이너리 직렬화기).
        return false;
    }

    FieldReadResult ValueSerializer::deserializeValueBinary( MutableValueAddress value, const hashed_string& typeName, const uint8* pData, size_t dataSize, size_t& offset,
                                                             const SerializeContext& context, BinaryWireVersion wireVersion )
    {
        return deserializeValueBinary( value, typeName, nullptr, pData, dataSize, offset, context, wireVersion );
    }

    FieldReadResult ValueSerializer::deserializeValueBinary( MutableValueAddress value, const hashed_string& typeName, const ReflectValueBinding* pBinding, const uint8* pData,
                                                             size_t dataSize, size_t& offset, const SerializeContext& context, BinaryWireVersion wireVersion )
    {
        void* const                           pValuePtr = value.get();
        const ValueCodecs                     codecs    = findValueCodecs( typeName, pBinding, context );
        const SerializeContext::BinaryReadFn* pReader   = codecs.findBinaryReader( context );
        if ( pReader != nullptr )
            return ContainerVisitor::makeFieldResult( ( *pReader )( pValuePtr, pData, dataSize, offset ) );

        if ( const EnumInfo* pEnumInfo = codecs.findEnum( context ) )
            return ContainerVisitor::makeFieldResult( ValueSerializerInternal::readEnumBinary( *pEnumInfo, value, pData, dataSize, offset, wireVersion ) );

        if ( const TypeInfo* pStructInfo = codecs.findStructType( context ) )
        {
            size_t blockStart{ 0 };
            uint32 blockSize{ 0 };
            if ( ValueSerializerInternal::readSizedBlock( pData, dataSize, offset, blockStart, blockSize ) == false )
                return FieldReadResult::FieldFailed;
            // 구조체 안의 객체째 실패(금지된 필드)는 그대로 올린다. bool 로 접으면 부른 쪽이 이 칸만 되돌리고 orphan 으로 넘긴다.
            return NestedObjectCodec::readBinary( pValuePtr, *pStructInfo, pData + blockStart, blockSize, context );
        }

        const SerializeContext::TextReadFn* pTextReader = codecs.findTextReader( context );
        if ( pTextReader != nullptr )
        {
            size_t blockStart{ 0 };
            uint32 blockSize{ 0 };
            if ( ValueSerializerInternal::readSizedBlock( pData, dataSize, offset, blockStart, blockSize ) == false )
                return FieldReadResult::FieldFailed;
            const string text( reinterpret_cast<const utf8*>( pData + blockStart ), blockSize );
            return ContainerVisitor::makeFieldResult( ( *pTextReader )( pValuePtr, text ) );
        }

        // 실을 수 없는 타입이다. 바이트를 건너뛰지 않고 실패를 돌려준다. 알림은 부른 쪽의 규칙을 따른다(엄격 읽기는 실패, 소프트 읽기는 orphan).
        return FieldReadResult::FieldFailed;
    }

    bool ValueSerializer::serializeNestedContainerBinary( ValueAddress container, const NestedContainerInfo& nested,
                                                          vector<uint8>& listBuffer, const SerializeContext& context )
    {
        ValueSerializerInternal::ContainerWriter writer( listBuffer, context, nested );
        ContainerVisitor::write( container, nested, writer, context );
        return writer.isAllWritten();
    }

    FieldReadResult ValueSerializer::deserializeNestedContainerBinary( MutableValueAddress container, const NestedContainerInfo& nested,
                                                                       const uint8* pData, size_t dataSize, size_t& offset,
                                                                       const SerializeContext& context, BinaryWireVersion wireVersion )
    {
        ValueSerializerInternal::ContainerReader reader( pData, dataSize, &offset, context, wireVersion, nested );
        return ContainerVisitor::toFieldResult( ContainerVisitor::read( container, nested, reader, context ) );
    }

    void ValueSerializer::valueToText( StringBuilder<constant::buffer::kMax8192>& ss, ValueAddress value, const hashed_string& typeName,
                                       const SerializeContext& context )
    {
        valueToText( ss, value, typeName, nullptr, context );
    }

    void ValueSerializer::valueToText( StringBuilder<constant::buffer::kMax8192>& ss, ValueAddress value, const hashed_string& typeName, const ReflectValueBinding* pBinding,
                                       const SerializeContext& context )
    {
        const void* const                    pValPtr     = value.get();
        const ValueCodecs                    codecs      = findValueCodecs( typeName, pBinding, context );
        const SerializeContext::TextWriteFn* pTextWriter = codecs.findTextWriter( context );
        if ( pTextWriter != nullptr )
        {
            ss.append( ( *pTextWriter )( pValPtr ).c_str() );
            return;
        }

        const EnumInfo* pEnumInfo = codecs.findEnum( context );
        if ( pEnumInfo != nullptr )
        {
            const int64 val = pEnumInfo->readValueFromMemory( pValPtr );
            if ( pEnumInfo->_bIsBitFlag )
                ss.append( pEnumInfo->toStringFlags( val ) );
            else
                ss.append( pEnumInfo->toString( val ) );
            return;
        }

        if ( const TypeInfo* pStructInfo = codecs.findStructType( context ) )
        {
            ss.append( NestedObjectCodec::writeJSON( pValPtr, *pStructInfo, context ) );
            return;
        }

        ss.append( "null" );
    }

    bool ValueSerializer::parseTextValue( MutableValueAddress value, const hashed_string& typeName, string_view valStr,
                                          const SerializeContext& context )
    {
        return parseTextValue( value, typeName, nullptr, valStr, context );
    }

    bool ValueSerializer::parseTextValue( MutableValueAddress value, const hashed_string& typeName, const ReflectValueBinding* pBinding, string_view valStr,
                                          const SerializeContext& context )
    {
        void* const                         pValPtr     = value.get();
        const ValueCodecs                   codecs      = findValueCodecs( typeName, pBinding, context );
        const SerializeContext::TextReadFn* pTextReader = codecs.findTextReader( context );
        if ( pTextReader != nullptr )
            return ( *pTextReader )( pValPtr, valStr );

        const EnumInfo* pEnumInfo = codecs.findEnum( context );
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
                SW_LOG_WARNING_ID( "Serialization.UnknownEnumerator", "'%#' is not a value of enum %# - the field keeps its current value", flagsText, pEnumInfo->_fullyQualifiedName.c_str() );
                return false;
            }
            pEnumInfo->writeValueToMemory( pValPtr, parsedValue );
            return true;
        }

        if ( const TypeInfo* pStructInfo = codecs.findStructType( context ) )
        {
            {
                string_view sv            = StringUtil::trim( valStr );
                const bool  bQuotedStruct = sv.size() >= 2 && sv.front() == '"' && sv.back() == '"';
                if ( bQuotedStruct )
                {
                    const string unescaped = JSONDocument::unescapeString( sv.substr( 1, sv.size() - 2 ) );
                    return NestedObjectCodec::readJSON( pValPtr, *pStructInfo, unescaped, context );
                }
                return NestedObjectCodec::readJSON( pValPtr, *pStructInfo, sv, context );
            }
        }

        return false;
    }

    bool ValueSerializer::canCarryValueType( hashed_string typeName, const SerializeContext& context )
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

    string ValueSerializer::formatValueText( ValueAddress value, const hashed_string& typeName, const SerializeContext& context )
    {
        StringBuilder<constant::buffer::kMax8192> ss;
        valueToText( ss, value, typeName, context );
        return ss.c_str();
    }
} // namespace sw
