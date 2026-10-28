#include "pch.h"

#include "Engine/Window/Linux/Wayland/WaylandShmBuffer.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_WAYLAND )
    #include "Core/Container/VectorUtil.h"

    #include <sys/mman.h>
    #include <unistd.h>
    #include <wayland-client.h>

namespace sw
{
    SW_LOG_CALLER( "WaylandShmBuffer" );

    namespace
    {
        /** @brief 컴포지터가 버퍼를 다 읽었다고 알리는 리스너입니다(사용자 값이 `WaylandShmBuffer*`). */
        struct WaylandShmBufferInternal
        {
            static constexpr uint32 kBytesPerPixel = 4;

            static void onBufferRelease( void* pData, wl_buffer* pBuffer )
            {
                (void)pBuffer;
                static_cast<WaylandShmBuffer*>( pData )->clearBusy();
            }

            static wl_buffer_listener makeListener()
            {
                wl_buffer_listener listener{};
                listener.release = &onBufferRelease;
                return listener;
            }
        };

        const wl_buffer_listener s_bufferListener = WaylandShmBufferInternal::makeListener();
    } // namespace

    WaylandShmBuffer::~WaylandShmBuffer()
    {
        if ( _pBuffer != nullptr )
            wl_buffer_destroy( _pBuffer );
        if ( _pPixels != nullptr )
            munmap( _pPixels, _byteSize );
    }

    bool WaylandShmBuffer::initialize( wl_shm* pShm, uint32 width, uint32 height )
    {
        const bool bInvalidArgument = pShm == nullptr || width == 0 || height == 0;
        if ( bInvalidArgument )
        {
            SW_LOG_ERROR( "Wayland shm buffer needs wl_shm and a non-empty size (%#x%#)", width, height );
            return false;
        }
        const uint32 stride   = width * WaylandShmBufferInternal::kBytesPerPixel;
        const size_t byteSize = static_cast<size_t>( stride ) * static_cast<size_t>( height );

        const int32 fd = memfd_create( "sw-wayland-shm", MFD_CLOEXEC );
        if ( fd < 0 )
        {
            SW_LOG_ERROR( "Wayland shm buffer: memfd_create failed" );
            return false;
        }
        if ( ftruncate( fd, static_cast<off_t>( byteSize ) ) != 0 )
        {
            close( fd );
            SW_LOG_ERROR( "Wayland shm buffer: ftruncate failed (%# bytes)", static_cast<uint64>( byteSize ) );
            return false;
        }
        void* const pMapped = mmap( nullptr, byteSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
        if ( pMapped == MAP_FAILED )
        {
            close( fd );
            SW_LOG_ERROR( "Wayland shm buffer: mmap failed (%# bytes)", static_cast<uint64>( byteSize ) );
            return false;
        }
        // 풀은 버퍼를 만든 뒤 바로 지워도 된다(버퍼가 메모리를 붙든다). 파일 번호도 풀을 만든 뒤에는 필요 없다.
        wl_shm_pool* const pPool = wl_shm_create_pool( pShm, fd, static_cast<int32>( byteSize ) );
        close( fd );
        if ( pPool == nullptr )
        {
            munmap( pMapped, byteSize );
            SW_LOG_ERROR( "Wayland shm buffer: wl_shm_create_pool failed" );
            return false;
        }
        wl_buffer* const pBuffer = wl_shm_pool_create_buffer( pPool, 0, static_cast<int32>( width ), static_cast<int32>( height ), static_cast<int32>( stride ), WL_SHM_FORMAT_XRGB8888 );
        wl_shm_pool_destroy( pPool );
        if ( pBuffer == nullptr )
        {
            munmap( pMapped, byteSize );
            SW_LOG_ERROR( "Wayland shm buffer: wl_shm_pool_create_buffer failed" );
            return false;
        }
        wl_buffer_add_listener( pBuffer, &s_bufferListener, this );
        _pBuffer  = pBuffer;
        _pPixels  = static_cast<uint32*>( pMapped );
        _byteSize = byteSize;
        _width    = width;
        _height   = height;
        _bBusy    = false;
        return true;
    }

    WaylandShmBuffer* WaylandShmBufferPool::acquire( wl_shm* pShm, uint32 width, uint32 height )
    {
        // 크기가 달라진 놀고 있는 버퍼는 버린다(쓰는 중인 것은 반납을 기다린다).
        VectorUtil::removeIf( _listBuffer, [width, height]( const unique_ptr<WaylandShmBuffer>& pBuffer )
        { return pBuffer->isBusy() == false && ( pBuffer->getWidth() != width || pBuffer->getHeight() != height ); } );
        for ( const unique_ptr<WaylandShmBuffer>& pBuffer : _listBuffer )
        {
            if ( pBuffer->isBusy() == false )
                return pBuffer.get();
        }
        if ( _listBuffer.size() >= kMaxBuffers )
            return nullptr;
        unique_ptr<WaylandShmBuffer> pNew = make_unique<WaylandShmBuffer>();
        if ( pNew->initialize( pShm, width, height ) == false )
            return nullptr;
        WaylandShmBuffer* pResult = pNew.get();
        _listBuffer.push_back( std::move( pNew ) );
        return pResult;
    }
} // namespace sw

#else

namespace sw
{
    WaylandShmBuffer::~WaylandShmBuffer() = default;

    bool WaylandShmBuffer::initialize( wl_shm*, uint32, uint32 )
    {
        return false;
    }

    WaylandShmBuffer* WaylandShmBufferPool::acquire( wl_shm*, uint32, uint32 )
    {
        return nullptr;
    }
} // namespace sw

#endif
