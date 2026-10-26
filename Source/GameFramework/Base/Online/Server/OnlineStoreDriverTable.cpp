#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Module/ModuleUnloadListener.h"

namespace sw
{
    namespace
    {
        /**
         * @class OnlineStoreDriverSlots
         * @brief 묶인 함수 칸 둘입니다. 모듈 언로드 리스너라서, 핫 리로드가 이미지를 내리기 전에 그 이미지의 함수가 든 칸을 비운다.
         * @details 모듈 로드와 언로드(로더 스레드)에서 쓰고 서버 조립(서비스 스레드)에서 읽으므로 원자 포인터로 둔다.
         *          비우기는 모두 비교 교환이다 — 칸이 그 함수를 들고 있을 때만 비워, 그사이 새 이미지가 묶은 함수를 지우지 않는다.
         */
        class OnlineStoreDriverSlots final : public IModuleUnloadListener
        {
        public:
            static OnlineStoreDriverSlots& get()
            {
                static OnlineStoreDriverSlots s_slots;
                return s_slots;
            }

            const utf8* getModuleUnloadListenerName() const override { return "online store drivers"; }
            bool        isReleaseExpected() const override { return true; }

            uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
            {
                (void)outKeepImageMapped;
                return releaseWithin( _pCreateServiceStore, pBegin, pEnd ) + releaseWithin( _pCreateEphemeralStore, pBegin, pEnd );
            }

            /** @brief @p slot 이 @p pCreate 를 들고 있을 때만 비웁니다. */
            template <typename TFunction>
            static void release( atomic<TFunction>& slot, TFunction pCreate )
            {
                TFunction expected = pCreate;
                (void)slot.compare_exchange_strong( expected, nullptr, std::memory_order_acq_rel ); // 다른 함수가 들어 있으면 그대로 둔다
            }

            atomic<OnlineStoreDriverTable::CreateServiceStoreFunction>   _pCreateServiceStore;
            atomic<OnlineStoreDriverTable::CreateEphemeralStoreFunction> _pCreateEphemeralStore;

        private:
            OnlineStoreDriverSlots()
                : IModuleUnloadListener{}
                , _pCreateServiceStore{ nullptr }
                , _pCreateEphemeralStore{ nullptr }
            {
            }

            template <typename TFunction>
            static uint32 releaseWithin( atomic<TFunction>& slot, const void* pBegin, const void* pEnd )
            {
                const TFunction pCreate = slot.load( std::memory_order_acquire );
                if ( isAddressWithin( reinterpret_cast<const void*>( pCreate ), pBegin, pEnd ) == false )
                    return 0;
                release( slot, pCreate );
                return 1;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void OnlineStoreDriverTable::bindServiceStoreFactory( CreateServiceStoreFunction pCreate )
    {
        OnlineStoreDriverSlots::get()._pCreateServiceStore.store( pCreate, std::memory_order_release );
    }

    void OnlineStoreDriverTable::bindEphemeralStoreFactory( CreateEphemeralStoreFunction pCreate )
    {
        OnlineStoreDriverSlots::get()._pCreateEphemeralStore.store( pCreate, std::memory_order_release );
    }

    void OnlineStoreDriverTable::unbindServiceStoreFactory( CreateServiceStoreFunction pCreate )
    {
        OnlineStoreDriverSlots::release( OnlineStoreDriverSlots::get()._pCreateServiceStore, pCreate );
    }

    void OnlineStoreDriverTable::unbindEphemeralStoreFactory( CreateEphemeralStoreFunction pCreate )
    {
        OnlineStoreDriverSlots::release( OnlineStoreDriverSlots::get()._pCreateEphemeralStore, pCreate );
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
