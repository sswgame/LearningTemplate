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
            std::thread::id                         _walkingThread; ///< `releaseAllWithin` 이 잠금을 쥐고 도는 스레드(돌지 않으면 빈 id)
        };

        ModuleUnloadListenerList& getModuleUnloadListenerList()
        {
            static ModuleUnloadListenerList s_list;
            return s_list;
        }

        /** @brief 지금 스레드가 훑기 중인 그 스레드인지 묻습니다(그러면 이미 잠금을 쥐고 있다). */
        bool isWalkingThread( const ModuleUnloadListenerList& list )
        {
            return list._walkingThread == std::this_thread::get_id();
        }

        void addListener( IModuleUnloadListener* pListener )
        {
            ModuleUnloadListenerList& list = getModuleUnloadListenerList();
            if ( isWalkingThread( list ) )
            {
                (void)list._registeredListener.add( pListener ); // 훑기 안에서 — 잠금은 훑기가 쥐고 있다
                return;
            }
            std::scoped_lock<mutex> lock{ list._mutex };
            (void)list._registeredListener.add( pListener ); // 생성자에서 한 번 — 같은 객체가 두 번 오를 일은 없다
        }

        void removeListener( IModuleUnloadListener* pListener )
        {
            ModuleUnloadListenerList& list = getModuleUnloadListenerList();
            if ( isWalkingThread( list ) )
            {
                (void)list._registeredListener.remove( pListener ); // 훑기 안에서 — 훑기는 사본을 돌고 지워진 것은 건너뛴다
                return;
            }
            std::scoped_lock<mutex> lock{ list._mutex };
            (void)list._registeredListener.remove( pListener ); // 올라 있지 않으면(정적 소멸 순서) 할 일이 없다
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
        removeListener( this );
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
        // 사본을 돈다 — 리스너가 훑기 안에서 다른 리스너를 만들거나 지울 수 있다(같은 스레드는 잠금 없이 목록을 고친다).
        const vector<IModuleUnloadListener*> listSnapshot = list._registeredListener.getItems();
        list._walkingThread                               = std::this_thread::get_id();
        outListResult.reserve( listSnapshot.size() );
        for ( IModuleUnloadListener* pListener : listSnapshot )
        {
            const vector<IModuleUnloadListener*>& listLive = list._registeredListener.getItems();
            if ( std::find( listLive.begin(), listLive.end(), pListener ) == listLive.end() )
                continue; // 앞 리스너의 훑기 안에서 지워졌다
            ReleaseResult result{};
            result._pListenerName = pListener->getModuleUnloadListenerName();
            result._bExpected     = pListener->isReleaseExpected();
            result._releasedCount = pListener->onModuleUnloading( pBegin, pEnd, result._bKeepImageMapped );
            outListResult.push_back( result );
        }
        list._walkingThread = std::thread::id{};
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
