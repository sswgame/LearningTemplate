#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"

#include "Core/Delegate/AtomicSlot.h"

namespace sw
{
    namespace
    {
        /**
         * @struct OnlineStoreDriverSlots
         * @brief 묶인 함수 칸 둘입니다. 모듈 로드와 언로드(로더 스레드)에서 걸고 풀고 서버 조립(서비스 스레드)에서 읽으므로 공용 원자 칸(`AtomicSlot`)이다 —
         *        풀기는 칸이 그 함수를 들고 있을 때만 비워, 그사이 새 이미지가 묶은 함수를 지우지 않는다.
         */
        struct OnlineStoreDriverSlots
        {
            static OnlineStoreDriverSlots& get()
            {
                static OnlineStoreDriverSlots s_slots;
                return s_slots;
            }

            AtomicSlot<OnlineStoreDriverTable::CreateServiceStoreFunction>   _createServiceStore;
            AtomicSlot<OnlineStoreDriverTable::CreateEphemeralStoreFunction> _createEphemeralStore;
        };
    } // namespace
} // namespace sw

namespace sw
{
    Subscription OnlineStoreDriverTable::bindServiceStoreFactory( CreateServiceStoreFunction pCreate )
    {
        return OnlineStoreDriverSlots::get()._createServiceStore.bind( pCreate );
    }

    Subscription OnlineStoreDriverTable::bindEphemeralStoreFactory( CreateEphemeralStoreFunction pCreate )
    {
        return OnlineStoreDriverSlots::get()._createEphemeralStore.bind( pCreate );
    }

    OnlineStoreDriverTable::CreateServiceStoreFunction OnlineStoreDriverTable::findServiceStoreFactory()
    {
        return OnlineStoreDriverSlots::get()._createServiceStore.get();
    }

    OnlineStoreDriverTable::CreateEphemeralStoreFunction OnlineStoreDriverTable::findEphemeralStoreFactory()
    {
        return OnlineStoreDriverSlots::get()._createEphemeralStore.get();
    }
} // namespace sw
