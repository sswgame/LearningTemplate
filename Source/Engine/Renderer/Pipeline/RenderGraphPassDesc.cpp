#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"

#include "Engine/Graphics/RHI/RHIFormatInfo.h"

namespace sw
{
    uint32 RenderGraphPassDesc::collectColorOutputFormats( const vector<RenderPassAttachment>& listAttachment, RHIFormat* pOutFormat, uint32 capacity, uint32 fallbackCount ) const
    {
        if ( _listOutput.empty() )
            return fallbackCount;

        uint32 colorCount{ 0 };
        for ( const string& outName : _listOutput )
        {
            if ( colorCount >= capacity )
                break;
            if ( outName == kSwapchainOutputName )
            {
                if ( pOutFormat != nullptr )
                    pOutFormat[colorCount] = RHIFormat::Unknown;
                ++colorCount;
                continue;
            }
            for ( const RenderPassAttachment& attachment : listAttachment )
            {
                if ( attachment._name != outName )
                    continue;
                if ( RHIFormatUtil::isDepth( attachment._format ) == false )
                {
                    if ( pOutFormat != nullptr )
                        pOutFormat[colorCount] = attachment._format;
                    ++colorCount;
                }
                break;
            }
        }
        return colorCount;
    }
} // namespace sw
