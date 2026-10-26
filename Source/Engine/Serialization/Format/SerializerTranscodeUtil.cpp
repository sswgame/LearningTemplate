#include "pch.h"

#include "Engine/Serialization/Format/SerializerTranscodeUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    namespace
    {
        struct SerializerTranscodeUtilInternal
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
    bool SerializerTranscodeUtil::transcodeJSONToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                         const SerializeContext& context )
    {
        return SerializerTranscodeUtilInternal::transcodeTextToBinary( jsonStr, typeInfo, outBinary, context, [&]( void* pScratch )
        {
            return JSONSerializer::deserialize( pScratch, typeInfo, jsonStr, context );
        } );
    }

    string SerializerTranscodeUtil::transcodeBinaryToJSON( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty,
                                                           const SerializeContext& context )
    {
        return SerializerTranscodeUtilInternal::transcodeBinaryToText( pData, dataSize, typeInfo, context, [&]( const void* pScratch )
        {
            return bPretty ? JSONSerializer::serializePretty( pScratch, typeInfo, 4, context ) : JSONSerializer::serialize( pScratch, typeInfo, context );
        } );
    }

    bool SerializerTranscodeUtil::transcodeXMLToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                        const SerializeContext& context )
    {
        return SerializerTranscodeUtilInternal::transcodeTextToBinary( xmlStr, typeInfo, outBinary, context, [&]( void* pScratch )
        {
            return XMLSerializer::deserialize( pScratch, typeInfo, xmlStr, context );
        } );
    }

    string SerializerTranscodeUtil::transcodeBinaryToXML( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
                                                          const SerializeContext& context )
    {
        return SerializerTranscodeUtilInternal::transcodeBinaryToText( pData, dataSize, typeInfo, context, [&]( const void* pScratch )
        {
            return XMLSerializer::serialize( pScratch, typeInfo, context );
        } );
    }
} // namespace sw
