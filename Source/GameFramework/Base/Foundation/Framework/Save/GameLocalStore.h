/**
 * @file GameLocalStore.h
 * @brief 게임 인스턴스가 소유하는 로컬 저장소(`ILocalStore`)와 그 완료 배분입니다. 게임 상태 세이브 · 자동 저장이 이 슬롯 저장소 하나를 쓴다.
 * @details 소유자는 게임 범위 서브시스템이다 - 언리얼은 플랫폼 `ISaveGameSystem` 위에 `UGameplayStatics::AsyncSaveGameToSlot`(완료 델리게이트)을, 유니티는 앱 수명의
 *          `persistentDataPath` 를 둔다. 여기서는 봉투 · 봉인 · 백엔드가 GameFramework 에 있어 엔진 범위에 둘 수 없고 슬롯 폴더가 게임마다 다르므로 게임 범위에 둔다.
 *          완료 배분은 기반 `LocalStoreChannel` 이 한다(에디터 쪽 저장소 주인도 같은 기반을 든다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/Base/Online/Local/LocalStoreChannel.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
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
        using CompletionFunction = LocalStoreChannel::CompletionFunction;

        /** @brief 게임 인스턴스가 쓴다 - `initializeSubsystem` 이 파일 백엔드를 `kSaveDirectory` 에 연다(장치 키 · 암호 제공자로 봉인한다). */
        GameLocalStore();
        /** @brief 이미 연 프런트(테스트의 `MemoryLocalStore`)를 받습니다. 서브시스템 수명 함수를 거치지 않고 쓴다. */
        explicit GameLocalStore( unique_ptr<ILocalStore> store );
        ~GameLocalStore() override;

        GameLocalStore( const GameLocalStore& )            = delete;
        GameLocalStore& operator=( const GameLocalStore& ) = delete;

        /** @brief 저장소를 엽니다. 암호 제공자(`INetSecurityProvider`)가 게임 범위에 없거나 열지 못하면 오류를 남기고 실패한다(세이브 없이 서지 않는다). */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 맡긴 일이 바닥날 때까지 완료를 넘기고(콜백이 잇는 쓰기까지) 저장소를 닫습니다(`LocalStoreChannel::drain`). */
        void        stopSubsystem() override { _channel.drain(); }
        void        shutdownSubsystem() override { _channel.close(); }
        const utf8* getSubsystemName() const override { return kSubsystemName; }

        /** @brief 슬롯 하나를 읽습니다(`LocalStoreChannel::submitRead`). */
        [[nodiscard]] Subscription submitRead( string_view slot, const CompletionFunction& onComplete ) { return _channel.submitRead( slot, onComplete ); }
        /** @brief 슬롯 하나에 씁니다(`LocalStoreChannel::submitWrite`). */
        [[nodiscard]] Subscription submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options, const CompletionFunction& onComplete )
        {
            return _channel.submitWrite( slot, std::move( bytes ), options, onComplete );
        }
        /** @brief 슬롯 하나를 지웁니다(`LocalStoreChannel::submitErase`). */
        [[nodiscard]] Subscription submitErase( string_view slot, const CompletionFunction& onComplete ) { return _channel.submitErase( slot, onComplete ); }
        /** @brief 끝난 요청의 콜백을 요청 순서대로 부릅니다(`LocalStoreChannel::update`). */
        void update() { _channel.update(); }
        /** @brief 맡겼지만 아직 콜백을 부르지 않은 요청이 있으면 true 입니다. */
        bool hasPendingRequest() const { return _channel.hasPendingRequest(); }

    private:
        LocalStoreChannel _channel;
    };
} // namespace sw
