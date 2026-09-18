#include "pch.h"

#include "Engine/Compression/EngineCompressionCodecUtil.h"

#include "Core/Compression/CompressionCodecRegistry.h"

#include "Engine/Compression/Lz4CompressionCodec.h"
#include "Engine/Compression/ZlibCompressionCodec.h"
#include "Engine/Compression/ZstdCompressionCodec.h"

namespace sw
{
    void EngineCompressionCodecUtil::registerAll( CompressionCodecRegistry& inoutRegistry )
    {
        inoutRegistry.registerCodec( make_unique<Lz4CompressionCodec>() );
        inoutRegistry.registerCodec( make_unique<ZstdCompressionCodec>() );
        inoutRegistry.registerCodec( make_unique<ZlibCompressionCodec>() );
    }
} // namespace sw
