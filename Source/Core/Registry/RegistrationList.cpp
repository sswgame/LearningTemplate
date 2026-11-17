#include "pch.h"

#include "Core/Registry/RegistrationList.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace sw
{
    namespace
    {
#if SW_WITH_DYNAMIC_MODULES
        /** @brief 교체 이미지를 올리는 중첩 깊이입니다. 게임 스레드에서만 만지므로 원자일 필요가 없다. */
        int32 s_reloadScopeDepth = 0;
#endif

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
            /// @brief 목록이 저장소를 키울 때 기준선 때 저장소를 보관하는지입니다(`RegistrationListBase::setStorageKeptOnGrowth`). 각 목록이 자기 소유자 잠금 안에서 읽는다.
            atomic<bool> _bStorageKeptOnGrowth{ false };
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

        /**
         * @struct StaticRegistrationState
         * @brief 살아 있는 정적 등록자(`StaticRegistrarSite`)의 사슬과, 겹친 이름으로 거절된 정적 등록의 수 · 앞의 몇 줄입니다.
         * @details 시작 단계가 거절 줄을 읽어 로드 오류로 멈춘다(`collectRejectedStaticRegistrations`). 정적 초기화 중에 쓰므로 할당하지 않는 고정 칸이고 상수 초기화다 —
         *          칸은 한 번 쓰면 바꾸지 않는다(수가 칸을 넘으면 수만 센다).
         */
        struct StaticRegistrationState
        {
            static constexpr uint32 kKeptLineCount = 16;
            static constexpr uint32 kLineBytes     = 512;
            std::atomic_flag        _chainLock     = ATOMIC_FLAG_INIT;
            StaticRegistrarSite*    _pHead{ nullptr };
            std::atomic_flag        _rejectedLock = ATOMIC_FLAG_INIT;
            uint32                  _rejectedCount{ 0 };
            utf8                    _arrRejectedLine[kKeptLineCount][kLineBytes]{};
        };

        StaticRegistrationState s_staticRegistration; // 모든 멤버가 상수 초기화다

        /// @brief 이 스레드가 지금 올리는 중인 정적 등록자(생성자가 등록 함수를 부르는 동안만). 정적 초기화는 이미지를 올리는 스레드에서 돈다.
        thread_local const StaticRegistrarSite* t_pActiveRegistrar{ nullptr };

        /** @brief 스핀 잠금 하나를 잡습니다(임계 구역은 포인터 몇 개 · 줄 하나 복사). 로그를 남기지 않는다. */
        struct SpinLockScope
        {
            explicit SpinLockScope( std::atomic_flag& flag )
                : _flag{ flag }
            {
                while ( _flag.test_and_set( std::memory_order_acquire ) )
                {
                }
            }
            ~SpinLockScope() { _flag.clear( std::memory_order_release ); }
            SpinLockScope( const SpinLockScope& )            = delete;
            SpinLockScope& operator=( const SpinLockScope& ) = delete;

        private:
            std::atomic_flag& _flag;
        };

        /** @brief `파일(줄)` 글입니다. 모르는 자리는 `(unknown site)` 입니다. */
        string describeRegistrationSite( const optional<RegistrationSite>& site )
        {
            if ( site.has_value() == false || site->_pFile == nullptr )
                return "(unknown site)";
            return string( site->_pFile ) + "(" + to_string( site->_line ) + ")";
        }

        /** @brief 겹친 정적 등록 한 줄을 세고, 칸이 남았으면 적습니다. */
        void recordRejectedStaticRegistration( string_view line )
        {
            SpinLockScope lock{ s_staticRegistration._rejectedLock };
            const uint32  slot = s_staticRegistration._rejectedCount++;
            if ( slot >= StaticRegistrationState::kKeptLineCount )
                return;
            const size_t length = std::min<size_t>( line.size(), StaticRegistrationState::kLineBytes - 1 );
            std::memcpy( s_staticRegistration._arrRejectedLine[slot], line.data(), length );
            s_staticRegistration._arrRejectedLine[slot][length] = '\0';
        }

        /** @brief 겹친 이름 하나를 두 자리와 함께 알립니다. @p bStatic 이면 정적 등록 거절로 센다(시작 단계가 로드 오류로 멈춘다). */
        void reportDuplicateName( const utf8* pWhat, string_view name, const optional<RegistrationSite>& existing, const optional<RegistrationSite>& rejected, bool bStatic )
        {
            const string line = string( pWhat ) + " '" + string( name ) + "' is registered twice - first at " + describeRegistrationSite( existing ) + ", again at " +
                                describeRegistrationSite( rejected ) + " (keeping the first)";
            SW_LOG_ERROR_ID( "Registration.DuplicateName", "%#", line.c_str() );
            if ( bStatic )
                recordRejectedStaticRegistration( line );
        }
    } // namespace

    StaticRegistrarSite::StaticRegistrarSite( const void* pItem, RegistrationSite site )
        : _pItem{ pItem }
        , _site{ site }
        , _pPrev{ nullptr }
        , _pNext{ nullptr }
    {
        {
            SpinLockScope lock{ s_staticRegistration._chainLock };
            _pNext = s_staticRegistration._pHead;
            if ( s_staticRegistration._pHead != nullptr )
                s_staticRegistration._pHead->_pPrev = this;
            s_staticRegistration._pHead = this;
        }
        t_pActiveRegistrar = this;
    }

    StaticRegistrarSite::~StaticRegistrarSite()
    {
        if ( t_pActiveRegistrar == this )
            t_pActiveRegistrar = nullptr;
        SpinLockScope lock{ s_staticRegistration._chainLock };
        if ( _pPrev != nullptr )
            _pPrev->_pNext = _pNext;
        else
            s_staticRegistration._pHead = _pNext;
        if ( _pNext != nullptr )
            _pNext->_pPrev = _pPrev;
        _pPrev = nullptr;
        _pNext = nullptr;
    }

    void StaticRegistrarSite::endRegistration()
    {
        if ( t_pActiveRegistrar == this )
            t_pActiveRegistrar = nullptr;
    }

    optional<RegistrationSite> StaticRegistrarSite::findActiveRegistrationSite()
    {
        if ( t_pActiveRegistrar == nullptr )
            return nullopt;
        return t_pActiveRegistrar->_site;
    }

    optional<RegistrationSite> StaticRegistrarSite::findSiteOf( const void* pItem )
    {
        if ( pItem == nullptr )
            return nullopt;
        SpinLockScope lock{ s_staticRegistration._chainLock };
        for ( const StaticRegistrarSite* pSite = s_staticRegistration._pHead; pSite != nullptr; pSite = pSite->_pNext )
        {
            if ( pSite->_pItem == pItem )
                return pSite->_site;
        }
        return nullopt;
    }

    void reportDuplicateRegistration( const utf8* pWhat, string_view name, RegistrationSite existing, RegistrationSite rejected )
    {
        reportDuplicateName( pWhat, name, existing, rejected, true );
    }

    vector<string> collectRejectedStaticRegistrations()
    {
        uint32 count = 0;
        {
            SpinLockScope lock{ s_staticRegistration._rejectedLock };
            count = s_staticRegistration._rejectedCount;
        }
        // 수보다 앞의 칸은 다시 쓰지 않는다 — 잠금 밖에서 읽어도 된다(잠금 안에서 할당하지 않는다).
        vector<string> listLine;
        for ( uint32 slot = 0; slot < count && slot < StaticRegistrationState::kKeptLineCount; ++slot )
            listLine.emplace_back( s_staticRegistration._arrRejectedLine[slot] );
        if ( count > StaticRegistrationState::kKeptLineCount )
            listLine.push_back( "(+" + to_string( count - StaticRegistrationState::kKeptLineCount ) + " more - see the Registration.DuplicateName log lines)" );
        return listLine;
    }

    void reportRegistrationRejected( RegistrationResult result, const utf8* pWhat, string_view name )
    {
        reportRegistrationRejected( result, pWhat, name, nullptr );
    }

    void reportRegistrationRejected( RegistrationResult result, const utf8* pWhat, string_view name, const void* pExistingItem )
    {
        switch ( result )
        {
            case RegistrationResult::DuplicateName:
            {
                // 정적 등록자가 올리는 중이면 시작 때의 겹침이다 — 두 자리를 찍고 센다. 실행 중 손으로 부른 등록(테스트가 일부러 겹치는 것)은 세지 않는다.
                const optional<RegistrationSite> rejected = StaticRegistrarSite::findActiveRegistrationSite();
                reportDuplicateName( pWhat, name, StaticRegistrarSite::findSiteOf( pExistingItem ), rejected, rejected.has_value() );
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

#if SW_WITH_DYNAMIC_MODULES
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
#endif

    RegistrationListBase::RegistrationListBase( const utf8* pListName )
        : _pListName{ pListName }
        , _pPrev{ nullptr }
        , _pNext{ nullptr }
        , _bLinked{ SW_TRUE }
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
        _bLinked = SW_FALSE;
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

    bool RegistrationListBase::setStorageKeptOnGrowth( bool bKeep )
    {
        return s_chain._bStorageKeptOnGrowth.exchange( bKeep, std::memory_order_relaxed );
    }

    bool RegistrationListBase::isStorageKeptOnGrowth()
    {
        return s_chain._bStorageKeptOnGrowth.load( std::memory_order_relaxed );
    }
} // namespace sw
