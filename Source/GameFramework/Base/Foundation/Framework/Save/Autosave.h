/**
 * @file Autosave.h
 * @brief 자동 저장 정책 — 언제(간격 · 지역 이동 · 체크포인트 · 보스 앞 · 종료) 어느 칸에(돌림 칸) 저장하고, 체크포인트로 되돌리는지입니다. UI 는 없습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 자동 저장을 부른 까닭입니다. 칸 정보에 함께 적힙니다(목록 UI · 체크포인트 되돌리기가 고른다). */
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

    SW_GF_API const utf8* toString( AutosaveTrigger trigger );
} // namespace sw

namespace sw
{
    /**
     * @struct AutosaveSettings
     * @brief 자동 저장 정책의 수치입니다. `<Autosave interval="300" minimumGap="30" slots="3" directory="saves" prefix="autosave_"
     *        areaChange="true" checkpoint="true" beforeBoss="true" quit="true"/>` 를 읽습니다(빠진 속성은 기본값).
     */
    struct SW_GF_API AutosaveSettings
    {
        string  _directory{ "saves" };     ///< 칸 파일을 둘 폴더(저장 경로 기준)
        string  _prefix{ "autosave_" };    ///< 칸 파일 이름 앞부분 — `<폴더>/<앞부분><칸>.sav`
        float32 _interval{ 300.0f };       ///< 간격 자동 저장(초, 플레이 시간). 0 이면 끈다
        float32 _minimumGap{ 30.0f };      ///< 두 자동 저장 사이의 최소 간격(초). 종료 · 직접 부른 것은 이 간격을 무시한다
        int32   _slotCount{ 3 };           ///< 돌려 쓰는 칸 수(1 이상) — 새 저장은 가장 새 칸을 건드리지 않는다
        uint8   _bOnAreaChange{ SW_TRUE }; ///< 지역 이동에 저장한다
        uint8   _bOnCheckpoint{ SW_TRUE }; ///< 체크포인트에 저장한다
        uint8   _bOnBeforeBoss{ SW_TRUE }; ///< 보스 앞에 저장한다
        uint8   _bOnQuit{ SW_TRUE };       ///< 종료에 저장한다

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );
        /** @brief 칸 번호의 저장 파일 경로입니다. */
        string makeSlotPath( int32 slot ) const;

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    /** @brief 칸 하나의 기록입니다(칸 파일 옆 `.info` — 저장이 끝난 뒤 원자적으로 쓴다). */
    struct AutosaveSlotInfo
    {
        string          _path{};
        hashed_string   _label{};       ///< 지역 · 체크포인트 · 보스 이름
        uint64          _sequence{ 0 }; ///< 저장 순번 — 클수록 새것
        float64         _playTime{ 0.0 };
        int32           _slot{ -1 };
        AutosaveTrigger _trigger{ AutosaveTrigger::Manual };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutosaveManager
     * @brief 자동 저장의 언제 · 어디 · 되돌리기를 맡습니다. 저장 · 불러오기 자체는 게임이 넘긴 델리게이트(보통 `GameInstanceBase::saveStateToFile` ·
     *        `loadStateFromFile`)가 합니다.
     * @details 상용 게임의 자동 저장(언리얼 · 유니티에는 정책이 없고 게임마다 짓는다)을 장르 무관하게 줄인 것입니다:
     *          - 요청(`requestSave` · `notifyAreaChanged` · `reachCheckpoint` · `notifyBeforeBoss`)은 쌓였다가 `update` 에서 하나로 합쳐 저장한다 — 한 프레임에
     *            지역 이동과 체크포인트가 겹쳐도 한 번이고, 더 중요한 까닭(체크포인트 > 보스 > 지역 > 간격)이 칸 기록에 남는다.
     *          - 막기(`setBlocked` — 전투 · 연출 · 대화 중)가 있으면 요청은 기다렸다가 풀린 뒤 저장한다. 종료(`notifyQuit`)만 막기를 무시하지 않고 기다리지 않는다.
     *          - 돌림 칸: 새 저장은 가장 새 칸의 **다음** 칸에 쓴다. 파일 쓰기가 원자적(임시 파일 + 바꿔 끼우기, `FileUtil::writeFile`)이고 가장 새 칸은
     *            건드리지 않으므로, 저장 도중 꺼져도 마지막 좋은 저장이 남는다. 칸 기록(`.info`)은 저장이 성공한 뒤에 쓴다.
     *          - 되돌리기: `restoreLatest`(가장 새 칸) · `restoreCheckpoint`(가장 새 체크포인트 칸 — 보스에게 지면 보스 앞으로).
     *          시작할 때 폴더의 칸 기록을 읽어 순번 · 다음 칸을 잇는다(다른 실행의 저장).
     */
    class SW_GF_API AutosaveManager
    {
    public:
        /** @brief 경로에 지금 상태를 씁니다. 성공하면 true 입니다. */
        using SaveDelegate = Delegate<bool( string_view path )>;
        /** @brief 경로의 상태를 읽어 되살립니다. 성공하면 true 입니다. */
        using LoadDelegate = Delegate<bool( string_view path )>;

