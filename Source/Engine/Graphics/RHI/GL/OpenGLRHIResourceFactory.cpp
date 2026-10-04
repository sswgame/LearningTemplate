#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.h"

#include "Core/Common/EnumUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIBufferSize.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <glad/glad.h>

namespace sw
{
    SW_LOG_CALLER( "OpenGLRHIResourceFactory" );

    namespace
    {
        // S3TC 는 확장이라 glad 헤더에 없다. 값은 EXT_texture_compression_s3tc 그대로다.
        constexpr GLenum kGlCompressedRgbaS3tcDxt1 = 0x83F1;
        constexpr GLenum kGlCompressedRgbaS3tcDxt3 = 0x83F2;
        constexpr GLenum kGlCompressedRgbaS3tcDxt5 = 0x83F3;
        constexpr GLenum kGlCompressedRgRgtc2      = 0x8DBD;

        /**
         * @brief RHIFormat 하나의 GL 세 값(internalFormat · (pixel) format · type)입니다.
         * @details 세 값을 한 줄에 두어 포맷을 더할 때 한 자리만 고치게 합니다. 압축 포맷은 internal 만 있습니다(glCompressedTexImage
         *          경로라 format · type 은 쓰지 않습니다). 표에 없는 포맷(`Unknown` = 첨부 없음)은 셋 다 0 입니다.
         */
        struct OpenGLFormatRow
        {
            RHIFormat _format;
            GLenum    _internalFormat;
            GLenum    _pixelFormat;
            GLenum    _pixelType;
        };

        constexpr OpenGLFormatRow arrFormatRow[] = {
            {         RHIFormat::BC1_UNORM,     kGlCompressedRgbaS3tcDxt1,                0,                    0},
            {         RHIFormat::BC2_UNORM,     kGlCompressedRgbaS3tcDxt3,                0,                    0},
            {         RHIFormat::BC3_UNORM,     kGlCompressedRgbaS3tcDxt5,                0,                    0},
            {         RHIFormat::BC4_UNORM,       GL_COMPRESSED_RED_RGTC1,                0,                    0},
            {         RHIFormat::BC5_UNORM,          kGlCompressedRgRgtc2,                0,                    0},
            {         RHIFormat::BC7_UNORM, GL_COMPRESSED_RGBA_BPTC_UNORM,                0,                    0},
            {    RHIFormat::R8G8B8A8_UNORM,                      GL_RGBA8,          GL_RGBA,     GL_UNSIGNED_BYTE},
            {    RHIFormat::B8G8R8A8_UNORM,                      GL_RGBA8,          GL_BGRA,     GL_UNSIGNED_BYTE},
            // **half 는 GL_HALF_FLOAT 다.** GL_FLOAT 로 두면 GL 이 픽셀당 16 바이트를 읽고 쓰는데 엔진이 잡아 둔 버퍼는
            // 8 바이트/픽셀이다(`getRhiFormatBlockInfo` 가 기준). HDR 첨부를 CPU 로 되읽는 경로(스크린샷 · 렌더 타깃 패널)가
            // 버퍼를 두 배로 넘겨 써서 **그냥 죽는다**.
            {RHIFormat::R16G16B16A16_FLOAT,                    GL_RGBA16F,          GL_RGBA,        GL_HALF_FLOAT},
            { RHIFormat::D24_UNORM_S8_UINT,           GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},
            {   RHIFormat::R32G32B32_FLOAT,                     GL_RGB32F,           GL_RGB,             GL_FLOAT},
            {      RHIFormat::R32G32_FLOAT,                      GL_RG32F,            GL_RG,             GL_FLOAT},
            {         RHIFormat::R32_FLOAT,                       GL_R32F,           GL_RED,             GL_FLOAT},
        };

        const OpenGLFormatRow* findFormatRow( RHIFormat format )
        {
            for ( const OpenGLFormatRow& row : arrFormatRow )
            {
                if ( row._format == format )
                    return &row;
            }
            return nullptr;
        }

        GLenum toGlInternalFormat( RHIFormat format )
        {
            const OpenGLFormatRow* pRow = findFormatRow( format );
            return pRow != nullptr ? pRow->_internalFormat : 0;
        }

        GLenum toGlFormat( RHIFormat format )
        {
            const OpenGLFormatRow* pRow = findFormatRow( format );
            return pRow != nullptr ? pRow->_pixelFormat : 0;
        }

