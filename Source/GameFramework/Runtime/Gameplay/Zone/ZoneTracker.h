/**
 * @file ZoneTracker.h
 * @brief 지역 효과의 처리 길 하나입니다 — 입력(볼륨 겹침 · 표면 디딤 · 부딪힘 · 잠김)이 이번 프레임에 안에 있는 대상과 위치 값을 알리면, 진입 · 체류 · 이탈을 가려 모듈 처리기에 넘깁니다.
 * @details 볼륨(`ZoneVolumeComponent`)과 표면 접촉(`ZoneContactComponent`)이 각자 하나씩 들지만 처리는 이 클래스 하나다 — 플레이어 · NPC · 탈것 · 물리 바디가 같은 길을 지난다.
 *          대상마다 위치 값(`ZoneSample`)과 모듈마다의 기억(`ZoneModuleMemory` — 주기 · 조건 전환 · 지연)을 든다. 대상 역할(들어온 개체 · 탈것 · 탑승자 · 근처)을 풀어 처리기를 부르고,
 *          처리기가 부르는 동작은 출처(`volume:<이름>` · `surface:<종류>`)를 실어 Ability 디버거 · 동작 추적(ActionTrace)에 남는다. 진입 · 이탈 · 전환은 고정 용량 기록(`collectEventLog`)에도 든다.
 *          게임 스레드에서 틱 뒤에 부른다(물리 자리 읽기 · 동작이 오브젝트를 만들고 지운다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneModule.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /** @brief 지역 기록 한 줄의 사건입니다. */
    enum class ZoneLogEvent : uint8
    {
        Enter = 0,
        Exit
    };

    /** @brief 지역 기록 한 줄입니다(진단 HUD 섹션 `zone`). */
    struct ZoneLogEntry
    {
        hashed_string    _source{};
        GameObjectHandle _target{};
        float32          _timeInside{ 0.0f };
        ZoneLogEvent     _event{ ZoneLogEvent::Enter };
    };

    /**
     * @class ZoneTracker
     * @brief 입력 하나(볼륨 · 표면 종류)의 대상 기록과 사건 분배입니다(파일 머리말 참고).
     */
    class SW_GF_API ZoneTracker
    {
    public:
        /** @brief 근처 역할의 기본 반경(m)입니다. 할 일 모듈은 값 `radius` 로 바꾼다. */
        static constexpr float32 kDefaultNearbyRadius = 3.0f;
        /** @brief 기록 고리의 칸 수입니다. */
        static constexpr uint32 kLogCapacity = 32;

        /** @brief 대상 하나의 기록입니다. */
        struct OccupantRecord
        {
            GameObjectHandle         _target{};
            ZoneSample               _sample{};
            vector<ZoneModuleMemory> _listMemory{}; ///< 모듈마다(프로필 순서)
            uint32                   _touchStamp{ 0 };
            uint8                    _bEntered{ SW_FALSE };
            uint8                    _arrReserved[3]{};
        };

        ZoneTracker();

        /** @brief 이번 프레임의 알림을 시작합니다. 이 뒤 `touch` 하지 않은 대상은 `dispatchOverlapEvents` 에서 나간다. */
        void beginFrame();
        /** @brief 대상 @p target 이 이번 프레임에 안에 있다고 알립니다. 위치 값 @p sample 의 시간은 이 클래스가 센다. */
        void touch( const GameObject& target, const ZoneSample& sample );
        /**
         * @brief 이번 프레임을 마칩니다 — 새 대상은 진입, 남은 대상은 체류(게임 초 @p deltaTime), 알리지 않은 대상은 이탈을 처리기에 넘깁니다.
         * @param source 동작 · 이펙트에 실을 출처입니다.
         */
        void dispatchOverlapEvents( GameObjectManager& manager, const ZoneCompiledProfile& profile, const ActionSource& source, float32 deltaTime );
        /** @brief 남은 대상을 모두 내보냅니다(입력이 끝날 때 — 영역 제거 · 씬 전환). */
        void exitAllOccupants( GameObjectManager& manager, const ZoneCompiledProfile& profile, const ActionSource& source );
        /** @brief 입력이 시작할 때 모듈마다 한 번(대상과 상관없는 일 — 내비 비용)입니다. */
        static void onZoneOpened( GameObjectManager& manager, const ZoneCompiledProfile& profile );

        /** @brief 대상 @p target 의 기록입니다. 안에 없으면 nullptr 입니다. */
        const OccupantRecord*         findOccupancy( GameObjectHandle target ) const;
        const vector<OccupantRecord>& getOccupants() const { return _listOccupancy; }
        /** @brief 최근 진입 · 이탈 기록(오래된 것부터)을 @p outListEntry 에 붙입니다. */
        void collectEventLog( vector<ZoneLogEntry>& outListEntry ) const;

    private:
        /** @brief 사건 종류입니다. */
        enum class Event : uint8
        {
            Enter = 0,
            Stay,
            Exit
        };

        /** @brief 기록 하나의 모든 모듈에 사건 @p event 를 넘깁니다(대상 역할을 풀어 대상마다). */
        void dispatch( GameObjectManager& manager, const ZoneCompiledProfile& profile, const ActionSource& source, OccupantRecord& occupancy, Event event, float32 deltaTime );
        /** @brief 모듈 @p module 의 대상 역할을 @p occupant 기준으로 풉니다. */
        static void resolveRoleTargets( GameObjectManager& manager, const ZoneCompiledModule& module, GameObject& occupant, vector<GameObject*>& outListTarget );
        void        appendLog( const ActionSource& source, const OccupantRecord& occupancy, ZoneLogEvent event );

    private:
        vector<OccupantRecord> _listOccupancy;
        ZoneLogEntry           _arrLog[kLogCapacity];
        uint32                 _logCount;
        uint32                 _stamp;
    };
} // namespace sw
