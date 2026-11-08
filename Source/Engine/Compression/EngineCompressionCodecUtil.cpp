#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"

#include "Engine/Compression/EngineCompressionCodecs.h"

namespace sw
{
    vector<Subscription> EngineCompressionCodecUtil::registerAll( CompressionCodecRegistry& inoutRegistry )
    {
        vector<Subscription> listSubscription;
        listSubscription.push_back( inoutRegistry.registerCodec( make_unique<LZ4CompressionCodec>() ) );
        listSubscription.push_back( inoutRegistry.registerCodec( make_unique<ZstdCompressionCodec>() ) );
        listSubscription.push_back( inoutRegistry.registerCodec( make_unique<ZlibCompressionCodec>() ) );
        return listSubscription;
    }
} // namespace sw
