#include "pch.h"

#include "Engine/Graphics/Debug/RenderTargetRegistry.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/Half.h"
#include "Core/Math/OctahedralNormal.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"

namespace sw
{
    namespace
    {
        struct RenderTargetReadbackInternal
        {
            static constexpr float32 kUnorm8Max    = 255.0f; ///< 8 비트 정규화 채널의 최댓값
            static constexpr float32 kSignedToUnit = 0.5f;   ///< [-1, 1] 방향을 [0, 1] 색으로 옮기는 배율 · 치우침(n * 0.5 + 0.5)

            /** @brief 32 비트 실수 채널 @p channel 을 읽습니다(정렬되지 않은 바이트). */
            static float32 readFloat( const uint8* pPixel, uint32 channel )
            {
                float32 value = 0.0f;
                Memory::copy( &value, pPixel + channel * sizeof( float32 ), sizeof( value ) );
                return value;
            }
        };
    } // namespace

    bool RenderTargetReadback::isDecodableFormat( RHIFormat format, bool bDepth )
    {
        // 깊이는 bDepth 로 온다 — 깊이 배치가 아닌 D24 바이트는 풀지 않는다(표의 바이트가 0 이다). 압축 · Unknown 도 0 이다.
        return bDepth || getRHIFormatBytesPerPixel( format ) != 0;
    }

