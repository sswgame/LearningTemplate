/**
 * @file GameSaveSlots.h
 * @brief 게임 상태 세이브 슬롯 — 슬롯에 저장 · 불러오기를 맡기고, 다른 씬의 세이브는 그 씬을 연 뒤 적용하며, 깨진 복원 뒤의 세이브 막기를 듭니다.
 * @details 게임 인스턴스의 수명주기 · 프레임 순서와 정책이 다르다(비동기 요청 · 씬 이동 · 저장 막기) — 인스턴스는 봉투를 찍고 되살리기만 하고(`captureSnapshot` · `restoreSnapshot`)
 *          무엇을 언제 어느 슬롯에 쓰는지는 여기서 정한다. 언리얼 `UGameplayStatics::AsyncSaveGameToSlot` 을 부르는 게임 인스턴스 서브시스템 자리다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Service/GameInstanceSubsystem.h"

namespace sw
{
    struct GameSettings;
    struct LocalStoreCompletion;

    class GameInstance;
    class SaveLock;
    class Scene;

    /**
     * @brief 끝난 게임 상태 세이브 슬롯 쓰기 · 읽기(복원까지) 중 성공한 수입니다. 감시 값 `Save.Written` · `Save.Loaded` 가 읽는다.
     * @details 세이브 찍기 시간(`Save.CaptureMs`)은 여기 없다 — 프로파일러 구간으로 재서 배포에 칸도 비용도 남지 않는다.
     */
    struct SaveSlotCounts
    {
        uint32 _written{ 0 };
        uint32 _loaded{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @class GameSaveSlots @brief 게임 인스턴스 서브시스템입니다(`game::findSubsystem<GameSaveSlots>()`). 파일 머리말 참고. 게임 스레드에서만 쓴다. */
    class SW_GF_API GameSaveSlots final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "GameSaveSlots";

        /** @brief 세이브 적용의 끝을 받습니다(성공이면 true) — 자동 저장 되돌리기의 완료(`AutosaveManager::RestoreCompletedFunction`)가 여기 온다. */
        using AppliedFunction = Delegate<void( bool bRestored )>;

        /** @brief 셋 다 빌린다 — 게임 인스턴스가 이 서브시스템을 소유하고, 막기 문은 게임 범위보다 먼저 서고 늦게 진다. */
        GameSaveSlots( GameInstance& owner, const GameSettings& settings, SaveLock& saveLock );
        ~GameSaveSlots() override;

        GameSaveSlots( const GameSaveSlots& )            = delete;
        GameSaveSlots& operator=( const GameSaveSlots& ) = delete;

        /** @brief 씬을 기다리던 세이브를 잊습니다(맡긴 쓰기는 `GameLocalStore::stopSubsystem` 이 끝낸다). */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 씬 흐름 · 로컬 저장소 위에 선다. */
        span<const utf8* const> getSubsystemDependencies() const override;

        /**
         * @brief 씬과 게임 상태 전체를 로컬 저장 슬롯 @p slot 에 맡깁니다(`GameLocalStore` - 게임 스레드는 디스크를 기다리지 않는다). 비우면 기본 슬롯(`GameSettings::_defaultSaveSlot`)입니다.
         * @details 슬롯 이름은 `ILocalStore::isValidSlotName` 규칙이다(`save/slot0`). 언리얼 `UGameplayStatics::AsyncSaveGameToSlot` 자리다.
         *          슬롯이 정해졌으면 쓰기가 끝난 뒤의 `update` 가(캡처가 실패하면 바로) 성공 · 실패와 함께 `SaveGameSavedEvent` 를 "game" 채널에 냅니다.
         * @return 쓰기를 맡겼으면 true 입니다. 슬롯이 없거나 틀리면 알리고 이벤트 없이 false, 캡처가 실패하면 실패 이벤트를 내고 false 입니다.
         */
        [[nodiscard]] bool save( string_view slot = {} );

        /**
         * @brief 로컬 저장 슬롯에서 씬과 게임 상태 전체를 불러오기를 맡깁니다. 비우면 기본 슬롯입니다.
         * @details 읽기가 끝나면 봉투 머리의 씬 정체를 본다. 지금 활성 씬이면 바로 복원하고, 다른 씬이면 `GameSceneFlow::requestSceneThen` 으로 그 씬을 연 뒤
         *          (`SceneLoadCompletedEvent` 바로 뒤) 복원한다(감사 G-7 — 언리얼 게임은 세이브의 맵으로 `OpenLevel` 한 뒤 적용한다). 어느 쪽이든 끝에 성공 · 실패와 함께
         *          `SaveGameLoadedEvent` 를 "game" 채널에 냅니다.
         * @return 읽기를 맡겼으면 true 입니다. 슬롯이 없거나 틀리거나, 앞의 세이브가 아직 씬을 옮기는 중이면 알리고 이벤트 없이 false 입니다.
         */
        [[nodiscard]] bool load( string_view slot = {} );
        /** @brief 성공한 세이브 슬롯 쓰기 · 읽기 수입니다. */
        const SaveSlotCounts& getCounts() const { return _counts; }
        /**
         * @brief 읽은 세이브 바이트를 적용합니다 — 슬롯 불러오기와 자동 저장 되돌리기가 함께 지나는 한 길(감사 G-7).
         * @details 옛 판 봉투는 먼저 이 빌드의 판으로 올린다(`GameStateSnapshot::upgradeEnvelope`). 봉투 머리의 씬 정체가 활성 씬이면 바로 복원하고, 다른 씬이면 `GameSceneFlow::requestSceneThen` 으로 그 씬을 연 뒤 복원한다. 끝에 `SaveGameLoadedEvent` 를 내고 @p onApplied 를 부른다.
         *          앞의 세이브가 씬을 옮기는 중이면 적용하지 않고 실패로 끝낸다.
         */
        void applySlot( const string& slot, vector<uint8> bytes, const AppliedFunction& onApplied );
        /** @brief 복원이 씬을 비운 뒤 졌습니다(`GameInstance::readStateEnvelope`) — 세이브 막기를 걸고, 세이브 목적이면 씬을 디스크에서 다시 엽니다. */
        void notifySceneBroken( SerializePurpose purpose );
        /** @brief 복원이 성공했습니다 — 깨진 씬이 덮였으니 막기를 풉니다. */
        void notifySceneRestored();

    private:
        /** @brief 세이브 슬롯 이름(비면 기본 슬롯)을 정합니다. 없거나 슬롯 이름 규칙을 어기면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool resolveSlot( string_view slot, string& outSlot ) const;
        /** @brief 세이브 쓰기 완료입니다 - `SaveGameSavedEvent` 를 냅니다. */
        void onSlotWritten( const LocalStoreCompletion& completion );
        /** @brief 세이브 읽기 완료입니다 - 형식을 확인하고 `applySlot` 에 넘깁니다. */
        void onSlotRead( const LocalStoreCompletion& completion );
        /** @brief 찍은 씬이 열린 뒤입니다(`GameSceneFlow::requestSceneThen`) — 기다리던 세이브를 적용하고 `SaveGameLoadedEvent` 를 냅니다. @p pScene 이 nullptr 이면 실패로 알린다. */
        void onSaveSceneLoaded( Scene* pScene );
        /** @brief 성공한 읽기 수를 세고 `SaveGameLoadedEvent` 를 낸 뒤 @p onApplied 가 묶였으면 부릅니다. */
        void publishSlotLoaded( const string& slot, bool bLoaded, const AppliedFunction& onApplied );
        /** @brief 교체에서 진 복원 뒤 활성 씬을 그 리소스에서 다시 엽니다. 리소스가 없거나 요청이 실패하면 알리고 막기는 남는다. */
        void reopenBrokenScene();
        /** @brief 깨진 씬이 다시 열린 뒤입니다 — 섰으면 깨진 복원의 세이브 막기를 풉니다. */
        void onBrokenSceneReopened( Scene* pScene );
        /** @brief 끝난(꺼진) 세이브 요청 구독을 걷고 @p request 를 듭니다. */
        void holdSlotRequest( Subscription request );

    private:
        GameInstance&        _owner;
        const GameSettings&  _settings;
        SaveLock&            _saveLock;
        vector<Subscription> _listSlotRequest; ///< 맡긴 세이브 쓰기 · 읽기의 완료 구독(끝난 것은 꺼져 다음 요청이 걷는다)
        /** @brief 찍은 씬이 열리기를 기다리는 세이브 하나입니다(`onSlotRead` → `onSaveSceneLoaded`). */
        struct PendingSlotRestore
        {
            string          _slot;      ///< 읽은 슬롯 이름
            vector<uint8>   _bytes;     ///< 읽은 봉투 바이트
            AppliedFunction _onApplied; ///< 적용의 끝을 받을 함수(자동 저장 되돌리기 — 슬롯 불러오기는 묶지 않는다)
        };
        optional<PendingSlotRestore> _pendingSlotRestore; ///< 씬을 옮기는 중인 세이브(없으면 없음) — 하나만 둔다, 겹친 불러오기는 거절한다
        /**
         * @brief 마지막 복원이 교체 중에 졌다 — 씬은 비워져 일부만 섰다. 다음 성공한 복원까지 세이브를 막는다(감사 P0-6).
         * @details 예전에는 실패를 경고만 하고 간격 자동 저장(예: 300 초 · 슬롯 셋)이 깨진 씬을 15 분 안에 모든 슬롯에 썼다. 수동 · 간격 · 종료 저장이 모두
         *          `captureSnapshot( SaveGame )` 한 곳을 지나므로 거기서 문(`_saveLock`)을 본다.
         */
        Subscription   _brokenRestoreBlock;
        SaveSlotCounts _counts; ///< 성공한 세이브 슬롯 쓰기 · 읽기 수
    };
} // namespace sw
