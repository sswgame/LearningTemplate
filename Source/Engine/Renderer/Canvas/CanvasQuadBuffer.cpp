#include "pch.h"

#include "Engine/Renderer/Canvas/CanvasQuadBuffer.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    SW_LOG_CALLER( "Canvas" );

    namespace
    {
        struct CanvasQuadBufferInternal
        {
            /** @brief 사각형 버퍼가 모자라면 이 배율로 키운다(프레임마다 조금씩 늘 때 다시 만들기를 줄인다). */
            static constexpr float32 kQuadBufferGrowth = 1.5f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    CanvasQuadBuffer::CanvasQuadBuffer()
        : _listAnchorScratch{}
        , _listUploadedAnchor{}
        , _listQuadScratch{}
        , _listTargetQuadBase{}
        , _quadBuffer{}
        , _uploadedRevision{ 0 }
        , _uploadedQuadCount{ 0 }
        , _mainQuadBase{ 0 }
    {
    }

    CanvasQuadBuffer::~CanvasQuadBuffer() = default;

    void CanvasQuadBuffer::upload( IRHIDevice& device, const CanvasFrameData& frame, bool bNeedsUAV )
    {
        // 앞에 화면 앵커, 그 뒤에 주 출력, 그 뒤에 대상들을 잇는다. 내용 서명(주 출력 · 대상의 내용 번호)과 앵커 수가 같고 버퍼가 그대로면
        // 사각형은 올리지 않고 바뀐 앵커만 올린다(번호 0 은 "모른다" 라 늘 올린다).
        const uint32 anchorCount = static_cast<uint32>( frame._listAnchor.size() );
        _mainQuadBase            = anchorCount;
        uint32 quadCount         = anchorCount + static_cast<uint32>( frame._mainOutput._listQuad.size() );
        uint64 signature         = frame._contentRevision;
        bool   bKnown            = frame._contentRevision != 0;
        _listTargetQuadBase.clear();
        for ( const CanvasTargetDrawList& target : frame._listTarget )
        {
            _listTargetQuadBase.push_back( quadCount );
            quadCount += static_cast<uint32>( target._list._listQuad.size() );
            bKnown    = bKnown && target._contentRevision != 0;
            signature = HashUtil::mix64( signature ^ ( target._contentRevision * HashUtil::kGoldenRatio64 ) ^ target._targetPath.getHash() );
        }
        if ( quadCount == 0 )
            return;
        // 마커 투영 컴퓨트가 앵커 원소를 쓰려면 이 버퍼에 UAV 가 있어야 한다. 마커가 처음 올라온 프레임에 UAV 없이 만들어진 버퍼는 다시 만들고 내용을 다시 올린다.
        if ( bNeedsUAV && _quadBuffer.isValid() && _quadBuffer._uav == kInvalidDescriptorIndex )
        {
            _quadBuffer.release( &device );
            _uploadedRevision = 0;
        }
        const RHIBufferUsage quadUsage = bNeedsUAV ? ( RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess ) : RHIBufferUsage::ShaderResource;
        signature                      = signature == 0 ? 1 : signature;
        const bool bSameContent        = bKnown && signature == _uploadedRevision && quadCount == _uploadedQuadCount && _quadBuffer.isValid();
        if ( bSameContent )
        {
            uploadAnchors( device, frame._listAnchor, false );
            return;
        }
        const CanvasQuad* pQuads = frame._mainOutput._listQuad.data();
        if ( frame._listTarget.empty() == false )
        {
            _listQuadScratch.clear();
            _listQuadScratch.insert( _listQuadScratch.end(), frame._mainOutput._listQuad.begin(), frame._mainOutput._listQuad.end() );
            for ( const CanvasTargetDrawList& target : frame._listTarget )
            {
                _listQuadScratch.insert( _listQuadScratch.end(), target._list._listQuad.begin(), target._list._listQuad.end() );
            }
            pQuads = _listQuadScratch.data();
        }
        if ( _quadBuffer._capacityElements < quadCount || _quadBuffer.isValid() == false )
        {
            const uint32 capacity = MathUtil::max( quadCount, static_cast<uint32>( static_cast<float32>( _quadBuffer._capacityElements ) * CanvasQuadBufferInternal::kQuadBufferGrowth ) );
            _quadBuffer.release( &device );
            if ( _quadBuffer.ensureCapacity( &device, sizeof( CanvasQuad ), capacity, quadUsage, true, bNeedsUAV, nullptr ) == false )
            {
                SW_LOG_ERROR( "Failed to create the canvas quad buffer (%# quads) - the canvas is not drawn", capacity );
                _uploadedRevision = 0;
                return;
            }
        }
        const uint32 anchorBytes = anchorCount * static_cast<uint32>( sizeof( CanvasQuad ) );
        _quadBuffer.upload( &device, pQuads, ( quadCount - anchorCount ) * static_cast<uint32>( sizeof( CanvasQuad ) ), anchorBytes );
        uploadAnchors( device, frame._listAnchor, true );
        _uploadedRevision  = bKnown ? signature : 0;
        _uploadedQuadCount = quadCount;
    }

    void CanvasQuadBuffer::uploadAnchors( IRHIDevice& device, const vector<CanvasAnchor>& listAnchor, bool bForce )
    {
        const bool bSameAnchors = bForce == false && listAnchor.size() == _listUploadedAnchor.size() &&
                                  ( listAnchor.empty() || Memory::compare( listAnchor.data(), _listUploadedAnchor.data(), listAnchor.size() * sizeof( CanvasAnchor ) ) == 0 );
        if ( bSameAnchors || listAnchor.empty() )
        {
            _listUploadedAnchor = listAnchor;
            return;
        }
        // 앵커 하나가 사각형 원소 하나다(셰이더는 그 rect 만 읽는다). 구조 버퍼 하나에 두어 캔버스 패스의 바인딩을 늘리지 않는다.
        _listAnchorScratch.resize( listAnchor.size() );
        for ( size_t index = 0; index < listAnchor.size(); ++index )
        {
            const CanvasAnchor& anchor      = listAnchor[index];
            _listAnchorScratch[index]._rect = float4{ anchor._position._x, anchor._position._y, anchor._scale, 0.0f };
        }
        _quadBuffer.upload( &device, _listAnchorScratch.data(), static_cast<uint32>( listAnchor.size() * sizeof( CanvasQuad ) ) );
        _listUploadedAnchor = listAnchor;
    }

    void CanvasQuadBuffer::release( IRHIDevice* pDevice )
    {
        _quadBuffer.release( pDevice );
        _uploadedRevision  = 0;
        _uploadedQuadCount = 0;
        _listUploadedAnchor.clear();
    }
} // namespace sw
