#include "pch.h"

#include "Engine/Content/Pack/PackCompressionUtil.h"

#include "Core/Compression/CompressionCodecRegistry.h"

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
        return pActive != nullptr ? pActive->getCodec( codecType ) : nullptr;
    }
} // namespace sw
