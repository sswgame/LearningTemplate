#include "pch.h"

#include "Engine/Graphics/Texture/TextureStreamingPool.h"

namespace sw
{
    TextureStreamingReservation::TextureStreamingReservation( atomic<uint64>& usedBytes, uint64 bytes )
        : _pUsedBytes{ &usedBytes }
        , _bytes{ bytes }
    {
    }

    TextureStreamingReservation::~TextureStreamingReservation()
    {
        reset();
    }

    TextureStreamingReservation::TextureStreamingReservation( TextureStreamingReservation&& other ) noexcept
        : _pUsedBytes{ other._pUsedBytes }
        , _bytes{ other._bytes }
    {
        other._pUsedBytes = nullptr;
        other._bytes      = 0;
    }

    TextureStreamingReservation& TextureStreamingReservation::operator=( TextureStreamingReservation&& other ) noexcept
    {
        if ( this != &other )
        {
            reset();
            _pUsedBytes       = other._pUsedBytes;
            _bytes            = other._bytes;
            other._pUsedBytes = nullptr;
            other._bytes      = 0;
        }
        return *this;
    }

    void TextureStreamingReservation::reset()
    {
        if ( _pUsedBytes != nullptr )
            _pUsedBytes->fetch_sub( _bytes, std::memory_order_acq_rel );
        _pUsedBytes = nullptr;
        _bytes      = 0;
    }

    TextureStreamingPool::TextureStreamingPool()
        : _usedBytes{ 0 }
    {
    }

    TextureStreamingPool::~TextureStreamingPool()
    {
        SW_ASSERT( _usedBytes.load( std::memory_order_acquire ) == 0 ); // 예약(텍스처 항목)이 풀보다 먼저 사라져야 한다
    }

    optional<TextureStreamingReservation> TextureStreamingPool::tryReserve( uint64 bytes, uint64 budgetBytes )
    {
        uint64 used = _usedBytes.load( std::memory_order_acquire );
        do
        {
            if ( used + bytes > budgetBytes )
                return nullopt;
        } while ( _usedBytes.compare_exchange_weak( used, used + bytes, std::memory_order_acq_rel, std::memory_order_acquire ) == false );
        return TextureStreamingReservation{ _usedBytes, bytes };
    }
} // namespace sw
