/**
 * @file GameLocalStore.h
 * @brief 게임 인스턴스가 소유하는 로컬 저장소(`ILocalStore`)와 그 완료 배분입니다. 게임 상태 세이브 · 자동 저장 · 계정 장치 비밀이 모두 이 슬롯 저장소 하나를 쓴다.
 * @details 소유자는 게임 범위 서브시스템이다 - 언리얼은 플랫폼 `ISaveGameSystem` 위에 `UGameplayStatics::AsyncSaveGameToSlot`(완료 델리게이트)을, 유니티는 앱 수명의
 *          `persistentDataPath` 를 둔다. 여기서는 봉투 · 봉인 · 백엔드가 GameFramework 에 있어 엔진 범위에 둘 수 없고 슬롯 폴더가 게임마다 다르므로 게임 범위에 둔다.
 *          완료는 요청마다 단 콜백으로 나눈다(요청 id 로 배분 - 저장소 하나의 완료 소비자는 이 객체 하나다). 요청은 구독(`Subscription`)을 돌려주므로,
 *          콜백 주인이 먼저 사라지면 구독이 풀려 콜백이 불리지 않는다. 완료를 넘긴 요청의 구독은 꺼진다(`isActive` false).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/Base/Online/Local/LocalSlotEnvelope.h"
#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class LocalDeviceKeyProvider;

    /**
     * @class GameLocalStore
     * @brief 게임 인스턴스 서브시스템입니다(`game::findSubsystem<GameLocalStore>()` · 인스턴스의 `getSubsystem<GameLocalStore>()`). 게임 스레드에서만 쓴다.
     * @details 프레임마다 인스턴스가 `update` 를 불러 끝난 요청의 콜백을 요청 순서대로 부른다. 종료 전 단계(`stopSubsystem`)는 맡긴 일과 완료 콜백이 잇는 일을 끝까지 마치고 완료를 넘긴다
     *          (끝낼 때 쓴 세이브를 잃지 않는다 - 남은 읽기는 IOError 다).
     */
    class SW_GF_API GameLocalStore final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "GameLocalStore";
        /** @brief 게임 인스턴스가 여는 저장소의 자리입니다 - 그 게임의 사용자 데이터 폴더 아래 `saves`(슬롯 `save/slot0` 은 `saves/save/slot0.swls`). */
        static constexpr const utf8* kSaveDirectory = "saves";

        /** @brief 끝난 요청 하나를 받습니다. */
        using CompletionFunction = Delegate<void( const LocalStoreCompletion& )>;

        /** @brief 게임 인스턴스가 쓴다 - `initializeSubsystem` 이 파일 백엔드를 `kSaveDirectory` 에 연다(장치 키 · 암호 제공자로 봉인한다). */
        GameLocalStore();
        /** @brief 이미 연 프런트(테스트의 `MemoryLocalStore`)를 받습니다. 서브시스템 수명 함수를 거치지 않고 쓴다. */
        explicit GameLocalStore( unique_ptr<ILocalStore> store );
        ~GameLocalStore() override;

        GameLocalStore( const GameLocalStore& )            = delete;
        GameLocalStore& operator=( const GameLocalStore& ) = delete;

        /** @brief 저장소를 엽니다. 암호 제공자(`INetSecurityProvider`)가 게임 범위에 없거나 열지 못하면 오류를 남기고 실패한다(세이브 없이 서지 않는다). */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 맡긴 일이 바닥날 때까지 완료를 넘기고(콜백이 잇는 쓰기까지) 저장소를 닫습니다(`ILocalStore::shutdown`). */
        void        stopSubsystem() override;
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }

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
        SubscriptionList                   _subscriptions{ SubscriptionList::ReleaseFunction::create<&GameLocalStore::releaseRequest>( this ) };
    };
} // namespace sw
