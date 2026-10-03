#include "pch.h"

#include "Core/Module/ModuleCodeHolder.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/RegistrationList.h"

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
            mutex                               _mutex;
            RegistrationList<IModuleCodeHolder> _registeredHolder;
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
            (void)list._registeredHolder.add( pHolder ); // 생성자에서 한 번 — 같은 객체가 두 번 오를 일은 없다
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
        (void)list._registeredHolder.remove( this ); // 올라 있지 않으면(정적 소멸 순서) 할 일이 없다
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
        outListResult.reserve( list._registeredHolder.getCount() );
        for ( IModuleCodeHolder* pHolder : list._registeredHolder.getItems() )
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
        return list._registeredHolder.getCount();
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