        AutosaveManager();

        /** @brief 정책 · 델리게이트를 두고 폴더의 칸 기록을 읽어 순번을 잇습니다. 타이머 · 요청 · 막기는 비웁니다. */
        void initialize( const AutosaveSettings& settings, const SaveDelegate& onSave, const LoadDelegate& onLoad );
        /** @brief 플레이 시간을 넘기고(간격 저장), 쌓인 요청을 막기가 없을 때 하나로 저장합니다. 게임 업데이트에서 부릅니다. */
        void update( float32 deltaTime );

        /** @brief 자동 저장을 요청합니다. 그 까닭이 정책에서 꺼져 있으면 무시합니다. 다음 `update` 가 저장합니다. */
        void requestSave( AutosaveTrigger trigger, const hashed_string& label = hashed_string{} );
        void notifyAreaChanged( const hashed_string& areaName ) { requestSave( AutosaveTrigger::AreaChanged, areaName ); }
        void notifyBeforeBoss( const hashed_string& bossName ) { requestSave( AutosaveTrigger::BeforeBoss, bossName ); }
        /** @brief 체크포인트에 닿았습니다. 같은 체크포인트에 다시 닿으면 저장하지 않습니다. */
        void reachCheckpoint( const hashed_string& checkpointID );
        /** @brief 게임을 끕니다 — 정책이 켜 두었으면 막기 · 최소 간격과 상관없이 지금 저장합니다. 저장했으면 true 입니다. */
        bool notifyQuit();
        /** @brief 지금 저장합니다(최소 간격 · 정책 무시, 막기는 지킨다). 저장했으면 true 입니다. */
        [[nodiscard]] bool saveNow( AutosaveTrigger trigger, const hashed_string& label = hashed_string{} );

        /** @brief 막기 하나를 둡니다(같은 이름은 한 번). 막기가 하나라도 있으면 요청은 기다린다. */
        void setBlocked( const hashed_string& reason );
        void clearBlocked( const hashed_string& reason );
        bool isBlocked() const { return _listBlockReason.empty() == false; }

        /** @brief 가장 새 칸을 불러옵니다. 칸이 없거나 읽지 못하면 false 입니다. */
        [[nodiscard]] bool restoreLatest();
        /** @brief 가장 새 체크포인트 칸을 불러옵니다(없으면 false). */
        [[nodiscard]] bool restoreCheckpoint();

        /** @brief 칸 기록을 순번이 큰 것(새것)부터 담습니다. */
        void collectSlots( vector<AutosaveSlotInfo>& outListSlot ) const;
        /** @brief 가장 새 칸입니다. @p trigger 를 주면 그 까닭의 것 중에서입니다. 없으면 nullptr 입니다. */
        const AutosaveSlotInfo* findLatestSlot() const;
        const AutosaveSlotInfo* findLatestSlot( AutosaveTrigger trigger ) const;

        bool                    hasPendingRequest() const { return _bPending == SW_TRUE; }
        AutosaveTrigger         getPendingTrigger() const { return _pendingTrigger; }
        const hashed_string&    getLastCheckpointID() const { return _lastCheckpointID; }
        float64                 getPlayTime() const { return _playTime; }
        uint32                  getSaveCount() const { return _saveCount; }
        const AutosaveSettings& getSettings() const { return _settings; }

    private:
        bool isTriggerEnabled( AutosaveTrigger trigger ) const;
        /** @brief 다음 칸에 저장하고 성공하면 칸 기록을 씁니다. */
        bool performSave( AutosaveTrigger trigger, const hashed_string& label );
        /** @brief 폴더의 칸 기록을 다시 읽습니다. */
        void                      scanSlots();
        [[nodiscard]] static bool writeSlotInfo( const AutosaveSlotInfo& info );
        [[nodiscard]] static bool readSlotInfo( string_view infoPath, AutosaveSlotInfo& outInfo );
        static int32              getTriggerPriority( AutosaveTrigger trigger );

        AutosaveSettings         _settings;
        SaveDelegate             _onSave;
        LoadDelegate             _onLoad;
        vector<AutosaveSlotInfo> _listSlot; ///< 칸마다(칸 번호 자리) — 기록이 없으면 `_sequence` 0
        vector<hashed_string>    _listBlockReason;
        hashed_string            _pendingLabel;
        hashed_string            _lastCheckpointID;
        float64                  _playTime;
        float64                  _lastSaveTime; ///< 마지막 자동 저장의 플레이 시간(음수면 아직 없음)
        float32                  _intervalTimer;
        uint64                   _nextSequence;
        uint32                   _saveCount;
        AutosaveTrigger          _pendingTrigger;
        uint8                    _bPending;
        uint8                    _bSaving; ///< 저장 델리게이트 안이다 — 그 안에서 부른 요청은 쌓기만 한다
    };
} // namespace sw
