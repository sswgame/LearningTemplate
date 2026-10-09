#include "pch.h"

#include "Core/Module/ModuleUnloadListener.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /**
         * @struct ModuleUnloadListenerList
         * @brief 살아 있는 리스너 목록과 그 잠금입니다.
         * @details 함수 지역 static 으로 둔다 — 정적 리스너(로그 리스너)가 정적 초기화 중에 오르므로 목록이 그보다 먼저 서야 하고,
         *          먼저 선 것이 나중에 사라지므로 정적 리스너의 소멸이 죽은 목록을 밟지 않는다.
         */
        struct ModuleUnloadListenerList
        {
            mutex                                   _mutex;
            RegistrationList<IModuleUnloadListener> _registeredListener;
        };

        ModuleUnloadListenerList& getModuleUnloadListenerList()
        {
            static ModuleUnloadListenerList s_list;
            return s_list;
        }

        void addListener( IModuleUnloadListener* pListener )
        {
            ModuleUnloadListenerList& list = getModuleUnloadListenerList();
            std::scoped_lock<mutex>   lock{ list._mutex };
            (void)list._registeredListener.add( pListener ); // 생성자에서 한 번 — 같은 객체가 두 번 오를 일은 없다
        }
    } // namespace
} // namespace sw

namespace sw
{
    IModuleUnloadListener::IModuleUnloadListener()
    {
        addListener( this );
    }

    IModuleUnloadListener::IModuleUnloadListener( const IModuleUnloadListener& other )
    {
        (void)other;
        addListener( this );
    }

    IModuleUnloadListener::~IModuleUnloadListener()
    {
        ModuleUnloadListenerList& list = getModuleUnloadListenerList();
        std::scoped_lock<mutex>   lock{ list._mutex };
        (void)list._registeredListener.remove( this ); // 올라 있지 않으면(정적 소멸 순서) 할 일이 없다
    }

    IModuleUnloadListener& IModuleUnloadListener::operator=( const IModuleUnloadListener& other )
    {
        (void)other;
        return *this;
    }

    void IModuleUnloadListener::releaseAllWithin( const void* pBegin, const void* pEnd, vector<ReleaseResult>& outListResult )
    {
        outListResult.clear();
        if ( pBegin == nullptr || pEnd == nullptr )
            return;

        ModuleUnloadListenerList& list = getModuleUnloadListenerList();
        std::scoped_lock<mutex>   lock{ list._mutex };
        outListResult.reserve( list._registeredListener.getCount() );
        for ( IModuleUnloadListener* pListener : list._registeredListener.getItems() )
        {
            ReleaseResult result{};
            result._pListenerName = pListener->getModuleUnloadListenerName();
            result._releasedCount = pListener->onModuleUnloading( pBegin, pEnd, result._bKeepImageMapped );
            outListResult.push_back( result );
        }
    }

    uint32 IModuleUnloadListener::getListenerCount()
    {
        ModuleUnloadListenerList& list = getModuleUnloadListenerList();
        std::scoped_lock<mutex>   lock{ list._mutex };
        return list._registeredListener.getCount();
    }

    bool IModuleUnloadListener::isAddressWithin( const void* pCode, const void* pBegin, const void* pEnd )
    {
        const uintptr_t code = reinterpret_cast<uintptr_t>( pCode );
        return code != 0 && reinterpret_cast<uintptr_t>( pBegin ) <= code && code < reinterpret_cast<uintptr_t>( pEnd );
    }

    const void* IModuleUnloadListener::findVtableAddress( const void* pObject )
    {
        if ( pObject == nullptr )
            return nullptr;
        return *static_cast<const void* const*>( pObject );
    }
} // namespace sw
