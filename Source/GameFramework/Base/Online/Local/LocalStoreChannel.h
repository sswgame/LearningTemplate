/**
 * @file LocalStoreChannel.h
 * @brief 연 로컬 저장소(`ILocalStore`) 하나와 그 완료 배분입니다. 게임 범위의 `GameLocalStore` 와 에디터 쪽 저장소 주인이 같은 기반으로 든다.
 * @details 저장소 하나의 완료 소비자는 이 객체 하나다 - 완료는 요청마다 단 콜백으로 나눈다(요청 id 로 배분). 요청은 구독(`Subscription`)을 돌려주므로
 *          콜백 주인이 먼저 사라지면 구독이 풀려 콜백이 불리지 않는다. 완료를 넘긴 요청의 구독은 꺼진다(`isActive` false).
 *          언리얼 `UGameplayStatics::AsyncSaveGameToSlot`(완료 델리게이트)의 자리다. 주인(서브시스템)은 프레임마다 `update` 를 부르고, 내릴 때 `drain` → `close` 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/File/UserDataPath.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Local/LocalSlotEnvelope.h"
#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class INetSecurityProvider;
    class LocalDeviceKeyProvider;

    /**
     * @class LocalStoreChannel
     * @brief 로컬 저장소 하나를 열고 요청별 완료 콜백을 요청 순서대로 부릅니다. 한 스레드(주인의 스레드)에서만 쓴다.
     */
    class SW_GF_API LocalStoreChannel
    {
    public:
        /** @brief 끝난 요청 하나를 받습니다. */
        using CompletionFunction = Delegate<void( const LocalStoreCompletion& )>;

        LocalStoreChannel();
        /** @brief 이미 연 프런트(테스트의 `MemoryLocalStore`)를 받습니다. */
        explicit LocalStoreChannel( unique_ptr<ILocalStore> store );
        ~LocalStoreChannel();

        LocalStoreChannel( const LocalStoreChannel& )            = delete;
        LocalStoreChannel& operator=( const LocalStoreChannel& ) = delete;

        /**
         * @brief 파일 백엔드를 @p location 에 엽니다(@p gameName 의 사용자 데이터 폴더 기준).
         * @param pSecurityProvider 봉인 슬롯의 암호 제공자 - 주면 장치 키(`device.key`)로 봉인하고, nullptr 이면 봉인 없는 저장소다(봉인 쓰기는 `Invalid` 로 끝난다).
         * @return 열지 못하면 false 와 까닭(@p outError)입니다.
         */
        [[nodiscard]] bool open( string_view gameName, const LocalStoreLocation& location, INetSecurityProvider* pSecurityProvider, string& outError );
        /** @brief 저장소가 열려 있으면 true 입니다. */
        bool isOpen() const { return _store != nullptr; }
        /** @brief 맡긴 일과 완료 콜백이 잇는 일이 바닥날 때까지 완료를 넘기고(끝낼 때 쓴 세이브를 잃지 않는다 - 남은 읽기는 IOError) 저장소를 닫습니다(`ILocalStore::shutdown`). */
        void drain();
        /** @brief 남은 구독을 끊고 저장소 · 봉인 문맥 · 키를 놓습니다. */
        void close();

        /** @brief 슬롯 하나를 읽습니다. 끝나면 @p onComplete 를 부릅니다. 구독을 놓으면 콜백을 부르지 않습니다. */
        [[nodiscard]] Subscription submitRead( string_view slot, const CompletionFunction& onComplete );
        /** @brief 슬롯 하나에 씁니다(슬롯 단위 원자 쓰기). 끝나면 @p onComplete 를 부릅니다. 구독을 놓아도 쓰기는 끝까지 한다. */
        [[nodiscard]] Subscription submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options, const CompletionFunction& onComplete );
        /** @brief 슬롯 하나를 지웁니다(없는 슬롯은 저장소가 정한 결과로 끝난다). 구독을 놓아도 지우기는 끝까지 한다. */
        [[nodiscard]] Subscription submitErase( string_view slot, const CompletionFunction& onComplete );
        /** @brief 끝난 요청의 콜백을 요청 순서대로 부릅니다. 콜백 안에서 새 요청을 맡겨도 된다(다음 `update` 에 끝난다). */
        void update();
        /** @brief 맡겼지만 아직 콜백을 부르지 않은 요청이 있으면 true 입니다. */
        bool hasPendingRequest() const { return _listPendingRequest.empty() == false; }

    private:
        struct PendingRequest
        {
            CompletionFunction _onComplete;
            uint64             _requestID;
        };

        [[nodiscard]] Subscription track( uint64 requestID, const CompletionFunction& onComplete );
        /** @brief 구독이 풀렸다 - 그 요청의 콜백을 지운다(요청 자체는 저장소가 끝까지 한다). */
        void releaseRequest( uint64 requestID );

        unique_ptr<LocalDeviceKeyProvider> _keyProvider; ///< 봉인 키(장치 키). 저장소보다 먼저 서고 늦게 진다
        LocalSealContext                   _sealContext; ///< 저장소가 빌려 쓴다
        unique_ptr<ILocalStore>            _store;
        vector<PendingRequest>             _listPendingRequest;
        SubscriptionList                   _subscriptions{ SubscriptionList::ReleaseFunction::create<&LocalStoreChannel::releaseRequest>( this ) };
    };
} // namespace sw