        GLenum toGlType( RHIFormat format )
        {
            const OpenGLFormatRow* pRow = findFormatRow( format );
            return pRow != nullptr ? pRow->_pixelType : 0;
        }
    } // namespace

    uint32 OpenGLRHIDevice::getGlTextureName( RHITextureHandle texture ) const
    {
        if ( texture == 0 )
            return 0;
        const OpenGLTextureRecord* pRecord = resolveTexture( texture );
        return pRecord != nullptr ? pRecord->_texture : 0;
    }

    RHIBufferHandle OpenGLRHIResourceFactory::createConstantBuffer( uint32 size )
    {
        // 머티리얼 상수버퍼는 **게임 스레드**(Material::initialize) 에서 만들어진다. GL 컨텍스트는 렌더 스레드가
        // 프레임 동안만 쥐고 executePacket 끝에 놓으므로(RenderThread), 여기서도 createBuffer 처럼 잠깐 빌려야 한다.
        // 주의: 가드 없이는 glGenBuffers 가 조용히 아무것도 안 해 초기화 안 된 이름이 그대로 저장되고(0xFFFFFFFF),
        // 이후 update 도 무시돼 PS 가 color=0 을 읽어 큐브가 모두 검게 나온다. GL 에러도, 로그도 없이.
        ScopedOpenGLContext ctxScope( _pDevice );
        const uint32        alignedSize = MathUtil::align( size, constant::kConstantBufferAlignment );
        GLuint              ubo{ 0 };
        glGenBuffers( 1, &ubo );
        if ( ubo == 0 )
            return 0;
        glBindBuffer( GL_UNIFORM_BUFFER, ubo );
        glBufferData( GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>( alignedSize ), nullptr, GL_DYNAMIC_DRAW );
        glBindBuffer( GL_UNIFORM_BUFFER, 0 );

        return _pDevice->storeGlBuffer( ubo, alignedSize );
    }

