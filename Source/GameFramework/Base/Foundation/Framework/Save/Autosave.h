/**
 * @file Autosave.h
 * @brief 자동 저장 정책을 정의합니다.
 * @details 언제(간격, 지역 이동, 체크포인트, 보스 앞, 종료) 어느 슬롯(순환 슬롯)에 저장하는지와 체크포인트로 되돌리는 방법을 정한다. UI 는 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct LocalStoreCompletion;

    class GameLocalStore;
    class XMLNode;

    /** @brief 자동 저장을 부른 까닭입니다. 슬롯 정보에 함께 적히며, 목록 UI 와 체크포인트 되돌리기가 이것으로 슬롯을 고릅니다. */
    ENUM()
    enum class AutosaveTrigger : uint8
    {
        Interval = 0, ///< 정한 간격이 지났다
        AreaChanged,  ///< 지역 · 맵을 옮겼다
        Checkpoint,   ///< 체크포인트에 닿았다(되돌리기의 기준)
        BeforeBoss,   ///< 보스 방 · 큰 싸움 앞
        Quit,         ///< 게임을 끈다
        Manual        ///< 게임 코드가 직접 불렀다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AutosaveSettings
     * @brief 자동 저장 정책의 수치입니다. 빠진 속성은 기본값을 씁니다.
     * @code
     *     <Autosave interval="300" minimumGap="30" slots="3" group="autosave" prefix="slot"
     *               areaChange="true" checkpoint="true" beforeBoss="true" quit="true"/>
     * @endcode
     */
    struct SW_GF_API AutosaveSettings
    {
        /** @brief 로컬 저장 슬롯 그룹입니다(`ILocalStore` 슬롯 이름 규칙의 그룹 - `[0-9a-z_.-]`). 자리(사용자 데이터 폴더)는 `GameLocalStore` 가 정한다 */
        string  _group{ "autosave" };
        string  _prefix{ "slot" };         ///< 슬롯 이름의 접두사. 슬롯은 `<그룹>/<접두사><번호>`, 기록은 그 뒤에 `.info` 다
        float32 _interval{ 300.0f };       ///< 간격 자동 저장(초, 플레이 시간). 0 이면 끈다
        float32 _minimumGap{ 30.0f };      ///< 두 자동 저장 사이의 최소 간격(초). 종료 · 직접 부른 것은 이 간격을 무시한다
        int32   _slotCount{ 3 };           ///< 순환해 쓰는 슬롯 수(1 이상). 새 저장은 가장 새 슬롯을 건드리지 않는다
        uint8   _bOnAreaChange{ SW_TRUE }; ///< 지역 이동에 저장한다
        uint8   _bOnCheckpoint{ SW_TRUE }; ///< 체크포인트에 저장한다
        uint8   _bOnBeforeBoss{ SW_TRUE }; ///< 보스 앞에 저장한다
        uint8   _bOnQuit{ SW_TRUE };       ///< 종료에 저장한다

        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );
        /** @brief 슬롯 번호의 로컬 저장 슬롯 이름입니다(`autosave/slot0`). */
        string makeSlotName( int32 slot ) const;
        /** @brief 슬롯 번호의 기록 슬롯 이름입니다(`autosave/slot0.info`). */
        string makeInfoSlotName( int32 slot ) const;

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    /** @brief 슬롯 하나의 기록입니다. 상태 슬롯 바로 뒤에 기록 슬롯(`.info`)에 원자적으로 씁니다. */
    struct AutosaveSlotInfo
    {
        string          _slotName{};
        hashed_string   _label{};       ///< 지역, 체크포인트, 보스 이름
        uint64          _sequence{ 0 }; ///< 저장 순번. 클수록 새것이다
        float64         _playTime{ 0.0 };
        int32           _slot{ -1 };
        AutosaveTrigger _trigger{ AutosaveTrigger::Manual };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutosaveManager
     * @brief 자동 저장의 시점, 위치, 되돌리기를 맡습니다.
     * @details 상태 바이트를 만들고 되살리는 일은 게임이 넘긴 델리게이트(보통 `GameInstance::captureSnapshot` 과 `restoreSnapshot` 의 `SerializePurpose::SaveGame`)가 하고,
     *          슬롯 읽기 · 쓰기는 이 관리자가 로컬 저장소(`GameLocalStore`)에 맡긴다 - 게임 스레드는 디스크를 기다리지 않는다.
     *          언리얼과 유니티에는 자동 저장 정책이 없어서 게임마다 만든다. 이 클래스는 그 정책을 장르와 무관하게 줄인 것이다.
     *          - 요청(`requestSave`, `notifyAreaChanged`, `reachCheckpoint`, `notifyBeforeBoss`)은 쌓였다가 `update` 에서 하나로 합쳐 저장한다.
     *            한 프레임에 지역 이동과 체크포인트가 겹쳐도 저장은 한 번이며, 더 중요한 까닭(체크포인트, 보스, 지역, 간격 순)이 슬롯 기록에 남는다.
     *          - 막기(`setBlocked`)가 있으면(전투, 연출, 대화 중) 요청은 기다렸다가 풀린 뒤 저장한다. 종료(`notifyQuit`)만 막기를 무시하고 기다리지 않는다.
     *          - 순환 슬롯: 새 저장은 가장 새 슬롯의 **다음** 슬롯에 쓴다. 슬롯 쓰기가 원자적이고 가장 새 슬롯은 건드리지 않으므로, 저장 도중 꺼져도 마지막 좋은 저장이 남는다.
     *            슬롯 기록(`.info`)은 상태 슬롯 바로 뒤에 맡긴다(저장소가 맡은 순서로 쓴다 - 끝낼 때 맡긴 종료 저장도 기록까지 남는다).
     *            상태 쓰기가 실패하면 그 슬롯의 기록은 옛 내용(원자 쓰기로 남은 옛 상태)을 새 순번으로 가리킬 수 있다 - 읽기 실패와 같게 다음 저장이 덮는다.
     *          - 쓰기를 맡긴 저장 하나가 끝날 때까지(완료) 다음 저장은 기다린다. 저장 수 · 마지막 슬롯은 완료에서 바뀐다.
     *          - 되돌리기: `restoreLatest` 는 가장 새 슬롯을, `restoreCheckpoint` 는 가장 새 체크포인트 슬롯을 읽기를 맡기고, 끝나면 불러오기 델리게이트에 바이트를 넘긴다.
     *          시작할 때(`initialize`) 슬롯 기록을 읽기를 맡겨 순번과 다음 슬롯을 잇는다. 다 읽을 때까지(`isScanning`) 저장 요청은 기다리고 되돌리기는 false 다.
     */
    class SW_GF_API AutosaveManager
    {
    public:
        /** @brief 지금 상태를 바이트로 만듭니다. 성공하면 true 입니다. */
        using SaveDelegate = Delegate<bool( vector<uint8>& outBytes )>;
        /** @brief 바이트의 상태로 되살립니다. 성공하면 true 입니다. */
        using LoadDelegate = Delegate<bool( const vector<uint8>& bytes )>;

        AutosaveManager();

        /**
         * @brief 정책, 로컬 저장소, 델리게이트를 두고 슬롯 기록 읽기를 맡겨 순번을 잇습니다. 타이머, 요청, 막기는 비웁니다.
         * @param store 빌려 쓴다(이 관리자보다 오래 산다 - 보통 게임 인스턴스의 `GameLocalStore`). 맡긴 요청의 구독은 이 관리자가 들어, 먼저 사라지면 콜백이 불리지 않는다.
         * @return 슬롯 수가 1..64 밖이거나 간격 · 최소 간격이 음수이거나 그룹 · 접두사가 슬롯 이름 규칙을 어기면 오류를 남기고 false 입니다(가까운 값으로 맞추지 않는다).
         *         그러면 상태는 그대로입니다. XML 읽기(`loadFromNode`)는 같은 범위를 읽을 때 이미 거절한다.
         */
        [[nodiscard]] bool initialize( const AutosaveSettings& settings, GameLocalStore& store, const SaveDelegate& onSave, const LoadDelegate& onLoad );
        /** @brief 플레이 시간을 넘기고(간격 저장), 쌓인 요청을 막기가 없을 때 하나로 저장합니다. 게임 업데이트에서 부릅니다. */
        void update( float32 deltaTime );

        /** @brief 자동 저장을 요청합니다. 그 까닭이 정책에서 꺼져 있으면 무시합니다. 다음 `update` 가 저장합니다. */
        void requestSave( AutosaveTrigger trigger, const hashed_string& label = hashed_string{} );
        void notifyAreaChanged( const hashed_string& areaName ) { requestSave( AutosaveTrigger::AreaChanged, areaName ); }
        void notifyBeforeBoss( const hashed_string& bossName ) { requestSave( AutosaveTrigger::BeforeBoss, bossName ); }
        /** @brief 체크포인트에 닿았습니다. 같은 체크포인트에 다시 닿으면 저장하지 않습니다. */
        void reachCheckpoint( const hashed_string& checkpointID );
        /** @brief 게임을 끕니다. 정책이 켜 두었으면 막기와 최소 간격에 상관없이 지금 저장을 맡기며, 맡겼으면 true 입니다(끝은 저장소의 종료가 기다린다). */
        bool notifyQuit();
        /** @brief 지금 저장을 맡깁니다. 최소 간격과 정책은 무시하고 막기는 지킵니다. 맡겼으면 true 입니다. */
        [[nodiscard]] bool saveNow( AutosaveTrigger trigger, const hashed_string& label = hashed_string{} );

        /** @brief 막기 하나를 둡니다. 같은 이름은 한 번만 두며, 막기가 하나라도 있으면 요청은 기다립니다. */
        void setBlocked( const hashed_string& reason );
        void clearBlocked( const hashed_string& reason );
        bool isBlocked() const { return _listBlockReason.empty() == false; }

        /** @brief 가장 새 슬롯 읽기를 맡깁니다. 슬롯이 없거나 기록을 읽는 중이면 false 입니다. 읽은 바이트는 불러오기 델리게이트가 받고, 실패는 알립니다. */
        [[nodiscard]] bool restoreLatest();
        /** @brief 가장 새 체크포인트 슬롯 읽기를 맡깁니다(없으면 false). 되살리면 그 체크포인트에 다시 서도 저장하지 않습니다. */
        [[nodiscard]] bool restoreCheckpoint();

        /** @brief 슬롯 기록을 순번이 큰 것(새것)부터 담습니다. */
        void collectSlots( vector<AutosaveSlotInfo>& outListSlot ) const;
        /** @brief 가장 새 슬롯을 반환합니다. @p trigger 를 주면 그 까닭의 슬롯 중에서 고르며, 없으면 nullptr 입니다. */
        const AutosaveSlotInfo* findLatestSlot() const;
        const AutosaveSlotInfo* findLatestSlot( AutosaveTrigger trigger ) const;

        bool hasPendingRequest() const { return _bPending == SW_TRUE; }
        /** @brief 시작 때 맡긴 슬롯 기록 읽기가 아직 남았으면 true 입니다. */
        bool isScanning() const { return _scanPendingCount > 0; }
        /** @brief 맡긴 저장(상태 슬롯 쓰기)이 아직 끝나지 않았으면 true 입니다. */
        bool                    isSaving() const { return _saveRequest.isActive(); }
        AutosaveTrigger         getPendingTrigger() const { return _pendingTrigger; }
        const hashed_string&    getLastCheckpointID() const { return _lastCheckpointID; }
        float64                 getPlayTime() const { return _playTime; }
        uint32                  getSaveCount() const { return _saveCount; }
        const AutosaveSettings& getSettings() const { return _settings; }

    private:
        bool isTriggerEnabled( AutosaveTrigger trigger ) const;
        /** @brief 상태를 만들어 다음 슬롯에 쓰기를 맡기고, 바로 뒤에 슬롯 기록 쓰기를 맡깁니다. */
        bool performSave( AutosaveTrigger trigger, const hashed_string& label );
        /** @brief 슬롯 기록 읽기를 모두 맡깁니다. */
        void scanSlots();
        /** @brief @p pSlot 의 상태 슬롯 읽기를 맡깁니다. */
        [[nodiscard]] bool        submitRestore( const AutosaveSlotInfo* pSlot, bool bCheckpoint );
        void                      onSlotWritten( const LocalStoreCompletion& completion );
        void                      onInfoWritten( const LocalStoreCompletion& completion );
        void                      onInfoRead( const LocalStoreCompletion& completion );
        void                      onSlotRead( const LocalStoreCompletion& completion );
        [[nodiscard]] static bool writeSlotInfo( const AutosaveSlotInfo& info, vector<uint8>& outBytes );
        [[nodiscard]] static bool readSlotInfo( const vector<uint8>& bytes, AutosaveSlotInfo& outInfo );
        static int32              getTriggerPriority( AutosaveTrigger trigger );

        AutosaveSettings         _settings;
        SaveDelegate             _onSave;
        LoadDelegate             _onLoad;
        GameLocalStore*          _pStore;   ///< 빌려 쓴다(`initialize`)
        vector<AutosaveSlotInfo> _listSlot; ///< 슬롯 번호마다 하나다. 기록이 없으면 `_sequence` 가 0 이다
        vector<hashed_string>    _listBlockReason;
        AutosaveSlotInfo         _savingSlot; ///< 쓰기를 맡긴 저장의 기록(완료에서 `_listSlot` 에 오른다)
        hashed_string            _pendingLabel;
        hashed_string            _lastCheckpointID;
        hashed_string            _restoringCheckpointID; ///< 읽기를 맡긴 체크포인트 되돌리기(비면 가장 새 슬롯 되돌리기)
        float64                  _playTime;
        float64                  _lastSaveTime; ///< 마지막 자동 저장의 플레이 시간(음수면 아직 없음)
        float32                  _intervalTimer;
        uint64                   _nextSequence;
        uint32                   _saveCount;
        int32                    _scanPendingCount; ///< 아직 끝나지 않은 슬롯 기록 읽기 수
        AutosaveTrigger          _pendingTrigger;
        uint8                    _bPending;
        uint8                    _bSaving;         ///< 상태 델리게이트 안이다. 그 안에서 부른 요청은 쌓기만 한다
        vector<Subscription>     _listScanRequest; ///< 슬롯 기록 읽기 구독
        Subscription             _saveRequest;     ///< 상태 슬롯 쓰기 구독(끝나면 꺼진다)
        Subscription             _infoRequest;     ///< 슬롯 기록 쓰기 구독
        Subscription             _restoreRequest;  ///< 되돌리기 읽기 구독
    };
} // namespace sw
