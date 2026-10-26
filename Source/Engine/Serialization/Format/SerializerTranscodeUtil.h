/**
 * @file SerializerTranscodeUtil.h
 * @brief 리플렉션 객체 하나를 형식 사이(JSON · XML ↔ 바이너리)로 옮겨 씁니다.
 * @details 세 형식 직렬화기(`BinarySerializer` · `JSONSerializer` · `XMLSerializer`)를 모두 부르므로 형식 층(Format)에 둡니다.
 *          아래 층(`Base/SerializerUtil`)이 위 층 직렬화기를 부르지 않게 하려고 옮겼습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Serialization/Base/SerializeContext.h"

namespace sw
{
    struct TypeInfo;

    /** @brief JSON · XML 과 바이너리 사이를 임시 인스턴스 하나를 거쳐 옮겨 씁니다. */
    struct SW_API SerializerTranscodeUtil
    {
        /** @brief JSON 문자열을 바이너리 버퍼로 트랜스코딩합니다. 읽지 못하면 false 이고 그때 @p outBinary 는 건드리지 않습니다. */
        static bool transcodeJSONToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                           const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 바이너리 데이터를 JSON 문자열로 트랜스코딩합니다. 읽지 못하면 빈 문자열입니다. */
        static string transcodeBinaryToJSON( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty = false,
                                             const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief XML 문자열을 바이너리 버퍼로 트랜스코딩합니다. 읽지 못하면 false 이고 그때 @p outBinary 는 건드리지 않습니다. */
        static bool transcodeXMLToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                          const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 바이너리 데이터를 XML 문자열로 트랜스코딩합니다. 읽지 못하면 빈 문자열입니다. */
        static string transcodeBinaryToXML( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
                                            const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );
    };
} // namespace sw