    void OpenGLRHIResourceFactory::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr )
            return;
        GLuint ubo = _pDevice->resolveGlBuffer( buffer );
        if ( ubo == 0 )
            return;
        ScopedOpenGLContext ctxScope( _pDevice );
        glBindBuffer( GL_UNIFORM_BUFFER, ubo );
        glBufferSubData( GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>( size ), pData );
        glBindBuffer( GL_UNIFORM_BUFFER, 0 );
    }

    RHIBufferHandle OpenGLRHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || elementSize == 0 || elementCount == 0 )
            return 0;

        // 32비트 API 다. 담기지 않으면 만들지 않는다(RHIBufferSize 가 세 백엔드의 규칙 하나).
        uint32 totalBytes{ 0 };
        if ( RHIBufferSize::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false )
            return 0;

        RHIBufferDesc desc{};
        desc._elementSize  = elementSize;
        desc._elementCount = elementCount;
        desc._sizeBytes    = static_cast<uint32>( totalBytes );
        desc._usage        = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs;
        return createBuffer( desc );
    }

    void OpenGLRHIResourceFactory::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                                  const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 )
            return;

        GLuint ssbo = _pDevice->resolveGlBuffer( buffer );
        if ( ssbo == 0 )
            return;

        // GL 은 제출 · 배리어가 없어 조각마다 부르는 비용이 거의 없다. 바인딩만 한 번 하고 돈다.
        ScopedOpenGLContext ctxScope( _pDevice );
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
        const uint8* pBase = static_cast<const uint8*>( pBaseSource );
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._size == 0 )
                continue;
            glBufferSubData( GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>( region._dstOffset ),
                             static_cast<GLsizeiptr>( region._size ), pBase + region._srcOffset );
        }
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );
    }

    RHIBufferHandle OpenGLRHIResourceFactory::createBuffer( const RHIBufferDesc& desc )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;

        ScopedOpenGLContext ctxScope( _pDevice );

        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Vertex ) && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
            return createVertexBuffer( desc._pInitialData, desc._sizeBytes );
        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Constant ) )
            return createConstantBuffer( desc._sizeBytes > 0 ? desc._sizeBytes : 256u );
        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Index ) )
        {
            const uint32 stride = ( desc._elementSize == 2 ) ? 2u : 4u;
            return _pDevice->createIndexBuffer( desc._pInitialData, desc._sizeBytes, stride );
        }

        uint32 sizeBytes = desc._sizeBytes;
        if ( sizeBytes == 0 )
        {
            const uint32 elemSize  = desc._elementSize > 0 ? desc._elementSize : 4u;
            const uint32 elemCount = desc._elementCount > 0 ? desc._elementCount : 1u;
            sizeBytes              = elemSize * elemCount;
        }
        if ( sizeBytes == 0 )
            return 0;

        const uint32 alignedSize = MathUtil::align( sizeBytes, constant::kConstantBufferAlignment );

        // SSBO 할당. 같은 이름을 GL_DRAW_INDIRECT_BUFFER · GL_DISPATCH_INDIRECT_BUFFER 로도 걸 수 있다.
        //
        // **할당은 정렬 크기로, 채우기는 실제 크기로 나눈다.** 주의: `glBufferData` 에 정렬 크기와 초기 데이터를
        // 함께 넘기면, 부르는 쪽이 준 버퍼는 `_sizeBytes` 뿐이라 GL 이 그 뒤를 읽는다(정렬이 256 바이트라 최대
        // 255 바이트를 넘겨 읽는다). 읽은 쓰레기가 버퍼 꼬리에 들어갈 뿐 아니라, 부르는 쪽 버퍼가 페이지 끝에
        // 걸리면 그대로 죽는다.
        GLuint ssbo{ 0 };
        glGenBuffers( 1, &ssbo );
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
        glBufferData( GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>( alignedSize ), nullptr, GL_DYNAMIC_DRAW );
        if ( desc._pInitialData != nullptr && sizeBytes > 0 )
            glBufferSubData( GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>( sizeBytes ), desc._pInitialData );
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );

        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::IndirectArgs ) )
        {
            glBindBuffer( GL_DRAW_INDIRECT_BUFFER, ssbo );
            glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
        }

        return _pDevice->storeGlBuffer( ssbo, alignedSize );
    }

    RHIBufferHandle OpenGLRHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;
        ScopedOpenGLContext ctxScope( _pDevice );
        return _pDevice->createIndexBuffer( pData, sizeBytes, ( indexStride == 2 ) ? 2u : 4u );
    }

    RHIBufferHandle OpenGLRHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || pData == nullptr || sizeBytes == 0 )
            return 0;

        ScopedOpenGLContext ctxScope( _pDevice );
        GLuint              vbo{ 0 };
        glGenBuffers( 1, &vbo );
        glBindBuffer( GL_ARRAY_BUFFER, vbo );
        glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeBytes ), pData, GL_STATIC_DRAW );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        return _pDevice->storeGlBuffer( vbo, sizeBytes );
    }

    void OpenGLRHIResourceFactory::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;

        ScopedOpenGLContext ctxScope( _pDevice );
        if ( buffer == _pDevice->_recordingState._boundMeshVb )
            _pDevice->_recordingState._boundMeshVb = 0;
        if ( buffer == _pDevice->_recordingState._boundIndexBuffer )
            _pDevice->_recordingState._boundIndexBuffer = 0;

        uint32 glName{ 0 };
        if ( _pDevice->_gpuBuffers.take( buffer, glName ) == false )
            return;

        for ( OpenGLRHIDevice::BindlessResourceRecord& record : _pDevice->_listRegisteredBindless )
        {
            if ( record._buffer != buffer )
                continue;
            record._buffer = 0;
        }
        for ( OpenGLRHIDevice::BindlessResourceRecord& record : _pDevice->_listRegisteredUAV )
        {
            if ( record._buffer != buffer )
                continue;
            record._buffer = 0;
        }

        // 장부는 이름을 실제로 지울 때 줄인다(해제 요청 시점이 아니라).
        RHIMemoryLedger* pLedger   = &_pDevice->getMemoryLedger();
        auto             releaseCb = [glBuffer = glName, pLedger, buffer]()
        {
            GLuint name = glBuffer;
            glDeleteBuffers( 1, &name );
            pLedger->recordFree( RHIMemoryKey::makeBuffer( buffer ) );
        };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }

    bool OpenGLRHIResourceFactory::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == 0 || _pDevice->_bInitialized == SW_FALSE )
            return false;
        if ( pRecord->_bDepthStencil != SW_FALSE )
            return false;
        if ( desc._arraySlice >= pRecord->_arraySize )
        {
            SW_LOG_ERROR( "uploadTexture2D: slice %# is out of range (%# slices)", desc._arraySlice, pRecord->_arraySize );
            return false;
        }

        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, pRecord->_format, pRecord->_width, pRecord->_height,
                                                               pRecord->_mipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2D: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
                          desc._sizeBytes, pRecord->_width, pRecord->_height, pRecord->_mipLevels );
            return false;
        }

        ScopedOpenGLContext ctxScope( _pDevice );
        const bool          bCompressed = isRhiFormatBlockCompressed( pRecord->_format );
        const GLenum        glInternal  = toGlInternalFormat( pRecord->_format );
        const GLenum        glFormat    = toGlFormat( pRecord->_format );
        const GLenum        glType      = toGlType( pRecord->_format );

        // 행이 빈틈없이 이어진 데이터라 기본 4바이트 행 정렬을 끈다(R32G32B32 12바이트 행 같은 경우).
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        if ( pRecord->_target == GL_TEXTURE_2D )
        {
            glBindTexture( GL_TEXTURE_2D, pRecord->_texture );
            for ( uint32 mip = 0; mip < mipCount; ++mip )
            {
                const RHITextureMipSpan& span = arrMip[mip];
                if ( bCompressed )
                {
                    glCompressedTexSubImage2D( GL_TEXTURE_2D, static_cast<GLint>( span._mip ), 0, 0, static_cast<GLsizei>( span._width ),
                                               static_cast<GLsizei>( span._height ), glInternal, static_cast<GLsizei>( span._sizeBytes ), span._pData );
                }
                else
                {
                    glTexSubImage2D( GL_TEXTURE_2D, static_cast<GLint>( span._mip ), 0, 0, static_cast<GLsizei>( span._width ),
                                     static_cast<GLsizei>( span._height ), glFormat, glType, span._pData );
                }
            }
            glBindTexture( GL_TEXTURE_2D, 0 );
        }
        else
        {
            // 배열 · 큐브는 DSA 의 3D 창(z = 면)으로 올린다. 큐브 맵도 DSA 에서는 면이 z 다.
            for ( uint32 mip = 0; mip < mipCount; ++mip )
            {
                const RHITextureMipSpan& span = arrMip[mip];
                if ( bCompressed )
                {
                    glCompressedTextureSubImage3D( pRecord->_texture, static_cast<GLint>( span._mip ), 0, 0, static_cast<GLint>( desc._arraySlice ),
                                                   static_cast<GLsizei>( span._width ), static_cast<GLsizei>( span._height ), 1, glInternal,
                                                   static_cast<GLsizei>( span._sizeBytes ), span._pData );
                }
                else
                {
                    glTextureSubImage3D( pRecord->_texture, static_cast<GLint>( span._mip ), 0, 0, static_cast<GLint>( desc._arraySlice ),
                                         static_cast<GLsizei>( span._width ), static_cast<GLsizei>( span._height ), 1, glFormat, glType, span._pData );
                }
            }
        }
        glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
        return true;
    }

    RHIFormat OpenGLRHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        const OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        return pRecord != nullptr ? pRecord->_format : RHIFormat::Unknown;
    }

    bool OpenGLRHIResourceFactory::readbackTexture2D( RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes, RHITextureMipSpan& outLayout )
    {
        OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == 0 || _pDevice->_bInitialized == SW_FALSE )
            return false;
        if ( pRecord->_bDepthStencil != SW_FALSE || mip >= pRecord->_mipLevels || arraySlice >= pRecord->_arraySize )
            return false;
        if ( computeRhiTextureMipLayout( pRecord->_format, pRecord->_width, pRecord->_height, mip, outLayout ) == false )
            return false;

        ScopedOpenGLContext ctxScope( _pDevice );
        outBytes.assign( outLayout._sizeBytes, 0 );
        glPixelStorei( GL_PACK_ALIGNMENT, 1 );
        if ( pRecord->_target == GL_TEXTURE_2D )
        {
            glBindTexture( GL_TEXTURE_2D, pRecord->_texture );
            if ( isRhiFormatBlockCompressed( pRecord->_format ) )
                glGetCompressedTexImage( GL_TEXTURE_2D, static_cast<GLint>( mip ), outBytes.data() );
            else
                glGetTexImage( GL_TEXTURE_2D, static_cast<GLint>( mip ), toGlFormat( pRecord->_format ), toGlType( pRecord->_format ), outBytes.data() );
            glBindTexture( GL_TEXTURE_2D, 0 );
        }
        else
        {
            // 면 하나만 읽는다(z = 면 — 큐브 맵도 DSA 에서는 면이 z 다).
            const GLsizei bufferBytes = static_cast<GLsizei>( outBytes.size() );
            if ( isRhiFormatBlockCompressed( pRecord->_format ) )
            {
                glGetCompressedTextureSubImage( pRecord->_texture, static_cast<GLint>( mip ), 0, 0, static_cast<GLint>( arraySlice ),
                                                static_cast<GLsizei>( outLayout._width ), static_cast<GLsizei>( outLayout._height ), 1, bufferBytes, outBytes.data() );
            }
            else
            {
                glGetTextureSubImage( pRecord->_texture, static_cast<GLint>( mip ), 0, 0, static_cast<GLint>( arraySlice ), static_cast<GLsizei>( outLayout._width ),
                                      static_cast<GLsizei>( outLayout._height ), 1, toGlFormat( pRecord->_format ), toGlType( pRecord->_format ), bufferBytes,
                                      outBytes.data() );
            }
        }
        glPixelStorei( GL_PACK_ALIGNMENT, 4 );
        return true;
    }

    RHITextureHandle OpenGLRHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || desc._width == 0 || desc._height == 0 )
            return 0;
        if ( isRhiTextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR( "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                          desc._arraySize, desc._width, desc._height );
            return 0;
        }

        ScopedOpenGLContext ctxScope( _pDevice );
        const uint32        mipLevels   = desc._mipLevels > 0 ? desc._mipLevels : 1;
        const GLenum        internalFmt = toGlInternalFormat( desc._format );
        const bool          bDepth      = desc._bIsDepthStencil || desc._format == RHIFormat::D24_UNORM_S8_UINT;
        const GLenum        target      = ( desc._dimension == RHITextureDimension::TextureCube )    ? GL_TEXTURE_CUBE_MAP
                                        : ( desc._dimension == RHITextureDimension::Texture2DArray ) ? GL_TEXTURE_2D_ARRAY
                                                                                                     : GL_TEXTURE_2D;

        GLuint tex{ 0 };
        glGenTextures( 1, &tex );
        glBindTexture( target, tex );

        if ( target == GL_TEXTURE_2D_ARRAY )
        {
            glTexStorage3D( GL_TEXTURE_2D_ARRAY, static_cast<GLsizei>( mipLevels ), internalFmt, static_cast<GLsizei>( desc._width ),
                            static_cast<GLsizei>( desc._height ), static_cast<GLsizei>( desc._arraySize ) );
        }
        else if ( target == GL_TEXTURE_CUBE_MAP || glad_glTexStorage2D != nullptr )
        {
            // 큐브 맵의 저장소는 2D 와 같은 호출 하나로 면 여섯이 잡힌다.
            glTexStorage2D( target, static_cast<GLsizei>( mipLevels ), internalFmt, static_cast<GLsizei>( desc._width ), static_cast<GLsizei>( desc._height ) );
        }
        else
        {
            glTexImage2D( GL_TEXTURE_2D, 0, static_cast<GLint>( internalFmt ),
                          static_cast<GLsizei>( desc._width ), static_cast<GLsizei>( desc._height ),
                          0, toGlFormat( desc._format ), toGlType( desc._format ), nullptr );
        }

        if ( bDepth )
        {
            glTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
            glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
            glTexParameteri( target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glTexParameteri( target, GL_TEXTURE_COMPARE_MODE, GL_NONE );
            glTexParameteri( target, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_DEPTH_COMPONENT );
        }
        else
        {
            const GLint minFilter = ( mipLevels > 1 ) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
            glTexParameteri( target, GL_TEXTURE_MIN_FILTER, minFilter );
            glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        }
        glBindTexture( target, 0 );

        OpenGLRHIDevice::OpenGLTextureRecord record{};
        record._texture        = tex;
        record._width          = desc._width;
        record._height         = desc._height;
        record._mipLevels      = mipLevels;
        record._format         = desc._format;
        record._internalFormat = static_cast<uint32>( internalFmt );
        record._target         = static_cast<uint32>( target );
        record._arraySize      = desc._arraySize;
        record._dimension      = desc._dimension;
        record._bDepthStencil  = bDepth ? 1 : 0;
        record._bUAV           = desc._bIsUnorderedAccess ? 1 : 0;
        record._reserved       = 0;

        // 단일 타깃 FBO — 면마다 하나(면이 하나면 `_fbo` 하나).
        const uint32 fboCount = ( desc._bIsRenderTarget || bDepth ) ? desc._arraySize : 0u;
        for ( uint32 slice = 0; slice < fboCount; ++slice )
        {
            GLuint fbo{ 0 };
            glGenFramebuffers( 1, &fbo );
            glBindFramebuffer( GL_FRAMEBUFFER, fbo );
            if ( bDepth )
            {
                const GLenum depthAttachment = ( desc._format == RHIFormat::D24_UNORM_S8_UINT )
                                                 ? GL_DEPTH_STENCIL_ATTACHMENT
                                                 : GL_DEPTH_ATTACHMENT;
                OpenGLRHIDevice::attachTextureToFramebuffer( depthAttachment, record, slice );
                glDrawBuffer( GL_NONE );
                glReadBuffer( GL_NONE );
            }
            else
                OpenGLRHIDevice::attachTextureToFramebuffer( GL_COLOR_ATTACHMENT0, record, slice );
            const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
            if ( status != GL_FRAMEBUFFER_COMPLETE )
            {
                SW_LOG_WARNING( "createTexture2D FBO incomplete (status=%#, slice %#) — texture kept without FBO.",
                                static_cast<uint32>( status ), slice );
                glDeleteFramebuffers( 1, &fbo );
                fbo = 0;
            }
            if ( slice == 0 )
                record._fbo = fbo;
            if ( desc._dimension != RHITextureDimension::Texture2D )
                record._listSliceFbo.push_back( fbo );
        }

        // GPU 메모리 장부에 올리는 유일한 자리다. GL 에는 할당 크기를 물을 API 가 없어 서술로 계산한 논리 크기를 적는다.
        const RHITextureHandle handle = _pDevice->_gpuTextures.insert( record );
        _pDevice->getMemoryLedger().recordAllocation( RHIMemoryKey::makeTexture( handle ), RHIMemoryLedger::classifyTexture( desc ),
                                                      RHIMemoryLedger::computeTextureLogicalBytes( desc ) );
        return handle;
    }

    void OpenGLRHIResourceFactory::destroyTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return;

        ScopedOpenGLContext                  ctxScope( _pDevice );
        OpenGLRHIDevice::OpenGLTextureRecord owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;

        for ( auto compIt = _pDevice->_mapCompositeFbo.begin(); compIt != _pDevice->_mapCompositeFbo.end(); )
        {
            bool bUsesTexture = ( compIt->first._depth == texture );
            for ( uint32 colorIndex = 0; colorIndex < compIt->first._colorCount && bUsesTexture == false; ++colorIndex )
            {
                bUsesTexture = ( compIt->first._arrColor[colorIndex] == texture );
            }

            if ( bUsesTexture )
            {
                GLuint fbo = compIt->second;
                if ( fbo != 0 )
                    glDeleteFramebuffers( 1, &fbo );
                compIt = _pDevice->_mapCompositeFbo.erase( compIt );
            }
            else
                ++compIt;
        }

        const GLuint   fboName = owned._fbo;
        const GLuint   texName = owned._texture;
        vector<uint32> listSliceFbo{ owned._listSliceFbo };

        for ( size_t textureIndex = 0; textureIndex < _pDevice->_listRegisteredTexture.size(); ++textureIndex )
        {
            if ( _pDevice->_listRegisteredTexture[textureIndex]._texture != texture )
                continue;
            releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree,
                                  static_cast<uint32>( textureIndex ), OpenGLRHIDevice::BindlessTextureRecord{} );
        }

        RHIMemoryLedger* pLedger   = &_pDevice->getMemoryLedger();
        auto             releaseCb = [fboName, texName, listSliceFbo, pLedger, texture]()
        {
            // 면 FBO 의 0 번은 `_fbo` 와 같다 — 1 번부터 지운다.
            for ( size_t slice = 1; slice < listSliceFbo.size(); ++slice )
            {
                GLuint name = listSliceFbo[slice];
                if ( name != 0 )
                    glDeleteFramebuffers( 1, &name );
            }
            if ( fboName != 0 )
            {
                GLuint name = fboName;
                glDeleteFramebuffers( 1, &name );
            }
            if ( texName != 0 )
            {
                GLuint name = texName;
                glDeleteTextures( 1, &name );
            }
            // 장부는 이름을 실제로 지울 때 줄인다(해제 요청 시점이 아니라).
            pLedger->recordFree( RHIMemoryKey::makeTexture( texture ) );
        };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }
} // namespace sw
