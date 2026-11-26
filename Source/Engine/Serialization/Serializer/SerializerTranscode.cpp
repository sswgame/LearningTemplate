#include "pch.h"

#include "Engine/Serialization/Serializer/SerializerTranscode.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/BinarySerializer.h"
#include "Engine/Serialization/Serializer/JSONSerializer.h"
#include "Engine/Serialization/Serializer/ScopedScratchInstance.h"
#include "Engine/Serialization/Serializer/XMLSerializer.h"

namespace sw
{
    namespace
    {
        struct SerializerTranscodeInternal
        {
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SerializerTranscode::transcodeJSONToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                     const SerializeContext& context )
    {
        return SerializerTranscodeInternal::transcodeTextToBinary( jsonStr, typeInfo, outBinary, context, [&]( void* pScratch )
        {
            return JSONSerializer::deserialize( pScratch, typeInfo, jsonStr, context );
        } );
    }

    string SerializerTranscode::transcodeBinaryToJSON( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty,
                                                       const SerializeContext& context )
    {
        return SerializerTranscodeInternal::transcodeBinaryToText( pData, dataSize, typeInfo, context, [&]( const void* pScratch )
        {
            const expected<string> text = bPretty ? JSONSerializer::serializePretty( pScratch, typeInfo, 4, context ) : JSONSerializer::serialize( pScratch, typeInfo, context );
            if ( text.has_value() )
                return *text;
            // 이 함수의 계약은 "못 쓰면 빈 글" 이다(XML 쪽과 같다) — 이유는 여기서 남긴다.
            SW_LOG_ERROR_ID( "SerializerTranscode.JSONNotWritable", "Binary data could not be transcoded to JSON: %# (%#)", text.error().message(), text.error().detail() );
            return string{};
        } );
    }

    bool SerializerTranscode::transcodeXMLToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                    const SerializeContext& context )
    {
        return SerializerTranscodeInternal::transcodeTextToBinary( xmlStr, typeInfo, outBinary, context, [&]( void* pScratch )
        {
            return XMLSerializer::deserialize( pScratch, typeInfo, xmlStr, context );
        } );
    }

    string SerializerTranscode::transcodeBinaryToXML( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
                                                      const SerializeContext& context )
    {
        return SerializerTranscodeInternal::transcodeBinaryToText( pData, dataSize, typeInfo, context, [&]( const void* pScratch )
        {
            return XMLSerializer::serialize( pScratch, typeInfo, context );
        } );
    }
} // namespace sw
