#include "pch.h"

#include "Engine/Resource/PackCompressionUtil.h"

#include "Core/Compression/CompressionCodecRegistry.h"

#include "Engine/Compression/EngineCompressionCodecUtil.h"

namespace sw
{
    namespace
    {
        /**
         * @struct PackCompressionUtilInternal
         * @brief 활성 등록부가 없을 때 쓰는, 엔진 코덱을 모두 올린 등록부입니다.
         */
        struct PackCompressionUtilInternal
        {
            /** @brief 내장 둘 + 엔진 코덱을 올린 등록부를 한 번 만들어 둡니다. */
            static CompressionCodecRegistry& getEngineCodecRegistry()
            {
                struct EngineCodecRegistry
                {
                    EngineCodecRegistry()
                        : _registry{}
                    {
                        _registry.initialize();
                        EngineCompressionCodecUtil::registerAll( _registry );
                    }

                    CompressionCodecRegistry _registry;
                };
                static EngineCodecRegistry s_engineCodecRegistry;
                return s_engineCodecRegistry._registry;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PackCompressionUtil::findCodecType( PackCompressionType packType, CompressionCodecType& outCodecType )
    {
        for ( const CodecMapping& mapping : kArrCodecMapping )
        {
            if ( mapping._packType != packType )
                continue;
            outCodecType = mapping._codecType;
            return true;
        }
        return false;
    }

    ICompressionCodec* PackCompressionUtil::findCodec( PackCompressionType packType )
    {
        CompressionCodecType codecType{ CompressionCodecType::None };
        if ( findCodecType( packType, codecType ) == false )
            return nullptr;

        CompressionCodecRegistry* pActive = CompressionCodecRegistry::getActive();
        ICompressionCodec*        pCodec  = ( pActive != nullptr ) ? pActive->getCodec( codecType ) : nullptr;
        if ( pCodec == nullptr )
            pCodec = PackCompressionUtilInternal::getEngineCodecRegistry().getCodec( codecType );
        return pCodec;
    }
} // namespace sw
