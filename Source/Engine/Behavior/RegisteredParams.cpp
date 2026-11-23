#include "pch.h"

#include "Engine/Behavior/RegisteredParams.h"

#include "Core/Registry/RegistrationList.h"

#include <new>

namespace sw
{
    RegisteredParams::RegisteredParams()
        : _arrStorage{}
        , _pRaw{ nullptr }
        , _pEntry{ nullptr }
        , _pEntries{ nullptr }
        , _generation{ 0 }
    {
    }

    RegisteredParams::RegisteredParams( shared_ptr<const void> pParams, const void* pEntry, const LockedRegistrationListBase& entries, uint32 generation )
        : _arrStorage{}
        , _pRaw{ pParams.get() }
        , _pEntry{ pEntry }
        , _pEntries{ &entries }
        , _generation{ generation }
    {
        if ( _pRaw != nullptr )
            sw_placement_new( _arrStorage ) shared_ptr<const void>( std::move( pParams ) );
    }

    RegisteredParams::RegisteredParams( const RegisteredParams& other )
        : _arrStorage{}
        , _pRaw{ other._pRaw }
        , _pEntry{ other._pEntry }
        , _pEntries{ other._pEntries }
        , _generation{ other._generation }
    {
        // 참조 수 올리기는 이 이미지의 인라인 원자 연산이다 — 줄이 빠진 뒤에 복사해도 내린 코드를 부르지 않는다.
        if ( _pRaw != nullptr )
            sw_placement_new( _arrStorage ) shared_ptr<const void>( other.getShared() );
    }

    RegisteredParams& RegisteredParams::operator=( const RegisteredParams& other )
    {
        if ( this == &other )
            return *this;
        release();
        _pRaw       = other._pRaw;
        _pEntry     = other._pEntry;
        _pEntries   = other._pEntries;
        _generation = other._generation;
        if ( _pRaw != nullptr )
            sw_placement_new( _arrStorage ) shared_ptr<const void>( other.getShared() );
        return *this;
    }

    RegisteredParams::RegisteredParams( RegisteredParams&& other ) noexcept
        : _arrStorage{}
        , _pRaw{ other._pRaw }
        , _pEntry{ other._pEntry }
        , _pEntries{ other._pEntries }
        , _generation{ other._generation }
    {
        if ( _pRaw == nullptr )
            return;
        sw_placement_new( _arrStorage ) shared_ptr<const void>( std::move( other.getShared() ) );
        other.getShared().~shared_ptr<const void>(); // 옮긴 뒤 빈 포인터라 해제 코드를 부르지 않는다
        other._pRaw = nullptr;
    }

    RegisteredParams& RegisteredParams::operator=( RegisteredParams&& other ) noexcept
    {
        if ( this == &other )
            return *this;
        release();
        _pRaw       = other._pRaw;
        _pEntry     = other._pEntry;
        _pEntries   = other._pEntries;
        _generation = other._generation;
        if ( _pRaw == nullptr )
            return *this;
        sw_placement_new( _arrStorage ) shared_ptr<const void>( std::move( other.getShared() ) );
        other.getShared().~shared_ptr<const void>();
        other._pRaw = nullptr;
        return *this;
    }

    RegisteredParams::~RegisteredParams()
    {
        release();
    }

    bool RegisteredParams::isEntryRegistered() const
    {
        if ( _pRaw == nullptr || _pEntries == nullptr )
            return true;
        return _generation == _pEntries->getRemovalGeneration() || _pEntries->containsAddress( _pEntry );
    }

    void RegisteredParams::release()
    {
        if ( _pRaw == nullptr )
            return;
        // 줄이 빠졌으면 값의 해제 코드(제어 블록)가 내린 이미지에 있다 — 부르지 않고 칸을 버린다(일부러 남기는 누수).
        if ( isEntryRegistered() )
            getShared().~shared_ptr<const void>();
        _pRaw = nullptr;
    }
} // namespace sw