    uint32 RenderTargetReadback::getChannelCount( RHIFormat format, bool bDepth )
    {
        if ( bDepth )
            return 1;
        switch ( format )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            case RHIFormat::B8G8R8A8_UNORM:
            case RHIFormat::R16G16B16A16_FLOAT:
            case RHIFormat::R32G32B32A32_FLOAT:
                return 4;
            case RHIFormat::R16G16_SNORM: // 옥타헤드럴 법선을 방향 색(xyz * 0.5 + 0.5)으로 풀어 보이므로 보이는 채널은 RGB 셋이다
            case RHIFormat::R32G32B32_FLOAT:
                return 3;
            case RHIFormat::R32G32_FLOAT:
                return 2;
            case RHIFormat::R32_FLOAT:
            case RHIFormat::R8_UNORM:
                return 1;
            case RHIFormat::D24_UNORM_S8_UINT:
            case RHIFormat::Unknown:
            case RHIFormat::BC1_UNORM:
            case RHIFormat::BC2_UNORM:
            case RHIFormat::BC3_UNORM:
            case RHIFormat::BC4_UNORM:
            case RHIFormat::BC5_UNORM:
            case RHIFormat::BC7_UNORM:
            case RHIFormat::BC6H_UF16:
            case RHIFormat::ASTC_4x4_UNORM:
            case RHIFormat::ETC2_RGBA8_UNORM:
            case RHIFormat::EAC_R11_UNORM:
            case RHIFormat::EAC_RG11_UNORM:
                return 0;
        }
    }

    bool RenderTargetReadback::readPixel( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, bool bDepth, uint32 x, uint32 y,
                                          float4& outValue )
    {
        const bool bTexelUnreadable = isDecodableFormat( format, bDepth ) == false || x >= layout._width || y >= layout._height;
        if ( bTexelUnreadable )
            return false;
        const RHIFormat sourceFormat  = bDepth ? kRHIDepthReadbackFormat : format;
        const uint32    bytesPerPixel = getRHIFormatBytesPerPixel( sourceFormat );
        const size_t    offset        = static_cast<size_t>( y ) * layout._rowBytes + static_cast<size_t>( x ) * bytesPerPixel;
        if ( bytesPerPixel == 0 || offset + bytesPerPixel > byte.size() )
            return false;

        const uint8* pPixel = byte.data() + offset;
        outValue            = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
        switch ( sourceFormat )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            {
                outValue = float4{ pPixel[0] / RenderTargetReadbackInternal::kUnorm8Max, pPixel[1] / RenderTargetReadbackInternal::kUnorm8Max, pPixel[2] / RenderTargetReadbackInternal::kUnorm8Max, pPixel[3] / RenderTargetReadbackInternal::kUnorm8Max };
                break;
            }
            case RHIFormat::B8G8R8A8_UNORM:
            {
                outValue = float4{ pPixel[2] / RenderTargetReadbackInternal::kUnorm8Max, pPixel[1] / RenderTargetReadbackInternal::kUnorm8Max, pPixel[0] / RenderTargetReadbackInternal::kUnorm8Max, pPixel[3] / RenderTargetReadbackInternal::kUnorm8Max };
                break;
            }
            case RHIFormat::R16G16B16A16_FLOAT:
            {
                uint16 arrHalf[4]{};
                Memory::copy( arrHalf, pPixel, sizeof( arrHalf ) );
                outValue = float4{ half::fromBits( arrHalf[0] ).toFloat(), half::fromBits( arrHalf[1] ).toFloat(), half::fromBits( arrHalf[2] ).toFloat(),
                                   half::fromBits( arrHalf[3] ).toFloat() };
                break;
            }
            case RHIFormat::R32G32B32_FLOAT:
            {
                outValue._x = RenderTargetReadbackInternal::readFloat( pPixel, 0 );
                outValue._y = RenderTargetReadbackInternal::readFloat( pPixel, 1 );
                outValue._z = RenderTargetReadbackInternal::readFloat( pPixel, 2 );
                break;
            }
            case RHIFormat::R32G32B32A32_FLOAT:
            {
                outValue = float4{ RenderTargetReadbackInternal::readFloat( pPixel, 0 ), RenderTargetReadbackInternal::readFloat( pPixel, 1 ),
                                   RenderTargetReadbackInternal::readFloat( pPixel, 2 ), RenderTargetReadbackInternal::readFloat( pPixel, 3 ) };
                break;
            }
            case RHIFormat::R32G32_FLOAT:
            {
                outValue._x = RenderTargetReadbackInternal::readFloat( pPixel, 0 );
                outValue._y = RenderTargetReadbackInternal::readFloat( pPixel, 1 );
                break;
            }
            case RHIFormat::R32_FLOAT:
            {
                outValue._x = RenderTargetReadbackInternal::readFloat( pPixel, 0 );
                break;
            }
            case RHIFormat::R8_UNORM:
            {
                outValue._x = pPixel[0] / RenderTargetReadbackInternal::kUnorm8Max;
                break;
            }
            case RHIFormat::R16G16_SNORM:
            {
                // G버퍼 법선(옥타헤드럴 좌표)이다. 방향으로 풀어 옛 `n * 0.5 + 0.5` 첨부와 같은 색으로 보인다.
                int16 arrCoordinate[2]{};
                Memory::copy( arrCoordinate, pPixel, sizeof( arrCoordinate ) );
                const float3 direction = OctahedralNormal{ snorm16::fromRaw( arrCoordinate[0] ), snorm16::fromRaw( arrCoordinate[1] ) }.toNormal();
                outValue               = float4{ direction._x * RenderTargetReadbackInternal::kSignedToUnit + RenderTargetReadbackInternal::kSignedToUnit, direction._y * RenderTargetReadbackInternal::kSignedToUnit + RenderTargetReadbackInternal::kSignedToUnit, direction._z * RenderTargetReadbackInternal::kSignedToUnit + RenderTargetReadbackInternal::kSignedToUnit, 1.0f };
                break;
            }
            case RHIFormat::D24_UNORM_S8_UINT:
            case RHIFormat::Unknown:
            case RHIFormat::BC1_UNORM:
            case RHIFormat::BC2_UNORM:
            case RHIFormat::BC3_UNORM:
            case RHIFormat::BC4_UNORM:
            case RHIFormat::BC5_UNORM:
            case RHIFormat::BC7_UNORM:
            case RHIFormat::BC6H_UF16:
            case RHIFormat::ASTC_4x4_UNORM:
            case RHIFormat::ETC2_RGBA8_UNORM:
            case RHIFormat::EAC_R11_UNORM:
            case RHIFormat::EAC_RG11_UNORM:
            {
                return false;
            }
        }
        return true;
    }

    void RenderTargetRegistry::publish( vector<RenderTargetInfo> listTarget )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _listTarget = std::move( listTarget );
        }
        // 잠금을 푼 뒤에 세대를 증가시킨다. 읽는 쪽은 세대를 보고 목록을 다시 가져갈지 정한다.
        _generation.fetch_add( 1, std::memory_order_release );
    }

    void RenderTargetRegistry::clear()
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _listTarget.clear();
        }
        _generation.fetch_add( 1, std::memory_order_release );
    }

    void RenderTargetRegistry::snapshot( vector<RenderTargetInfo>& outList ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outList = _listTarget;
    }

    bool RenderTargetRegistry::isPublished( RHITextureHandle texture ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return VectorUtil::containsBy<&RenderTargetInfo::_texture>( _listTarget, texture );
    }

    uint32 RenderTargetRegistry::requestReadback( RHITextureHandle texture )
    {
        ScopedLock<mutex> lock{ _readbackMutex };
        ++_lastRequestSerial;
        _pendingReadbackTexture = texture;
        _pendingReadbackSerial  = _lastRequestSerial;
        return _lastRequestSerial;
    }

    bool RenderTargetRegistry::takeReadbackRequest( RHITextureHandle& outTexture, uint32& outSerial )
    {
        ScopedLock<mutex> lock{ _readbackMutex };
        if ( _pendingReadbackSerial == 0 )
            return false;
        outTexture              = _pendingReadbackTexture;
        outSerial               = _pendingReadbackSerial;
        _pendingReadbackTexture = 0;
        _pendingReadbackSerial  = 0;
        return true;
    }

    void RenderTargetRegistry::publishReadback( RenderTargetReadback readback )
    {
        const uint32 serial = readback._serial;
        {
            ScopedLock<mutex> lock{ _readbackMutex };
            _readback = std::move( readback );
        }
        _readbackSerial.store( serial, std::memory_order_release );
    }

    bool RenderTargetRegistry::copyReadback( RenderTargetReadback& outReadback ) const
    {
        ScopedLock<mutex> lock{ _readbackMutex };
        if ( _readback._serial == 0 )
            return false;
        outReadback = _readback;
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( RenderTargetRegistry, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( RenderTargetRegistry ) );
} // namespace sw
