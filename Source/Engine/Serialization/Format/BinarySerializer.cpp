#include "pch.h"

#include "Engine/Serialization/Format/BinarySerializer.h"

#include "Core/Compression/CompressionStream.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Base/BinaryStream.h"
#include "Engine/Serialization/Base/ContainerVisitor.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        struct BinarySerializerInternal
        {
            /**
             * @brief 프로퍼티 중복 검사를 **비트마스크로 할 수 있는 한계**입니다. `uint64` 의 비트 수입니다.
             * @details 이보다 많으면 해시 집합으로 넘어간다. 엄격 역직렬화와 소프트 역직렬화 경로가 **같은 값을
             *          써야 한다.** 한쪽만 바꾸면 그 경로만 다른 자료구조로 중복을 세게 되고, 프로퍼티가
             *          64개 언저리인 타입에서만 갈리는 재현하기 어려운 차이가 된다.
             */
            static constexpr size_t kFastPropBitmaskThreshold = 64;

            /**
             * @brief 태그 스트림 머리(uint32)의 구성입니다. 아래 24비트는 프로퍼티 수, 위 8비트는 값 인코딩의 버전(`BinaryWireVersion`)입니다.
             * @details 위 8비트가 현재 버전이 아니면 읽지 않는다(버전이 없던 스트림은 0 이다. 프로퍼티가 1600만 개인 타입은 없다). 머리는 스트림마다 있어
             *          중첩 구조체와 소유 포인터 본문도 자기 버전을 말한다. 버전을 올리는 변경은 이 위치 하나만 고치면 모든 바이너리 경로(세이브, 스냅샷, 쿠킹 씬,
             *          복사, RPC)에 반영된다.
             */
            static constexpr uint32 kTaggedVersionShift = 24;
            static constexpr uint32 kTaggedCountMask    = ( 1u << kTaggedVersionShift ) - 1;

            /**
             * @brief 이 빌드가 읽을 수 있는 버전인지 확인합니다. 현재 버전 하나뿐입니다. 더 높은 버전은 앞선 빌드가 쓴 것이고, 낮은 버전을 올리는 단계는 없습니다
             *        (값을 다른 뜻으로 읽게 되기 때문입니다).
             */
            static bool isReadableWireVersion( uint32 wireVersion )
            {
                return wireVersion == static_cast<uint32>( kCurrentBinaryWireVersion );
            }

            /**
             * @brief 엄격한 읽기가 타입에 없는 필드를 건너뛸 때(`skipsUnknownProperties`) 알립니다. 타입과 필드마다 한 번만 알립니다.
             * @details 바이너리는 필드 이름이 아니라 해시를 싣는다. 오브젝트마다 알리면 같은 타입 천 개가 천 줄을 쓴다.
             */
            static void warnSkippedFieldOnce( const TypeInfo& typeInfo, uint32 nameHash )
            {
                // "한 번만"의 상태는 범위(테스트 케이스 · 프로세스)에 속한다(`LogOnce`). 함수 안 정적 집합이면 테스트 순서가 로그 판정을 바꾼다.
                const uint64 key = ( static_cast<uint64>( typeInfo._name.getHash() ) << 32 ) | nameHash;
                if ( LogOnce::tryClaim( LogOnceKind::SkippedField, key ) == false )
                    return;
                SW_LOG_WARNING( "%#: a saved field (name hash %#) is not in the type any more - skipped (once per field)", typeInfo._name.c_str(), nameHash );
            }

            /**
             * @brief 엄격한 읽기가 읽지 못한 필드를 건너뛸 때(`skipsUnreadableProperties`) 알립니다. 타입과 필드마다 한 번만 알립니다.
             * @details 현재 타입에 있는 필드인데 값을 읽지 못한 경우다(모르는 열거자 이름, 바뀐 타입). XML, JSON 과 같이 그 필드만 남기고 나머지를 읽는다.
             *          컴포넌트째 실패하면 소유 포인터 목록의 **뒤 컴포넌트까지** 버린다.
             */
            static void warnUnreadableFieldOnce( const TypeInfo& typeInfo, const PropertyInfo& prop )
            {
                const uint64 key = ( static_cast<uint64>( typeInfo._name.getHash() ) << 32 ) | prop.getNameHash();
                if ( LogOnce::tryClaim( LogOnceKind::UnreadableField, key ) == false )
                    return;
                SW_LOG_WARNING( "%#: saved field '%#' (%#) could not be read - it is skipped and the rest is read (once per field)", typeInfo._name.c_str(),
                                prop._name.c_str(), prop._typeName.c_str() );
            }

            static void pushOrphanVal( vector<SchemaOrphanValue>* pOutListOrphan, hashed_string name, uint32 nameHash, uint32 wireTypeHash, const uint8* pPayload, uint32 payloadSize,
                                       BinaryWireVersion wireVersion )
            {
                if ( pOutListOrphan == nullptr )
                    return;
                SchemaOrphanValue orphan;
                orphan._name         = name;
                orphan._nameHash     = nameHash != 0 ? nameHash : name.getHash();
                orphan._wireTypeHash = wireTypeHash;
                orphan._wireVersion  = wireVersion;
                orphan._listBinary.assign( pPayload, pPayload + payloadSize );
                pOutListOrphan->push_back( std::move( orphan ) );
            }

            /**
             * @brief 태그 스트림(개수, [태그 해시, 기록 타입 해시, 크기, 페이로드]…)을 읽어 인스턴스에 쓰는 **하나의** 루프입니다.
             * @details 엄격(`deserialize`)과 소프트(`deserializeSoft`)가 함께 쓴다. 둘이 다른 것은 정책 두 가지뿐이다.
             *          (1) 모르는 프로퍼티: 엄격은 `skipsUnknownProperties` 이면 건너뛰고 아니면 실패하며, 소프트는 orphan 으로 담는다.
             *          (2) 읽지 못한 프로퍼티(페이로드를 끝까지 읽지 못한 것 포함): 엄격은 `skipsUnreadableProperties` 이면 그 필드만 건너뛰고(XML, JSON 과 같다)
             *          아니면 실패하며, 소프트는 orphan 으로 담는다. 건너뛰거나 orphan 으로 담은 필드는 **읽기 전 값**이다(읽다 만 컨테이너 · 구조체를 되돌린다).
             *          되돌릴 수 없는 소유 포인터 목록(되돌리면 컴포넌트를 새로 만든다)이 읽다 말면 객체째 실패한다.
             *          두 플래그는 따로다. 혼합 빌드 사이의 메시지는 (1) 만 켜서 새 빌드가 더한 필드는 건너뛰고 모르는 열거자는 거절한다. 중첩 구조체도
             *          같은 컨텍스트로 이 루프를 다시 지나므로(`SerializerUtil::deserializeValueBinary`) 모든 깊이에서 정책이 같다.
             *          기록 타입이 다르면 둘 다 이관(`tryCoerceBinaryPayload`)으로 간다. 기록 타입을 아는 스칼라는 이관이 값으로
             *          옮기고, 나머지는 제 타입으로 끝까지 읽히는지부터 본다. 이관이 안 되면 읽지 못한 프로퍼티다. 제 타입으로 "읽힌 만큼" 받지 않는다
             *          (`int32` 0 을 `vector<int32>` 로 읽으면 원소 수 0 으로 성공해 값이 조용히 빈다).
             *          신뢰할 수 없는 스트림의 경계 검사가 이 안에 있다. 사본이 하나여야 그 검사가 한쪽에서만 빠지는 일이 없다.
             * @return 스트림이 깨졌거나 엄격 읽기가 받지 않는 필드가 있으면 `FieldFailed`, 금지된 필드가 있거나(안쪽 칸 포함) 읽지 못한 필드를 되돌리지
             *         못하면 `ObjectRejected` 입니다. 소프트 읽기의 맨 바깥은 앞을 `NotParsed`, 뒤를 `Rejected` 로 옮긴다(`deserializeSoft`).
             */
            [[nodiscard]] static FieldReadResult deserializeTagged( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                                    const SerializeContext& context, vector<SchemaOrphanValue>* pOutListOrphan, bool bStrict )
            {
                BinaryStreamReader reader( pData, dataSize );
                uint32             header{ 0 };
                if ( reader.read( header ) == false )
                    return FieldReadResult::FieldFailed;
                // 머리가 버전을 말한다. 이 스트림 안의 값(컨테이너 원소, 맵 키 포함)은 그 버전으로 읽는다. 현재 버전이 아니면 읽지 않는다.
                const uint32 wireVersionNumber = header >> kTaggedVersionShift;
                if ( isReadableWireVersion( wireVersionNumber ) == false )
                {
                    SW_LOG_WARNING( "%#: binary stream is wire version %# but this build reads up to %# - not read", typeInfo._name.c_str(), wireVersionNumber,
                                    static_cast<uint32>( kCurrentBinaryWireVersion ) );
                    return FieldReadResult::FieldFailed;
                }
                const BinaryWireVersion wireVersion = static_cast<BinaryWireVersion>( wireVersionNumber );
                const uint32            propCount   = header & kTaggedCountMask;

                const vector<PropertyInfo>& listProp = typeInfo.getPropertiesWithBase();
                const size_t                numProps = listProp.size();
                uint64                      matchedBitmask{ 0 };
                unordered_set<uint32>       uniqueMatchedPropHashes;
                if ( numProps > kFastPropBitmaskThreshold )
                    uniqueMatchedPropHashes.reserve( numProps );
                // 읽지 못한 필드를 넘기고 이어 읽는 읽기(소프트, 오브젝트 상태의 엄격)는 그 필드를 **읽기 전 값**으로 남긴다. 컨테이너 · 구조체는 원소와 필드를
                // 채운 뒤 끝에서 실패하므로 읽다 만 값이 남는다. 그래서 읽기 전에 값을 바이트로 담아 두고 실패하면 되돌린다. 실패하면 객체째 버리는 엄격 읽기는 담지 않는다.
                const bool           bContinuesAfterUnreadable = bStrict == false || context.skipsUnreadableProperties();
                PropertyReadRollback rollback;

                for ( uint32 propIndex = 0; propIndex < propCount; ++propIndex )
                {
                    uint32     tagHash{ 0 };
                    uint32     wireTypeHash{ 0 };
                    uint32     payloadSize{ 0 };
                    const bool bTruncatedTag = reader.read( tagHash ) == false || reader.read( wireTypeHash ) == false || reader.read( payloadSize ) == false;
                    if ( bTruncatedTag )
                        return FieldReadResult::FieldFailed;

                    const size_t payloadStart = reader.getOffset();
                    if ( payloadStart + payloadSize > dataSize )
                        return FieldReadResult::FieldFailed;

                    // 지금 이름을 먼저, 없을 때만 옛 이름(Alias)을 본다. Transient 칸을 가리키는 태그는 모르는 칸과 같다.
                    const size_t        matchedIndex = SerializerUtil::findPropertyIndexByNameHash( listProp, tagHash );
                    const bool          bMatched     = matchedIndex < numProps && listProp[matchedIndex]._metadata->_bTransient == SW_FALSE;
                    const PropertyInfo* pTargetProp  = bMatched ? &listProp[matchedIndex] : nullptr;

                    if ( pTargetProp == nullptr )
                    {
                        if ( bStrict && context.skipsUnknownProperties() == false )
                            return FieldReadResult::FieldFailed;
                        if ( bStrict == false )
                            pushOrphanVal( pOutListOrphan, {}, tagHash, wireTypeHash, pData + payloadStart, payloadSize, wireVersion );
                        else
                            warnSkippedFieldOnce( typeInfo, tagHash );
                        reader.skip( payloadSize );
                        continue;
                    }

                    const PropertyInfo& prop = *pTargetProp;
                    if ( numProps <= kFastPropBitmaskThreshold )
                        matchedBitmask |= ( 1ULL << matchedIndex );
                    else
                        uniqueMatchedPropHashes.insert( prop.getNameHash() );

                    // 저작 파일의 런타임 상태는 로드 오류다(XML · JSON 과 같다).
                    if ( SerializerUtil::isForbiddenInFile( prop, context ) )
                    {
                        SerializerUtil::reportForbiddenProperty( typeInfo, prop );
                        return FieldReadResult::ObjectRejected;
                    }
                    // 이 목적이 다루지 않는 필드(옵트인 세이브의 나머지)는 읽지 않는다. 현재 값을 덮지 않는다(쓰기도 적지 않는다).
                    if ( SerializerUtil::shouldSerializeProperty( typeInfo, prop, context ) == false )
                    {
                        reader.skip( payloadSize );
                        continue;
                    }

                    // 값을 담지 못하면 실패해도 되돌릴 수 없다(코덱 결함). 기본값으로 넘어가지 않고 객체째 실패다.
                    if ( bContinuesAfterUnreadable && rollback.keepValueBeforeRead( typeInfo, prop, ObjectAddress{ pInstance }, context ) == false )
                        return FieldReadResult::ObjectRejected;

                    // **기록 타입을 같이 넘긴다.** 태그가 그것을 가지고 있는데 넘기지 않으면 POD -> string 이관이 크기로만
                    // 타입을 짐작한다(정수와 실수를 가르지 못한다).
                    const bool      bWireMismatch = ( wireTypeHash != 0 && wireTypeHash != prop._typeName.getHash() );
                    FieldReadResult fieldResult   = FieldReadResult::FieldFailed;
                    if ( bWireMismatch )
                    {
                        const hashed_string wireTypeName = findWireTypeName( wireTypeHash );
                        // 기록 타입을 모르면(지운 enum, 지운 타입) 바이트의 뜻을 모른다. 크기로 짐작해 읽지 않는다(지운 enum 의 열거자 이름 해시
                        // 4 바이트가 int32 로 바뀐 필드에 그대로 읽히게 된다).
                        // 프로퍼티 단위로 넘긴다 — 값 주소를 여기서 꺼내면 비트필드는 그 바이트 주소라 bool 이 같은 바이트의 다른 플래그까지 덮는다.
                        if ( wireTypeName.empty() == false )
                            fieldResult = tryCoercePropertyBinary( prop, MutableObjectAddress{ pInstance }, pData + payloadStart, payloadSize, context, wireTypeName, wireVersion );
                    }
                    else
                    {
                        // 기록 타입이 같은데 끝까지 읽히지 않으면 손상이다. 소프트 읽기도 이관으로 짐작하지 않고 orphan 으로 넘긴다(이관은 크기로 짐작해 4 바이트 페이로드를 수의 텍스트로 문자열에 넣는다).
                        fieldResult = SerializerUtil::applyPropertyBinary( prop, MutableObjectAddress{ pInstance }, pData + payloadStart, payloadSize, context, wireVersion );
                    }

                    switch ( fieldResult )
                    {
                        case FieldReadResult::Read:
                        {
                            break;
                        }
                        case FieldReadResult::FieldFailed:
                        {
                            // 엄격한 읽기도 읽지 못한 필드를 받는 컨텍스트(오브젝트 상태)이면 **이 필드만** 건너뛴다. 크기를 알아 다음 필드 위치는 맞다. 모르는 열거자 하나가
                            // 컴포넌트를, 그래서 그 뒤 컴포넌트까지 버리던 경로다. 혼합 빌드 사이의 메시지는 받지 않는다(그 필드가 읽기 전 값으로 남는다).
                            if ( bContinuesAfterUnreadable == false )
                                return FieldReadResult::FieldFailed;
                            // 읽다 만 값을 읽기 전 값으로 되돌린다. 되돌릴 수 없으면(소유 포인터 목록, 코덱 결함) 객체째 실패다.
                            if ( rollback.restoreValueBeforeRead( MutableObjectAddress{ pInstance }, context ) == false )
                                return FieldReadResult::ObjectRejected;
                            if ( bStrict )
                                warnUnreadableFieldOnce( typeInfo, prop );
                            else
                                pushOrphanVal( pOutListOrphan, prop._name, tagHash, wireTypeHash, pData + payloadStart, payloadSize, wireVersion );
                            break;
                        }
                        case FieldReadResult::ObjectRejected:
                        {
                            // 안쪽(구조체 칸, 컨테이너 원소인 구조체 · 컴포넌트)의 금지된 필드다. 이 필드만 넘기면 그것이 orphan 으로 넘어간다 — 객체째 실패다.
                            return FieldReadResult::ObjectRejected;
                        }
                    }

                    reader.skip( payloadSize );
                }

                // 스트림에 없던 프로퍼티는 기본값으로 채운다. 두 경로가 같은 규칙이다.
                for ( size_t propIdx = 0; propIdx < numProps; ++propIdx )
                {
                    bool bMatched = false;
                    if ( numProps <= kFastPropBitmaskThreshold )
                        bMatched = ( matchedBitmask & ( 1ULL << propIdx ) ) != 0;
                    else
                        bMatched = uniqueMatchedPropHashes.find( listProp[propIdx].getNameHash() ) != uniqueMatchedPropHashes.end();
                    // 이 목적이 다루지 않는 필드(Transient, 저작의 RuntimeState, 옵트인 세이브의 나머지)는 현재 값 그대로다. 기본값으로 되돌리지 않는다.
                    const bool bOutsidePurpose = SerializerUtil::shouldSerializeProperty( typeInfo, listProp[propIdx], context ) == false;
                    if ( bMatched == false && bOutsidePurpose == false )
                        SerializerUtil::applyPropertyDefault( listProp[propIdx], MutableObjectAddress{ pInstance }, context );
                }
                // 선언된 범위는 부르는 쪽(`deserializeNested` · `deserializeSoft`)이 읽기 공통 끝으로 본다 — 소프트 읽기가 "스트림이 깨짐"과 "범위 밖 값"을 가르게.
                return FieldReadResult::Read;
            }

            /**
             * @brief `serializeRaw` 가 쓴 바이트를 `codecType` 으로 압축해 `outListBuffer` 에 담습니다. 압축 버전 직렬화 두 함수가 함께 씁니다.
             * @return 인스턴스가 없거나, 쓴 바이트가 없거나, 압축에 실패하면 false 입니다.
             */
            template <typename SerializeRawFunc>
            [[nodiscard]] static bool serializeThenCompress( const void* pInstance, vector<uint8>& outListBuffer, CompressionCodecType codecType,
                                                             SerializeRawFunc&& serializeRaw )
            {
                outListBuffer.clear();
                if ( pInstance == nullptr )
                    return false;

                vector<uint8> listRawBinary;
                if ( serializeRaw( listRawBinary ) == false || listRawBinary.empty() )
                    return false;

                return CompressionStream::compressBuffer( listRawBinary.data(), listRawBinary.size(), outListBuffer, codecType );
            }

            /** @brief 압축을 풀어 나온 바이트를 `deserializeRaw` 로 읽습니다. 압축 버전 역직렬화 두 함수가 함께 씁니다. */
            template <typename DeserializeRawFunc>
            static bool decompressThenDeserialize( void* pInstance, const uint8* pData, size_t dataSize, DeserializeRawFunc&& deserializeRaw )
            {
                const bool bInvalidCompressedInput = pInstance == nullptr || pData == nullptr || dataSize == 0;
                if ( bInvalidCompressedInput )
                    return false;

                vector<uint8> listRawBinary;
                const bool    bDecompressOk = CompressionStream::decompressBuffer( pData, dataSize, listRawBinary );
                if ( bDecompressOk == false || listRawBinary.empty() )
                    return false;

                return deserializeRaw( listRawBinary.data(), listRawBinary.size() );
            }

            /**
             * @brief 버퍼 버전으로 쓴 바이트를 아카이브 끝에 옮깁니다. Archive 버전 직렬화가 모두 씁니다.
             * @return @p bWritten 이 거짓이면(버퍼 쓰기가 중간에 멈춤) 아무것도 옮기지 않고 아카이브를 오류 상태로 두며 false 입니다.
             */
            [[nodiscard]] static bool writeBufferToArchive( bool bWritten, const vector<uint8>& buffer, Archive& outArchive )
            {
                if ( bWritten == false )
                {
                    outArchive.setError();
                    return false;
                }
                if ( buffer.empty() == false )
                    outArchive.writeBytes( buffer.data(), buffer.size() );
                return true;
            }

            /** @brief 스트림 하나의 바이트 수를 값을 읽지 않고 셉니다. 잘렸거나 구조가 틀리면 false 입니다. */
            using MeasureStreamFn = bool ( * )( const uint8* pData, size_t dataSize, size_t& outStreamSize );

            /** @brief 태그 스트림입니다. 머리 뒤에 필드마다 이름 해시, 타입 해시, 값 크기, 값이 이어집니다. */
            static bool measureTaggedStream( const uint8* pData, size_t dataSize, size_t& outStreamSize )
            {
                BinaryStreamReader reader( pData, dataSize );
                uint32             header{ 0 };
                if ( reader.read( header ) == false )
                    return false;
                const uint32 propCount = header & kTaggedCountMask;
                for ( uint32 propIndex = 0; propIndex < propCount; ++propIndex )
                {
                    uint32     tagHash{ 0 };
                    uint32     wireTypeHash{ 0 };
                    uint32     payloadSize{ 0 };
                    const bool bTruncatedTagHeader = reader.read( tagHash ) == false || reader.read( wireTypeHash ) == false || reader.read( payloadSize ) == false;
                    if ( bTruncatedTagHeader )
                        return false;
                    if ( reader.skip( payloadSize ) == false )
                        return false;
                }
                outStreamSize = reader.getOffset();
                return true;
            }

            /** @brief 버전 스트림입니다. 버전(uint32) 뒤에 태그 스트림이 이어집니다. */
            static bool measureVersionedStream( const uint8* pData, size_t dataSize, size_t& outStreamSize )
            {
                constexpr size_t kVersionSize = sizeof( uint32 );
                size_t           bodySize{ 0 };
                if ( dataSize < kVersionSize || measureTaggedStream( pData + kVersionSize, dataSize - kVersionSize, bodySize ) == false )
                    return false;
                outStreamSize = kVersionSize + bodySize;
                return true;
            }

            /** @brief 압축 스트림입니다. `CompressionHeader` 가 압축된 본문 크기를 가집니다. */
            static bool measureCompressedStream( const uint8* pData, size_t dataSize, size_t& outStreamSize )
            {
                CompressionHeader header{};
                if ( dataSize < sizeof( header ) )
                    return false;
                Memory::copy( &header, pData, sizeof( header ) );
                if ( header._magic != CompressionHeader::kMagic || header._compressedSize > dataSize - sizeof( header ) )
                    return false;
                outStreamSize = sizeof( header ) + static_cast<size_t>( header._compressedSize );
                return true;
            }

            /**
             * @brief 아카이브의 현재 위치에서 스트림 하나를 `deserializeBytes` 로 읽고, 성공하면 읽기 위치를 그 끝으로 옮깁니다. Archive 버전 역직렬화가 모두 씁니다.
             * @details 스트림의 끝은 @p measure 가 값을 읽지 않고 센다. 그래서 같은 Archive 에 이어 쓴 객체와 값을 차례로 읽을 수 있다.
             *          오류 상태이거나 남은 바이트가 없거나 스트림이 잘렸으면 false 이고 위치는 그대로다.
             */
            template <typename DeserializeBytesFunc>
            [[nodiscard]] static bool deserializeArchiveStream( Archive& inArchive, MeasureStreamFn measure, DeserializeBytesFunc&& deserializeBytes )

            {
                const bool bInvalidArchive = inArchive.isError() || inArchive.getData() == nullptr || inArchive.getOffset() >= inArchive.getSize();
                if ( bInvalidArchive )
                    return false;
                const uint8* pStream    = inArchive.getData() + inArchive.getOffset();
                size_t       streamSize = 0;
                if ( measure( pStream, static_cast<size_t>( inArchive.getSize() - inArchive.getOffset() ), streamSize ) == false )
                    return false;
                if ( deserializeBytes( pStream, streamSize ) == false )
                    return false;
                return inArchive.setOffset( inArchive.getOffset() + streamSize );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "BinarySerializer" );

    bool BinarySerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                                      const SerializeContext& context )
    {
        BinaryStreamWriter          writer( outListBuffer );
        const vector<PropertyInfo>& listProp = typeInfo.getPropertiesWithBase();
        uint32                      propCount{ 0 };
        for ( const PropertyInfo& prop : listProp )
        {
            if ( SerializerUtil::shouldSerializeProperty( typeInfo, prop, context ) == false )
                continue;
            ++propCount;
        }
        outListBuffer.reserve( outListBuffer.size() + sizeof( uint32 ) + static_cast<size_t>( propCount ) * 32 );
        SW_ASSERT( propCount <= BinarySerializerInternal::kTaggedCountMask );
        // 머리의 위 8비트가 버전이다(`kTaggedVersionShift` 설명 참고). 읽는 쪽이 이 스트림의 enum 을 이름으로 읽을지 값으로 읽을지 여기서 안다.
        writer.write( propCount | ( static_cast<uint32>( kCurrentBinaryWireVersion ) << BinarySerializerInternal::kTaggedVersionShift ) );

        for ( const PropertyInfo& prop : listProp )
        {
            if ( SerializerUtil::shouldSerializeProperty( typeInfo, prop, context ) == false )
                continue;

            uint32 hashVal = prop.getNameHash();
            writer.write( hashVal );

            uint32 typeHash = prop._typeName.getHash();
            writer.write( typeHash );

            size_t sizeHeaderPos = writer.getOffset();
            uint32 dummySize{ 0 };
            writer.write( dummySize );

            size_t payloadStart = writer.getOffset();

            const bool bWritten = SerializerUtil::writePropertyBinary( prop, ObjectAddress{ pInstance }, outListBuffer, context );
            if ( bWritten == false )
            {
                // 쓰는 순간 멈춘다. 계속 쓰면 세이브는 써지고 로드에서 이 값이 빠진다. 알림은 호출마다 한다(타입마다 한 번이면 테스트 순서에 따라 결과가 달라진다).
                SW_LOG_ERROR( "%#::%# (%#) cannot be written: it or a value inside it has no binary, enum, struct or text handler - the object is not written",
                              typeInfo._name.c_str(), prop._name.c_str(), prop._typeName.c_str() );
                return false;
            }

            uint32 payloadSize = static_cast<uint32>( writer.getOffset() - payloadStart );
            writer.writeAt( sizeHeaderPos, payloadSize );
        }
        return true;
    }

    bool BinarySerializer::deserialize( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                        const SerializeContext& context )
    {
        // 맨 바깥 객체다. 바깥에 이어 읽을 읽기가 없으므로 필드 실패와 객체 실패는 같은 실패다.
        return deserializeNested( pInstance, typeInfo, pData, dataSize, context ) == FieldReadResult::Read;
    }

    FieldReadResult BinarySerializer::deserializeNested( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                         const SerializeContext& context )
    {
        const FieldReadResult taggedResult = BinarySerializerInternal::deserializeTagged( pInstance, typeInfo, pData, dataSize, context, nullptr, true );
        switch ( taggedResult )
        {
            case FieldReadResult::Read:
                // 선언된 범위는 읽기 공통 끝에서 본다(JSON · XML 과 같다). 범위 밖 값은 성공으로 돌아오지 않는다.
                return SerializerUtil::isWithinDeclaredRanges( typeInfo, ObjectAddress{ pInstance } ) ? FieldReadResult::Read : FieldReadResult::FieldFailed;
            case FieldReadResult::FieldFailed:
            case FieldReadResult::ObjectRejected:
                return taggedResult;
        }
        return FieldReadResult::ObjectRejected;
    }

    SoftReadResult BinarySerializer::deserializeSoft( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                      vector<SchemaOrphanValue>* pOutOrphans, const SerializeContext& context )
    {
        const bool bInvalidDeserializeInput = pInstance == nullptr || pData == nullptr || dataSize == 0;
        if ( bInvalidDeserializeInput )
            return SoftReadResult::NotParsed;
        // 소프트 읽기의 태그 루프는 모르는 · 읽지 못한 칸을 orphan 으로 넘기므로, 필드 실패는 스트림이 깨졌을 때뿐이다.
        // 객체째 실패(금지된 필드, 되돌리지 못한 필드)는 스트림을 읽은 뒤의 거절이다(XML · JSON 의 `Rejected` 와 같다).
        switch ( BinarySerializerInternal::deserializeTagged( pInstance, typeInfo, pData, dataSize, context, pOutOrphans, false ) )
        {
            case FieldReadResult::Read:
                break;
            case FieldReadResult::FieldFailed:
                return SoftReadResult::NotParsed;
            case FieldReadResult::ObjectRejected:
                return SoftReadResult::Rejected;
        }
        return SerializerUtil::isWithinDeclaredRanges( typeInfo, ObjectAddress{ pInstance } ) ? SoftReadResult::Read : SoftReadResult::Rejected;
    }

    bool BinarySerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
                                               const SerializeContext& context )
    {
        BinaryStreamWriter writer( outListBuffer );
        writer.write( version );
        return serialize( pInstance, typeInfo, outListBuffer, context );
    }

    bool BinarySerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
                                                 uint32 currentVersion, const TypeInfo* pLegacyTypeInfo,
                                                 const SerializeContext& context )
    {
        BinaryStreamReader reader( pData, dataSize );
        if ( reader.read( outVersion ) == false )
            return false;

        const uint8* pBody    = pData + reader.getOffset();
        const size_t bodySize = dataSize - reader.getOffset();

        // 절차는 JSON, XML 과 공통이다(`runVersionedDeserialize`). 바이너리만 다른 것은 두 가지다.
        // 버전을 **본문 앞에서 이미 읽었고**(위 `reader.read`), orphan 이 하나라도 있으면 이관 없이는
        // 거절한다. 바이너리는 손으로 고치는 포맷이 아니라, 모르는 필드가 있다는 것은 스키마가 바뀌었다는 뜻이기 때문이다.
        // 지운 필드를 버려도 되는 호출부는 컨텍스트로 그렇게 말한다(`setAllowUnknownProperties`, 오브젝트 상태). 버전이 다르면 그래도 거절한다.
        // orphan 은 모르는 이름과 읽지 못한 아는 필드를 함께 담으므로 두 플래그가 모두 켜졌을 때만 버린다. 모르는 이름만 버리는 읽기는 `deserializeExactVersion` 이다.
        const bool               bIgnoreOrphan = context.skipsUnknownProperties() && context.skipsUnreadableProperties();
        const SchemaOrphanPolicy orphanPolicy  = bIgnoreOrphan ? SchemaOrphanPolicy::Ignore : SchemaOrphanPolicy::Reject;
        return runVersionedDeserialize(
            outVersion, MutableObjectAddress{ pInstance }, typeInfo, currentVersion, pLegacyTypeInfo, context,
            SchemaVersionSource::Stream, orphanPolicy,
            SW_DELEGATE_LAMBDA( SoftDeserializeFn,
                                [&]( const MutableObjectAddress target, const TypeInfo& targetType, vector<SchemaOrphanValue>& listOrphan, uint32& ) -> bool
        {
            return deserializeSoft( target.get(), targetType, pBody, bodySize, &listOrphan, context ) == SoftReadResult::Read;
        } ) );
    }

    BinaryExactVersionResult BinarySerializer::deserializeExactVersion( uint32 expectedVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData,
                                                                        size_t dataSize, uint32& outReadVersion, const SerializeContext& context )
    {
        outReadVersion           = 0;
        const bool bInvalidInput = pInstance == nullptr || pData == nullptr;
        if ( bInvalidInput )
            return BinaryExactVersionResult::Unreadable;
        BinaryStreamReader reader( pData, dataSize );
        if ( reader.read( outReadVersion ) == false )
            return BinaryExactVersionResult::Unreadable;
        if ( outReadVersion != expectedVersion )
            return BinaryExactVersionResult::VersionMismatch;
        const size_t bodyOffset = reader.getOffset();
        if ( deserialize( pInstance, typeInfo, pData + bodyOffset, dataSize - bodyOffset, context ) == false )
            return BinaryExactVersionResult::Unreadable;
        return BinaryExactVersionResult::Read;
    }

    bool BinarySerializer::serializeCompressed( const void*             pInstance,
                                                const TypeInfo&         typeInfo,
                                                vector<uint8>&          outListBuffer,
                                                CompressionCodecType    codecType,
                                                const SerializeContext& context )
    {
        return BinarySerializerInternal::serializeThenCompress( pInstance, outListBuffer, codecType, [&]( vector<uint8>& outListRaw ) -> bool
        {
            return serialize( pInstance, typeInfo, outListRaw, context );
        } );
    }

    bool BinarySerializer::deserializeCompressed( void*                   pInstance,
                                                  const TypeInfo&         typeInfo,
                                                  const uint8*            pData,
                                                  size_t                  dataSize,
                                                  const SerializeContext& context )
    {
        return BinarySerializerInternal::decompressThenDeserialize( pInstance, pData, dataSize, [&]( const uint8* pRaw, size_t rawSize )
        {
            return deserialize( pInstance, typeInfo, pRaw, rawSize, context );
        } );
    }

    bool BinarySerializer::serialize( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                      const SerializeContext& context )
    {
        vector<uint8> buffer;
        const bool    bWritten = serialize( pInstance, typeInfo, buffer, context );
        return BinarySerializerInternal::writeBufferToArchive( bWritten, buffer, outArchive );
    }

    bool BinarySerializer::deserialize( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                        const SerializeContext& context )
    {
        return BinarySerializerInternal::deserializeArchiveStream( inArchive, &BinarySerializerInternal::measureTaggedStream,
                                                                   [&]( const uint8* pData, size_t dataSize )
        {
            return deserialize( pInstance, typeInfo, pData, dataSize, context );
        } );
    }

    bool BinarySerializer::serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                               const SerializeContext& context )
    {
        vector<uint8> buffer;
        const bool    bWritten = serializeVersioned( version, pInstance, typeInfo, buffer, context );
        return BinarySerializerInternal::writeBufferToArchive( bWritten, buffer, outArchive );
    }

    bool BinarySerializer::deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                                 uint32          currentVersion,
                                                 const TypeInfo* pLegacyTypeInfo, const SerializeContext& context )
    {
        return BinarySerializerInternal::deserializeArchiveStream( inArchive, &BinarySerializerInternal::measureVersionedStream,
                                                                   [&]( const uint8* pData, size_t dataSize )
        {
            return deserializeVersioned( outVersion, pInstance, typeInfo, pData, dataSize, currentVersion, pLegacyTypeInfo, context );
        } );
    }

    bool BinarySerializer::serializeCompressed( const void*             pInstance,
                                                const TypeInfo&         typeInfo,
                                                Archive&                outArchive,
                                                CompressionCodecType    codecType,
                                                const SerializeContext& context )
    {
        vector<uint8> buffer;
        const bool    bWritten = serializeCompressed( pInstance, typeInfo, buffer, codecType, context );
        return BinarySerializerInternal::writeBufferToArchive( bWritten, buffer, outArchive );
    }

    bool BinarySerializer::deserializeCompressed( void*                   pInstance,
                                                  const TypeInfo&         typeInfo,
                                                  Archive&                inArchive,
                                                  const SerializeContext& context )
    {
        return BinarySerializerInternal::deserializeArchiveStream( inArchive, &BinarySerializerInternal::measureCompressedStream,
                                                                   [&]( const uint8* pData, size_t dataSize )
        {
            return deserializeCompressed( pInstance, typeInfo, pData, dataSize, context );
        } );
    }
} // namespace sw
