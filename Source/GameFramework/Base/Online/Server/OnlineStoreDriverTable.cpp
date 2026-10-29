#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Module/ModuleLibraryUtil.h"

namespace sw
{
    namespace
    {
        /**
         * @class OnlineStoreDriverSlots
         * @brief 묶인 함수 칸 둘입니다. 묶기는 구독(`Subscription`)을 내주고, 모듈 언로드 리스너라서 핫 리로드가 이미지를 내리기 전에 그 이미지의 함수가 든 칸을 비운다.
         * @details 모듈 로드와 언로드(로더 스레드)에서 쓰고 서버 조립(서비스 스레드)에서 읽으므로 칸은 원자 포인터로 둔다.
         *          비우기는 모두 비교 교환이다 — 칸이 그 함수를 들고 있을 때만 비워, 그사이 새 이미지가 묶은 함수를 지우지 않는다.
         *          구독의 id 는 묶은 함수의 주소다(두 칸의 함수 타입이 달라 한 주소는 한 칸에만 있다).
         */
        class OnlineStoreDriverSlots final : public SubscriptionSource
        {
        public:
            static OnlineStoreDriverSlots& get()
            {
                static OnlineStoreDriverSlots s_slots;
                return s_slots;
            }

            template <typename TFunction>
            [[nodiscard]] Subscription bind( atomic<TFunction>& slot, TFunction pCreate )
            {
                slot.store( pCreate, std::memory_order_release );
                return issueSubscription( static_cast<uint64>( reinterpret_cast<uintptr_t>( pCreate ) ) );
            }

            atomic<OnlineStoreDriverTable::CreateServiceStoreFunction>   _pCreateServiceStore;
            atomic<OnlineStoreDriverTable::CreateEphemeralStoreFunction> _pCreateEphemeralStore;

        protected:
            void releaseSubscription( uint64 id ) override
            {
                const uintptr_t address = static_cast<uintptr_t>( id );
                release( _pCreateServiceStore, reinterpret_cast<OnlineStoreDriverTable::CreateServiceStoreFunction>( address ) );
                release( _pCreateEphemeralStore, reinterpret_cast<OnlineStoreDriverTable::CreateEphemeralStoreFunction>( address ) );
            }

        private:
            OnlineStoreDriverSlots()
                : SubscriptionSource{}
                , _pCreateServiceStore{ nullptr }
                , _pCreateEphemeralStore{ nullptr }
            {
            }

            /** @brief @p slot 이 @p pCreate 를 들고 있을 때만 비웁니다. 다른 함수가 들어 있으면 그대로 둡니다. */
            template <typename TFunction>
            static bool release( atomic<TFunction>& slot, TFunction pCreate )
            {
                TFunction expected = pCreate;
                return pCreate != nullptr && slot.compare_exchange_strong( expected, nullptr, std::memory_order_acq_rel );
            }

            template <typename TFunction>
            static uint32 releaseWithin( atomic<TFunction>& slot, const void* pBegin, const void* pEnd )
            {
                const TFunction pCreate = slot.load( std::memory_order_acquire );
                if ( ModuleLibraryUtil::isAddressWithin( reinterpret_cast<const void*>( pCreate ), pBegin, pEnd ) == false )
                    return 0;
                return release( slot, pCreate ) ? 1 : 0;
            }

        private:
        };
    } // namespace
} // namespace sw

namespace sw
{
    Subscription OnlineStoreDriverTable::bindServiceStoreFactory( CreateServiceStoreFunction pCreate )
    {
        OnlineStoreDriverSlots& slots = OnlineStoreDriverSlots::get();
        return slots.bind( slots._pCreateServiceStore, pCreate );
    }

    Subscription OnlineStoreDriverTable::bindEphemeralStoreFactory( CreateEphemeralStoreFunction pCreate )
    {
        OnlineStoreDriverSlots& slots = OnlineStoreDriverSlots::get();
        return slots.bind( slots._pCreateEphemeralStore, pCreate );
    }

    OnlineStoreDriverTable::CreateServiceStoreFunction OnlineStoreDriverTable::findServiceStoreFactory()
    {
        return OnlineStoreDriverSlots::get()._pCreateServiceStore.load( std::memory_order_acquire );
    }

    OnlineStoreDriverTable::CreateEphemeralStoreFunction OnlineStoreDriverTable::findEphemeralStoreFactory()
    {
        return OnlineStoreDriverSlots::get()._pCreateEphemeralStore.load( std::memory_order_acquire );
    }
} // namespace sw
