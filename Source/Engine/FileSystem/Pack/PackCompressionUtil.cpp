#include "pch.h"

#include "Engine/FileSystem/Pack/PackCompressionUtil.h"

#include "Core/Compression/CompressionCodecRegistry.h"

#include "Engine/Compression/EngineCompressionCodecUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 활성 레지스트리가 없을 때 쓰는, 내장 둘 + 엔진 코덱을 모두 올린 레지스트리를 한 번 만들어 둡니다. */
        CompressionCodecRegistry& getEngineCodecRegistry()
        {
            struct EngineCodecRegistry
            {
                EngineCodecRegistry()
                    : _registry{}
                {
                    _registry.initialize();
                    _listCodecSubscription = EngineCompressionCodecUtil::registerAll( _registry );
                }

                CompressionCodecRegistry _registry;
                vector<Subscription>     _listCodecSubscription; ///< 레지스트리 뒤에 선언해 먼저 사라진다
            };
            static EngineCodecRegistry s_engineCodecRegistry;
            return s_engineCodecRegistry._registry;
        }
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
            pCodec = getEngineCodecRegistry().getCodec( codecType );
        return pCodec;
    }
} // namespace sw
