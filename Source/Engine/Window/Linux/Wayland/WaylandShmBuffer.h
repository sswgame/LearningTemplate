/**
 * @file WaylandShmBuffer.h
 * @brief 공유 메모리(`wl_shm`) 픽셀 버퍼와 그 묶음입니다 — 스플래시 창 · 개발 콘솔 덧그림이 CPU 로 그린 그림을 컴포지터에 넘길 때 쓴다.
 * @details 형식은 XRGB8888(리틀 엔디언 메모리에서 B · G · R · 안 쓰는 바이트)이라 스플래시의 BGRA 픽셀과 같은 순서다. 라이브러리 헤더는 .cpp 에만 있다.
 *          버퍼는 컴포지터가 `release` 를 알리기 전까지 "쓰는 중"이라 다시 그리면 안 된다 — 묶음(`WaylandShmBufferPool`)이 놀고 있는 것을 골라 준다.
 *          `release` 는 연결 이벤트를 처리(dispatch)해야 온다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

struct wl_buffer;
struct wl_shm;

namespace sw
{
    /** @class WaylandShmBuffer @brief 픽셀 버퍼 하나입니다(공유 메모리 + `wl_buffer`). */
    class WaylandShmBuffer
    {
    public:
        WaylandShmBuffer() = default;
        ~WaylandShmBuffer();

        WaylandShmBuffer( const WaylandShmBuffer& )            = delete;
        WaylandShmBuffer& operator=( const WaylandShmBuffer& ) = delete;

        /** @brief @p width × @p height 버퍼를 만듭니다. 메모리 · 풀 · 버퍼 어느 하나라도 못 만들면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool initialize( wl_shm* pShm, uint32 width, uint32 height );

        wl_buffer* getBuffer() const { return _pBuffer; }
        /** @brief 행 우선, 빈틈없는 픽셀(0x00RRGGBB — 위 바이트는 쓰지 않는다)입니다. */
        uint32* getPixels() const { return _pPixels; }
        uint32  getWidth() const { return _width; }
        uint32  getHeight() const { return _height; }
        /** @brief 컴포지터에 넘겨 아직 `release` 가 오지 않았으면 true 입니다. */
        bool isBusy() const { return _bBusy; }
        /** @brief 이 버퍼를 `wl_surface_attach` 한 직후 부릅니다. */
        void markBusy() { _bBusy = true; }
        /** @brief 컴포지터의 `release` 가 부릅니다(리스너 전용). */
        void clearBusy() { _bBusy = false; }

    private:
        wl_buffer* _pBuffer{ nullptr };
        uint32*    _pPixels{ nullptr };
        size_t     _byteSize{ 0 };
        uint32     _width{ 0 };
        uint32     _height{ 0 };
        bool       _bBusy{ false };
    };
} // namespace sw

namespace sw
{
    /** @class WaylandShmBufferPool @brief 같은 크기의 버퍼 몇 개를 돌려 쓰는 묶음입니다. */
    class WaylandShmBufferPool
    {
    public:
        /**
         * @brief 놀고 있는 @p width × @p height 버퍼를 돌려줍니다. 크기가 다른 놀고 있는 버퍼는 버리고, 없으면 새로 만든다(`kMaxBuffers` 까지).
         * @return 전부 쓰는 중이거나 새로 만들지 못하면 nullptr 입니다(호출 쪽이 이번 그리기를 건너뛰고 다음에 다시 한다).
         */
        [[nodiscard]] WaylandShmBuffer* acquire( wl_shm* pShm, uint32 width, uint32 height );

    private:
        static constexpr size_t kMaxBuffers = 3;

        vector<unique_ptr<WaylandShmBuffer>> _listBuffer;
    };
} // namespace sw
