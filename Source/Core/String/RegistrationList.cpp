#include "pch.h"

#include "Core/String/RegistrationList.h"

#include "Core/Log/Logger.h"

#include <atomic>

namespace sw
{
    namespace
    {
        /** @brief 교체 이미지를 올리는 중첩 깊이입니다. 게임 스레드에서만 만지므로 원자일 필요가 없다. */
        int32 s_reloadScopeDepth = 0;

        /**
         * @struct RegistrationListChain
         * @brief 살아 있는 목록의 연결 목록 머리와 그 잠금입니다. 둘 다 상수 초기화라 다른 번역 단위의 정적 초기화가 먼저 와도 안전합니다.
         * @details 잠금은 연결과 끊기, 열거 동안만 잡는 스핀입니다(임계 구역은 포인터 몇 개). 로그를 남기지 않습니다 — 로거의 정적 리스너 목록도 이 길을 지납니다.
         */
        struct RegistrationListChain
        {
            std::atomic_flag      _lock = ATOMIC_FLAG_INIT;
            RegistrationListBase* _pHead{ nullptr };
            uint32                _count{ 0 };
        };

        RegistrationListChain s_chain; // 모든 멤버가 상수 초기화다(정적 초기화 순서와 무관)

        struct RegistrationListChainLock
        {
            RegistrationListChainLock()
            {
                while ( s_chain._lock.test_and_set( std::memory_order_acquire ) )
                {
                }
            }
            ~RegistrationListChainLock() { s_chain._lock.clear( std::memory_order_release ); }
            RegistrationListChainLock( const RegistrationListChainLock& )            = delete;
            RegistrationListChainLock& operator=( const RegistrationListChainLock& ) = delete;
        };
    } // namespace

    SW_LOG_CALLER( "Registration" );

    void reportRegistrationRejected( RegistrationResult result, const utf8* pWhat, string_view name )
    {
        switch ( result )
        {
            case RegistrationResult::DuplicateName:
            {
                SW_LOG_ERROR_ID( "Registration.DuplicateName", "%# '%#' is already registered - keeping the first", pWhat, name );
                return;
            }
            case RegistrationResult::EmptyName:
            {
                SW_LOG_ERROR_ID( "Registration.EmptyName", "%# was registered with an empty name - ignored", pWhat );
                return;
            }
            case RegistrationResult::NullItem:
            {
                SW_LOG_ERROR_ID( "Registration.NullItem", "%# was registered as null - ignored", pWhat );
                return;
            }
            case RegistrationResult::Added:
            case RegistrationResult::AlreadyPresent:
            {
                return;
            }
        }
    }

    RegistrationReloadScope::RegistrationReloadScope()
    {
        ++s_reloadScopeDepth;
    }

    RegistrationReloadScope::~RegistrationReloadScope()
    {
        --s_reloadScopeDepth;
    }

    bool RegistrationReloadScope::isActive()
    {
        return s_reloadScopeDepth > 0;
    }

    RegistrationListBase::RegistrationListBase( const utf8* pListName )
        : _pListName{ pListName }
        , _pPrev{ nullptr }
        , _pNext{ nullptr }
        , _bLinked{ true }
    {
        RegistrationListChainLock lock;
        _pNext = s_chain._pHead;
        if ( s_chain._pHead != nullptr )
            s_chain._pHead->_pPrev = this;
        s_chain._pHead = this;
        ++s_chain._count;
    }

    RegistrationListBase::~RegistrationListBase()
    {
        unlink();
    }

    void RegistrationListBase::unlink()
    {
        RegistrationListChainLock lock;
        if ( _bLinked == false )
            return;
        if ( _pPrev != nullptr )
            _pPrev->_pNext = _pNext;
        else
            s_chain._pHead = _pNext;
        if ( _pNext != nullptr )
            _pNext->_pPrev = _pPrev;
        _pPrev   = nullptr;
        _pNext   = nullptr;
        _bLinked = false;
        --s_chain._count;
    }

    void RegistrationListBase::collect( vector<const RegistrationListBase*>& outListRegistration )
    {
        outListRegistration.clear();
        // 잠금 안에서 할당하지 않는다 — 할당 경로가 목록을 만들면 교착한다. 용량을 잠금 밖에서 잡고, 그 사이 늘어난 목록은 이번 열거에서 뺀다.
        outListRegistration.reserve( getListCount() );
        RegistrationListChainLock lock;
        for ( const RegistrationListBase* pList = s_chain._pHead; pList != nullptr && outListRegistration.size() < outListRegistration.capacity(); pList = pList->_pNext )
        {
            outListRegistration.push_back( pList );
        }
    }

    uint32 RegistrationListBase::getListCount()
    {
        RegistrationListChainLock lock;
        return s_chain._count;
    }
} // namespace sw
