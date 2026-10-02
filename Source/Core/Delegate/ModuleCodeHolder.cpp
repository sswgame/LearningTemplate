#include "pch.h"

#include "Core/Delegate/ModuleCodeHolder.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"

namespace sw
{
    namespace
    {
        /**
         * @struct ModuleCodeHolderList
         * @brief 살아 있는 보유자 목록과 그 잠금입니다.
         * @details 함수 지역 static 으로 둔다 — 정적 보유자(로그 리스너)가 정적 초기화 중에 오르므로 목록이 그보다 먼저 서야 하고,
         *          먼저 선 것이 나중에 사라지므로 정적 보유자의 소멸이 죽은 목록을 밟지 않는다.
         */
        struct ModuleCodeHolderList
        {
            mutex                      _mutex;
            vector<IModuleCodeHolder*> _listHolder;
        };

        ModuleCodeHolderList& getModuleCodeHolderList()
        {
            static ModuleCodeHolderList s_list;
            return s_list;
        }

        void addHolder( IModuleCodeHolder* pHolder )
        {
            ModuleCodeHolderList&   list = getModuleCodeHolderList();
            std::scoped_lock<mutex> lock{ list._mutex };
            list._listHolder.push_back( pHolder );
        }
    } // namespace
} // namespace sw

namespace sw
{
    IModuleCodeHolder::IModuleCodeHolder()
    {
        addHolder( this );
    }

    IModuleCodeHolder::IModuleCodeHolder( const IModuleCodeHolder& other )
    {
        (void)other;
        addHolder( this );
    }

    IModuleCodeHolder::~IModuleCodeHolder()
    {
        ModuleCodeHolderList&   list = getModuleCodeHolderList();
        std::scoped_lock<mutex> lock{ list._mutex };
        const auto              it = std::find( list._listHolder.begin(), list._listHolder.end(), this );
        if ( it != list._listHolder.end() )
            list._listHolder.erase( it );
    }

    IModuleCodeHolder& IModuleCodeHolder::operator=( const IModuleCodeHolder& other )
    {
        (void)other;
        return *this;
    }

    void IModuleCodeHolder::releaseAllWithin( const void* pBegin, const void* pEnd, vector<ReleaseResult>& outListResult )
    {
        outListResult.clear();
        if ( pBegin == nullptr || pEnd == nullptr )
            return;

        ModuleCodeHolderList&   list = getModuleCodeHolderList();
        std::scoped_lock<mutex> lock{ list._mutex };
        outListResult.reserve( list._listHolder.size() );
        for ( IModuleCodeHolder* pHolder : list._listHolder )
        {
            ReleaseResult result{};
            result._pHolderName   = pHolder->getModuleCodeHolderName();
            result._releasedCount = pHolder->releaseModuleCodeWithin( pBegin, pEnd, result._bKeepImageMapped );
            outListResult.push_back( result );
        }
    }

    uint32 IModuleCodeHolder::getHolderCount()
    {
        ModuleCodeHolderList&   list = getModuleCodeHolderList();
        std::scoped_lock<mutex> lock{ list._mutex };
        return static_cast<uint32>( list._listHolder.size() );
    }

    bool IModuleCodeHolder::isAddressWithin( const void* pCode, const void* pBegin, const void* pEnd )
    {
        const uintptr_t code = reinterpret_cast<uintptr_t>( pCode );
        return code != 0 && reinterpret_cast<uintptr_t>( pBegin ) <= code && code < reinterpret_cast<uintptr_t>( pEnd );
    }

    const void* IModuleCodeHolder::findVtableAddress( const void* pObject )
    {
        if ( pObject == nullptr )
            return nullptr;
        return *static_cast<const void* const*>( pObject );
    }
} // namespace sw
