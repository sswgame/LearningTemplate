#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.h"

#include "Core/Common/EnumUtil.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIBufferSize.h"
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
            {         RHIFormat::BC1_UNORM,             kGlCompressedRgbaS3tcDxt1,                0,                    0},
            {         RHIFormat::BC2_UNORM,             kGlCompressedRgbaS3tcDxt3,                0,                    0},
            {         RHIFormat::BC3_UNORM,             kGlCompressedRgbaS3tcDxt5,                0,                    0},
            {         RHIFormat::BC4_UNORM,               GL_COMPRESSED_RED_RGTC1,                0,                    0},
            {         RHIFormat::BC5_UNORM,                  kGlCompressedRgRgtc2,                0,                    0},
            {         RHIFormat::BC7_UNORM,         GL_COMPRESSED_RGBA_BPTC_UNORM,                0,                    0},
            {         RHIFormat::BC6H_UF16, GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT,                0,                    0},
            {    RHIFormat::R8G8B8A8_UNORM,                              GL_RGBA8,          GL_RGBA,     GL_UNSIGNED_BYTE},
            {    RHIFormat::B8G8R8A8_UNORM,                              GL_RGBA8,          GL_BGRA,     GL_UNSIGNED_BYTE},
            // **half 는 GL_HALF_FLOAT 다.** GL_FLOAT 로 두면 GL 이 픽셀당 16 바이트를 읽고 쓰는데 엔진이 잡아 둔 버퍼는
            // 8 바이트/픽셀이다(`getRHIFormatBlockInfo` 가 기준). HDR 첨부를 CPU 로 되읽는 경로(스크린샷 · 렌더 타깃 패널)가
            // 버퍼를 두 배로 넘겨 써서 **그냥 죽는다**.
            {RHIFormat::R16G16B16A16_FLOAT,                            GL_RGBA16F,          GL_RGBA,        GL_HALF_FLOAT},
            { RHIFormat::D24_UNORM_S8_UINT,                   GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},
            {   RHIFormat::R32G32B32_FLOAT,                             GL_RGB32F,           GL_RGB,             GL_FLOAT},
            {      RHIFormat::R32G32_FLOAT,                              GL_RG32F,            GL_RG,             GL_FLOAT},
            {         RHIFormat::R32_FLOAT,                               GL_R32F,           GL_RED,             GL_FLOAT},
            {          RHIFormat::R8_UNORM,                                 GL_R8,           GL_RED,     GL_UNSIGNED_BYTE},
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

        /// @brief `GL_UNPACK_ALIGNMENT` · `GL_PACK_ALIGNMENT` 의 GL 기본값입니다. 가드 밖에서는 늘 이 값이다.
        constexpr GLint kGlDefaultPixelStoreAlignment = 4;

        /**
         * @class ScopedStagingUnpack
         * @brief 스테이징 링에서 텍스처로 올리는 동안 언팩 상태(`GL_PIXEL_UNPACK_BUFFER` = 링, `GL_UNPACK_ALIGNMENT` = 1)를 걸고, 소멸자에서 GL 기본값(0, 4)으로 되돌립니다.
         * @details 가드 밖의 언팩 상태는 늘 기본값이다. 주의: PBO 가 걸린 채 클라이언트 포인터로 올리는 호출(초기화의 1x1 흰 텍스처, ImGui 백엔드의 폰트 텍스처)이
         *          나가면 GL 이 그 포인터를 PBO 오프셋으로 읽어 엉뚱한 바이트를 올린다. 손으로 되돌리면 중간의 이른 반환 하나가 상태를 샌다.
         *          행 정렬 1 은 행이 빈틈없이 이어진 업로드 데이터(R32G32B32 의 12 바이트 행, R8 의 홀수 폭 행) 때문이다.
         */
        class ScopedStagingUnpack
        {
        public:
            explicit ScopedStagingUnpack( GLuint stagingBuffer )
            {
                glBindBuffer( GL_PIXEL_UNPACK_BUFFER, stagingBuffer );
                glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
            }
            ~ScopedStagingUnpack()
            {
                glPixelStorei( GL_UNPACK_ALIGNMENT, kGlDefaultPixelStoreAlignment );
                glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
            }
            ScopedStagingUnpack( const ScopedStagingUnpack& )            = delete;
            ScopedStagingUnpack& operator=( const ScopedStagingUnpack& ) = delete;

            /** @brief 링 안의 바이트 오프셋을 PBO 가 걸린 텍스처 명령의 데이터 인자로 바꿉니다(GL 은 그 자리에 포인터 대신 오프셋을 받는다). */
            static const void* toPixelOffset( uint64 offsetBytes ) { return reinterpret_cast<const void*>( static_cast<uintptr_t>( offsetBytes ) ); }
        };

        /**
         * @class ScopedStagingPack
         * @brief 되읽기 링으로 읽는 동안 팩 상태(`GL_PIXEL_PACK_BUFFER` = 되읽기 링, `GL_PACK_ALIGNMENT` = 1)를 걸고, 소멸자에서 GL 기본값(0, 4)으로 되돌립니다.
         * @details 가드 밖의 팩 상태는 늘 기본값이다. PBO 가 걸린 채 클라이언트 포인터로 읽는 호출(`glReadPixels` 등)이 나가면 GL 이 그 포인터를 PBO 오프셋으로 받아
         *          링의 엉뚱한 자리에 쓴다. 행 정렬 1 은 결과 버퍼의 행이 빈틈없이 이어지기 때문이다(`RHITextureMipSpan::_rowBytes`).
         */
        class ScopedStagingPack
        {
        public:
            explicit ScopedStagingPack( GLuint readbackBuffer )
            {
                glBindBuffer( GL_PIXEL_PACK_BUFFER, readbackBuffer );
                glPixelStorei( GL_PACK_ALIGNMENT, 1 );
            }
            ~ScopedStagingPack()
            {
                glPixelStorei( GL_PACK_ALIGNMENT, kGlDefaultPixelStoreAlignment );
                glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
            }
            ScopedStagingPack( const ScopedStagingPack& )            = delete;
            ScopedStagingPack& operator=( const ScopedStagingPack& ) = delete;

            /** @brief 링 안의 바이트 오프셋을 PBO 가 걸린 읽기 명령의 데이터 인자로 바꿉니다(GL 은 그 자리에 포인터 대신 오프셋을 받는다). */
            static void* toPixelOffset( uint64 offsetBytes ) { return reinterpret_cast<void*>( static_cast<uintptr_t>( offsetBytes ) ); }
        };

        /** @brief 텍스처 GL 이름과 면 FBO 이름들을 지웁니다. 0 인 이름은 건너뜁니다. 해제 큐에서 GL 컨텍스트가 있는 쪽에서 부릅니다. */
        void deleteTextureNames( GLuint fboName, GLuint texName, const vector<uint32>& listSliceFbo )
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
        ScopedOpenGLContext contextScope( _pDevice );
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
        ScopedOpenGLContext contextScope( _pDevice );
        glBindBuffer( GL_UNIFORM_BUFFER, ubo );
        glBufferSubData( GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>( size ), pData );
        glBindBuffer( GL_UNIFORM_BUFFER, 0 );
    }

    RHIBufferHandle OpenGLRHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        const bool bInvalidStructuredBufferRequest = _pDevice->_bInitialized == SW_FALSE || elementSize == 0 || elementCount == 0;
        if ( bInvalidStructuredBufferRequest )
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
        const bool bInvalidBufferRegionUpdate = _pDevice->_bInitialized == SW_FALSE || buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0;
        if ( bInvalidBufferRegionUpdate )
            return;

        GLuint ssbo = _pDevice->resolveGlBuffer( buffer );
        if ( ssbo == 0 )
            return;

        // GL 은 제출 · 배리어가 없어 조각마다 부르는 비용이 거의 없다. 바인딩만 한 번 하고 돈다.
        ScopedOpenGLContext contextScope( _pDevice );
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
        const uint8* pBase = static_cast<const uint8*>( pBaseSource );
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._sizeBytes == 0 )
                continue;
            glBufferSubData( GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>( region._dstOffset ),
                             static_cast<GLsizeiptr>( region._sizeBytes ), pBase + region._srcOffset );
        }
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );
    }

    RHIBufferHandle OpenGLRHIResourceFactory::createBuffer( const RHIBufferDesc& desc )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;

        ScopedOpenGLContext contextScope( _pDevice );

        const bool bHasVertexInitialData = EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Vertex ) && desc._pInitialData != nullptr && desc._sizeBytes > 0;
        if ( bHasVertexInitialData )
            return createVertexBuffer( desc._pInitialData, desc._sizeBytes );
        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Constant ) )
            return createConstantBuffer( desc._sizeBytes > 0 ? desc._sizeBytes : 256u );
        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Index ) )
            return _pDevice->createIndexBuffer( desc._pInitialData, desc._sizeBytes );

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

    RHIBufferHandle OpenGLRHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;
        ScopedOpenGLContext contextScope( _pDevice );
        return _pDevice->createIndexBuffer( pData, sizeBytes );
    }

    RHIBufferHandle OpenGLRHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        const bool bInvalidVertexBufferRequest = _pDevice->_bInitialized == SW_FALSE || pData == nullptr || sizeBytes == 0;
        if ( bInvalidVertexBufferRequest )
            return 0;

        ScopedOpenGLContext contextScope( _pDevice );
        GLuint              vbo{ 0 };
        glGenBuffers( 1, &vbo );
        glBindBuffer( GL_ARRAY_BUFFER, vbo );
        glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeBytes ), pData, GL_STATIC_DRAW );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        return _pDevice->storeGlBuffer( vbo, sizeBytes );
    }

    void OpenGLRHIResourceFactory::updateVertexBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        const bool bInvalidVertexRegionUpdate = _pDevice->_bInitialized == SW_FALSE || buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0;
        if ( bInvalidVertexRegionUpdate )
            return;
        const GLuint vbo = _pDevice->resolveGlBuffer( buffer );
        if ( vbo == 0 )
            return;

        // glBufferSubData 는 드라이버가 앞 드로우와 순서를 맞춘다. 대상은 GL_COPY_WRITE_BUFFER 다 — GL_ARRAY_BUFFER 바인딩을 건드리지 않는다.
        ScopedOpenGLContext contextScope( _pDevice );
        glBindBuffer( GL_COPY_WRITE_BUFFER, vbo );
        GLint bufferBytes{ 0 };
        glGetBufferParameteriv( GL_COPY_WRITE_BUFFER, GL_BUFFER_SIZE, &bufferBytes );
        const uint8* pBase = static_cast<const uint8*>( pBaseSource );
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( static_cast<int64>( region._dstOffset ) + static_cast<int64>( region._sizeBytes ) > static_cast<int64>( bufferBytes ) )
            {
                SW_LOG_ERROR( "updateVertexBufferRegions: region [%#, +%#) is outside the %#-byte vertex buffer; skipped", region._dstOffset, region._sizeBytes,
                              static_cast<int64>( bufferBytes ) );
                continue;
            }
            glBufferSubData( GL_COPY_WRITE_BUFFER, static_cast<GLintptr>( region._dstOffset ), static_cast<GLsizeiptr>( region._sizeBytes ), pBase + region._srcOffset );
        }
        glBindBuffer( GL_COPY_WRITE_BUFFER, 0 );
    }

    void OpenGLRHIResourceFactory::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;

        ScopedOpenGLContext contextScope( _pDevice );
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

    RHIBufferHandle OpenGLRHIResourceFactory::createReadbackBuffer( uint32 elementSize, uint32 elementCount )
    {
        uint32     totalBytes{ 0 };
        const bool bInvalidReadbackRequest = _pDevice->_bInitialized == SW_FALSE || elementSize == 0 || elementCount == 0 || RHIBufferSize::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false;
        if ( bInvalidReadbackRequest )
            return 0;
        ScopedOpenGLContext contextScope( _pDevice );
        GLuint              name{ 0 };
        glGenBuffers( 1, &name );
        if ( name == 0 )
            return 0;
        // GL_STREAM_READ: GPU 가 쓰고 CPU 가 읽는 용도 힌트. 대상은 GL_COPY_WRITE_BUFFER 라 다른 바인딩을 건드리지 않는다.
        glBindBuffer( GL_COPY_WRITE_BUFFER, name );
        glBufferData( GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>( totalBytes ), nullptr, GL_STREAM_READ );
        glBindBuffer( GL_COPY_WRITE_BUFFER, 0 );
        return _pDevice->storeGlBuffer( name, totalBytes );
    }

    bool OpenGLRHIResourceFactory::readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination )
    {
        const bool bInvalidRead = _pDevice->_bInitialized == SW_FALSE || pDestination == nullptr || sizeBytes == 0;
        if ( bInvalidRead )
            return false;
        const GLuint name = _pDevice->resolveGlBuffer( readback );
        if ( name == 0 )
            return false;
        ScopedOpenGLContext contextScope( _pDevice );
        glBindBuffer( GL_COPY_READ_BUFFER, name );
        GLint bufferBytes{ 0 };
        glGetBufferParameteriv( GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, &bufferBytes );
        // 읽기 버퍼만 GL_STREAM_READ 로 만든다(createReadbackBuffer). 다른 용도의 버퍼는 읽기 버퍼가 아니다. 네 백엔드가 같이 거절한다.
        GLint bufferUsage{ 0 };
        glGetBufferParameteriv( GL_COPY_READ_BUFFER, GL_BUFFER_USAGE, &bufferUsage );
        const bool bIsReadback = bufferUsage == static_cast<GLint>( GL_STREAM_READ );
        const bool bFits       = static_cast<int64>( sizeBytes ) <= static_cast<int64>( bufferBytes );
        const bool bReadable   = bIsReadback && bFits;
        if ( bReadable )
            glGetBufferSubData( GL_COPY_READ_BUFFER, 0, static_cast<GLsizeiptr>( sizeBytes ), pDestination );
        else if ( bIsReadback == false )
            SW_LOG_ERROR( "readReadbackBuffer: buffer %# is not a readback buffer", readback );
        else
            SW_LOG_ERROR( "readReadbackBuffer: %# bytes is past the end of the %#-byte readback buffer", sizeBytes, static_cast<int64>( bufferBytes ) );
        glBindBuffer( GL_COPY_READ_BUFFER, 0 );
        return bReadable;
    }

    RHITransferSubmit OpenGLRHIResourceFactory::submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        // 장치를 잃었으면 진입에서 Rejected 다(계약 2) — 잃은 뒤에는 워터마크가 멈춰 링이 비지 않으므로 아래 할당까지 가면 Busy 로 끝없이 되풀이된다.
        const bool bTextureNotUploadable = pRecord == nullptr || pRecord->_texture == 0 || _pDevice->_bInitialized == SW_FALSE || _pDevice->_transferSerials.isLost();
        if ( bTextureNotUploadable )
        {
            SW_LOG_ERROR( "submitTextureUpload: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }
        if ( pRecord->_bDepthStencil != SW_FALSE )
        {
            SW_LOG_ERROR( "submitTextureUpload: texture %# is a depth texture", texture );
            return RHITransferSubmit::rejected();
        }
        if ( desc._arraySlice >= pRecord->_arraySize )
        {
            SW_LOG_ERROR( "submitTextureUpload: slice %# is out of range (%# slices)", desc._arraySlice, pRecord->_arraySize );
            return RHITransferSubmit::rejected();
        }

        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, pRecord->_format, pRecord->_width, pRecord->_height,
                                                               pRecord->_mipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "submitTextureUpload: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
                          desc._sizeBytes, pRecord->_width, pRecord->_height, pRecord->_mipLevels );
            return RHITransferSubmit::rejected();
        }

        // 밉은 `desc._pData` 안에 빈틈없이 이어져 있다(resolveTextureUploadMips) — 링에는 마지막 밉의 끝까지 한 번에 복사하고, 밉마다 링 오프셋으로 올린다.
        // 링 한 바퀴보다 큰 업로드는 기다려도 못 받으므로 Rejected 다(allocateStagingSpan 이 이유와 설정 이름을 로그로 남긴다). 나눠 올리는 두 번째 경로는 두지 않는다.
        const RHITextureMipSpan& lastMip     = arrMip[mipCount - 1];
        const uint64             stagedBytes = static_cast<uint64>( lastMip._offsetBytes ) + lastMip._sizeBytes;
        RHIStagingSpan           staging{};
        const RHISubmitResult    allocation = _pDevice->allocateStagingSpan( stagedBytes, "submitTextureUpload", staging );
        if ( allocation != RHISubmitResult::Accepted )
            return allocation == RHISubmitResult::Busy ? RHITransferSubmit::busy() : RHITransferSubmit::rejected();
        // 복사는 컨텍스트 밖에서 한다(계약 1: 반환 전에 소스를 복사한다). 컨텍스트를 빌리는 시간은 PBO 바인딩, 텍스처 명령, 펜스뿐이다.
        Memory::copy( _pDevice->_pStagingRingMapped + staging._offsetBytes, desc._pData, static_cast<size_t>( stagedBytes ) );

        // 컨텍스트를 빌려 올린다(어느 스레드에서나 제출할 수 있다 — 계약 4). 렌더 스레드가 프레임 끝까지 쥐고 있으면 기다리다 못 얻을 수 있고, 그때는 나중에 다시 시도하면 된다.
        ScopedOpenGLContext contextScope( _pDevice );
        if ( _pDevice->isGraphicsContextCurrent() == false )
        {
            _pDevice->_stagingRing.free( staging );
            SW_LOG_ERROR( "submitTextureUpload: the GL context could not be acquired; submit again later" );
            return RHITransferSubmit::busy();
        }
        const bool   bCompressed = isRHIFormatBlockCompressed( pRecord->_format );
        const GLenum glInternal  = toGlInternalFormat( pRecord->_format );
        const GLenum glFormat    = toGlFormat( pRecord->_format );
        const GLenum glType      = toGlType( pRecord->_format );
        {
            const ScopedStagingUnpack unpack( _pDevice->_stagingRingBuffer );
            if ( pRecord->_target == GL_TEXTURE_2D )
            {
                glBindTexture( GL_TEXTURE_2D, pRecord->_texture );
                for ( uint32 mip = 0; mip < mipCount; ++mip )
                {
                    const RHITextureMipSpan& span    = arrMip[mip];
                    const void*              pPixels = ScopedStagingUnpack::toPixelOffset( staging._offsetBytes + span._offsetBytes );
                    if ( bCompressed )
                    {
                        glCompressedTexSubImage2D( GL_TEXTURE_2D, static_cast<GLint>( span._mip ), 0, 0, static_cast<GLsizei>( span._width ),
                                                   static_cast<GLsizei>( span._height ), glInternal, static_cast<GLsizei>( span._sizeBytes ), pPixels );
                    }
                    else
                    {
                        glTexSubImage2D( GL_TEXTURE_2D, static_cast<GLint>( span._mip ), 0, 0, static_cast<GLsizei>( span._width ),
                                         static_cast<GLsizei>( span._height ), glFormat, glType, pPixels );
                    }
                }
                glBindTexture( GL_TEXTURE_2D, 0 );
            }
            else
            {
                // 배열 · 큐브는 DSA 의 3D 창(z = 면)으로 올린다. 큐브 맵도 DSA 에서는 면이 z 다. DSA 업로드도 걸린 GL_PIXEL_UNPACK_BUFFER 를 읽는다.
                for ( uint32 mip = 0; mip < mipCount; ++mip )
                {
                    const RHITextureMipSpan& span    = arrMip[mip];
                    const void*              pPixels = ScopedStagingUnpack::toPixelOffset( staging._offsetBytes + span._offsetBytes );
                    if ( bCompressed )
                    {
                        glCompressedTextureSubImage3D( pRecord->_texture, static_cast<GLint>( span._mip ), 0, 0, static_cast<GLint>( desc._arraySlice ),
                                                       static_cast<GLsizei>( span._width ), static_cast<GLsizei>( span._height ), 1, glInternal,
                                                       static_cast<GLsizei>( span._sizeBytes ), pPixels );
                    }
                    else
                    {
                        glTextureSubImage3D( pRecord->_texture, static_cast<GLint>( span._mip ), 0, 0, static_cast<GLint>( desc._arraySlice ),
                                             static_cast<GLsizei>( span._width ), static_cast<GLsizei>( span._height ), 1, glFormat, glType, pPixels );
                    }
                }
            }
        }
        // 펜스가 링 구간의 회수 시점이다 — 워터마크가 이 시리얼을 지나면 구간이 돌아온다.
        const uint64 serial = _pDevice->endTransfer( true );
        _pDevice->_stagingRing.markSubmitted( staging, serial );
        pRecord->_lastTransferSerial.store( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferSubmit OpenGLRHIResourceFactory::submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        OpenGLRHIDevice::OpenGLTextureRecord* pRecord                     = _pDevice->resolveTexture( texture );
        const bool                            bTextureRegionNotUploadable = pRecord == nullptr || pRecord->_texture == 0 || _pDevice->_bInitialized == SW_FALSE || pRecord->_bDepthStencil != SW_FALSE ||
                                                 _pDevice->_transferSerials.isLost();
        if ( bTextureRegionNotUploadable )
        {
            SW_LOG_ERROR( "submitTextureRegionUpload: texture %# is missing, a depth texture, or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }
        uint32 rowBytes{ 0 };
        if ( validateTextureRegionUpload( pRecord->_format, pRecord->_width, pRecord->_height, pRecord->_mipLevels, pRecord->_arraySize, desc, rowBytes ) == false )
            return RHITransferSubmit::rejected();

        // 전체 업로드와 같은 링 · 같은 가드를 쓴다. `desc._sizeBytes` 가 더 커도 구간의 행만 복사한다(행은 빈틈없이 `rowBytes`).
        const uint64          stagedBytes = static_cast<uint64>( rowBytes ) * desc._height;
        RHIStagingSpan        staging{};
        const RHISubmitResult allocation = _pDevice->allocateStagingSpan( stagedBytes, "submitTextureRegionUpload", staging );
        if ( allocation != RHISubmitResult::Accepted )
            return allocation == RHISubmitResult::Busy ? RHITransferSubmit::busy() : RHITransferSubmit::rejected();
        Memory::copy( _pDevice->_pStagingRingMapped + staging._offsetBytes, desc._pData, static_cast<size_t>( stagedBytes ) );

        ScopedOpenGLContext contextScope( _pDevice );
        if ( _pDevice->isGraphicsContextCurrent() == false )
        {
            _pDevice->_stagingRing.free( staging );
            SW_LOG_ERROR( "submitTextureRegionUpload: the GL context could not be acquired; submit again later" );
            return RHITransferSubmit::busy();
        }
        const GLenum glFormat = toGlFormat( pRecord->_format );
        const GLenum glType   = toGlType( pRecord->_format );
        {
            const ScopedStagingUnpack unpack( _pDevice->_stagingRingBuffer );
            const void*               pPixels = ScopedStagingUnpack::toPixelOffset( staging._offsetBytes );
            if ( pRecord->_target == GL_TEXTURE_2D )
            {
                glBindTexture( GL_TEXTURE_2D, pRecord->_texture );
                glTexSubImage2D( GL_TEXTURE_2D, static_cast<GLint>( desc._mip ), static_cast<GLint>( desc._x ), static_cast<GLint>( desc._y ),
                                 static_cast<GLsizei>( desc._width ), static_cast<GLsizei>( desc._height ), glFormat, glType, pPixels );
                glBindTexture( GL_TEXTURE_2D, 0 );
            }
            else
            {
                // 배열 · 큐브는 전체 업로드와 같은 DSA 3D 창(z = 면)이다.
                glTextureSubImage3D( pRecord->_texture, static_cast<GLint>( desc._mip ), static_cast<GLint>( desc._x ), static_cast<GLint>( desc._y ),
                                     static_cast<GLint>( desc._arraySlice ), static_cast<GLsizei>( desc._width ), static_cast<GLsizei>( desc._height ), 1, glFormat,
                                     glType, pPixels );
            }
        }
        const uint64 serial = _pDevice->endTransfer( true );
        _pDevice->_stagingRing.markSubmitted( staging, serial );
        pRecord->_lastTransferSerial.store( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferStatus OpenGLRHIResourceFactory::pollTransfer( RHITransferTicket ticket )
    {
        const RHITransferStatus status = _pDevice->_transferSerials.classify( ticket );
        if ( status != RHITransferStatus::Pending )
            return status;
        // 컨텍스트를 기다리며 빌리지 않는다(계약 4) — 이미 쥐었거나 비어 있을 때만 펜스를 본다. 다른 스레드가 쥐었으면 그쪽이 올린 워터마크를 다음 폴링이 본다.
        if ( _pDevice->tryRetireFinishedTransfers() == false )
            return RHITransferStatus::Pending;
        return _pDevice->_transferSerials.classify( ticket );
    }

    bool OpenGLRHIResourceFactory::takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes )
    {
        if ( pollTransfer( ticket ) != RHITransferStatus::Complete )
        {
            SW_LOG_ERROR( "takeTextureReadback: ticket %# has not completed", ticket._serial );
            return false;
        }
        RHIStagingSpan span{};
        if ( _pDevice->_readbackTable.take( ticket._serial, span ) == false )
        {
            SW_LOG_ERROR( "takeTextureReadback: ticket %# is not a pending texture readback (an upload ticket, or already taken or discarded)", ticket._serial );
            return false;
        }
        // 펜스가 지났으므로 GPU 가 구간에 쓴 바이트가 COHERENT 매핑에 보인다. 복사는 컨텍스트 밖에서 하고, 다 읽은 구간을 링에 돌려준다.
        outBytes.resize( static_cast<size_t>( span._sizeBytes ) );
        Memory::copy( outBytes.data(), _pDevice->_pReadbackRingMapped + span._offsetBytes, static_cast<size_t>( span._sizeBytes ) );
        _pDevice->_readbackRing.free( span );
        return true;
    }

    void OpenGLRHIResourceFactory::discardTextureReadback( RHITransferTicket ticket )
    {
        if ( ticket._deviceGeneration != _pDevice->_transferSerials.getGeneration() )
            return;
        RHIStagingSpan span{};
        if ( _pDevice->_readbackTable.take( ticket._serial, span ) == false )
            return; // 업로드 티켓이거나 이미 가져감 · 버림이다 — 계약상 아무 일도 하지 않는다
        // Pending 이면 GPU 가 아직 구간에 쓰는 중이다 — 구간은 이 티켓의 펜스가 지난 뒤에 돌아온다.
        _pDevice->_readbackRing.markSubmitted( span, ticket._serial );
    }

    RHIFormat OpenGLRHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        const OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        return pRecord != nullptr ? pRecord->_format : RHIFormat::Unknown;
    }

    RHITransferSubmit OpenGLRHIResourceFactory::submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout )
    {
        OpenGLRHIDevice::OpenGLTextureRecord* pRecord             = _pDevice->resolveTexture( texture );
        const bool                            bTextureNotReadable = pRecord == nullptr || pRecord->_texture == 0 || _pDevice->_bInitialized == SW_FALSE || _pDevice->_transferSerials.isLost();
        if ( bTextureNotReadable )
        {
            SW_LOG_ERROR( "submitTextureReadback: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }
        if ( mip >= pRecord->_mipLevels || arraySlice >= pRecord->_arraySize )
        {
            SW_LOG_ERROR( "submitTextureReadback: mip %# / slice %# is out of range (%# mips, %# slices)", mip, arraySlice, pRecord->_mipLevels, pRecord->_arraySize );
            return RHITransferSubmit::rejected();
        }
        // 깊이는 깊이 성분만 0..1 float32 로 읽는다(kRHIDepthReadbackFormat). 스텐실은 버린다.
        const bool      bDepth       = pRecord->_bDepthStencil != SW_FALSE;
        const RHIFormat layoutFormat = bDepth ? kRHIDepthReadbackFormat : pRecord->_format;
        const GLenum    pixelFormat  = bDepth ? GL_DEPTH_COMPONENT : toGlFormat( pRecord->_format );
        const GLenum    pixelType    = bDepth ? GL_FLOAT : toGlType( pRecord->_format );
        if ( computeRHITextureMipLayout( layoutFormat, pRecord->_width, pRecord->_height, mip, outLayout ) == false )
        {
            SW_LOG_ERROR( "submitTextureReadback: unsupported format for %#x%# mip %#", pRecord->_width, pRecord->_height, mip );
            return RHITransferSubmit::rejected();
        }

        // 결과는 되읽기 링 구간으로 받는다. 링보다 큰 읽기 하나는 Rejected, 앞선 되읽기를 아직 가져가지 않아 차 있으면 Busy 다(allocateReadbackSpan 이 이유를 로그로 남긴다).
        RHIStagingSpan        readback{};
        const RHISubmitResult allocation = _pDevice->allocateReadbackSpan( outLayout._sizeBytes, readback );
        if ( allocation != RHISubmitResult::Accepted )
            return allocation == RHISubmitResult::Busy ? RHITransferSubmit::busy() : RHITransferSubmit::rejected();

        ScopedOpenGLContext contextScope( _pDevice );
        if ( _pDevice->isGraphicsContextCurrent() == false )
        {
            _pDevice->_readbackRing.free( readback );
            SW_LOG_ERROR( "submitTextureReadback: the GL context could not be acquired; submit again later" );
            return RHITransferSubmit::busy();
        }
        // 컨텍스트를 빌린 동안에는 읽기 명령(링 PBO 오프셋)과 펜스만 낸다 — GPU 가 끝나기를 기다리지 않는다. 앞서 낸 명령 뒤에 실행되므로 앞서 제출된 쓰기가 보인다(계약 3).
        // 바이트는 펜스가 지난 뒤 takeTextureReadback 이 컨텍스트 밖에서 복사한다. 2D 도 DSA 3D 창(z = 0)으로 읽어 텍스처 바인딩을 건드리지 않는다.
        {
            const ScopedStagingPack pack( _pDevice->_readbackRingBuffer );
            void*                   pPixels     = ScopedStagingPack::toPixelOffset( readback._offsetBytes );
            const GLsizei           bufferBytes = static_cast<GLsizei>( readback._sizeBytes );
            if ( isRHIFormatBlockCompressed( pRecord->_format ) )
            {
                glGetCompressedTextureSubImage( pRecord->_texture, static_cast<GLint>( mip ), 0, 0, static_cast<GLint>( arraySlice ),
                                                static_cast<GLsizei>( outLayout._width ), static_cast<GLsizei>( outLayout._height ), 1, bufferBytes, pPixels );
            }
            else
            {
                glGetTextureSubImage( pRecord->_texture, static_cast<GLint>( mip ), 0, 0, static_cast<GLint>( arraySlice ), static_cast<GLsizei>( outLayout._width ),
                                      static_cast<GLsizei>( outLayout._height ), 1, pixelFormat, pixelType, bufferBytes, pPixels );
            }
        }
        const uint64 serial = _pDevice->endTransfer( true );
        _pDevice->_readbackTable.add( serial, readback );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    // 길이 유지(긴 함수 정리 기준): 텍스처 생성 · 저장소 할당 · 매개변수 · 초기 데이터의 순서가 곧 의미라 나누지 않는다.
    RHITextureHandle OpenGLRHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        const bool bInvalidTextureRequest = _pDevice->_bInitialized == SW_FALSE || desc._width == 0 || desc._height == 0;
        if ( bInvalidTextureRequest )
            return 0;
        if ( isRHITextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR( "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                          desc._arraySize, desc._width, desc._height );
            return 0;
        }

        ScopedOpenGLContext contextScope( _pDevice );
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

        ScopedOpenGLContext                  contextScope( _pDevice );
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
            deleteTextureNames( fboName, texName, listSliceFbo );
            // 장부는 이름을 실제로 지울 때 줄인다(해제 요청 시점이 아니라).
            pLedger->recordFree( RHIMemoryKey::makeTexture( texture ) );
        };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }
} // namespace sw
