/**
 * @file TriggerComponent.h
 * @brief 활성자가 들어오면 한 번 일을 하는 볼륨의 바탕입니다(튜토리얼 힌트 · 자동 저장). 트리거 콜라이더의 겹침과, 물리 몸이 없는 활성자를 위한 거리 판정을 한 벌로 둡니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/TagID.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFlags;
    class GameObjectManager;

    /**
     * @brief 볼륨의 진입을 어느 월드가 판정하는지입니다. 파생이 생성자 인자로 반드시 고른다(기본값이 없다 — 새 볼륨이 "월드마다 판정" 을 모르고 물려받지 않게).
     * @details 언리얼은 트리거 오버랩이 서버 · 클라이언트 양쪽에서 나고 게임 로직이 `HasAuthority()` 뒤에서 서버만 돈다 — 그 갈림을 볼륨 형식마다의 값으로 둔다.
     *          넷 링크가 없는 월드(혼자 하는 게임)는 어느 쪽이든 판정한다.
     */
    enum class TriggerAuthority : uint8
    {
        Server = 0, ///< 서버(와 혼자 하는 게임)만 판정한다 — 게임 상태를 바꾸는 일(컷씬 시작 · 저장 · 진행 플래그). 클라이언트 월드는 진입을 받아도 일하지 않는다
        Local       ///< 월드마다 판정한다 — 그 월드의 로컬 플레이어 화면 일(힌트) · 월드마다 같이 도는 보이는 일 · 이동 시뮬레이션(지역 표면 겉모습 · 물리 필드). 원격 플레이어의 진입을 거르는 것은 파생의 몫이다
    };

    /**
     * @struct TriggerAuthorityRule
     * @brief 판정 자리 규칙 한 벌입니다 — 활성자 볼륨(`TriggerComponent`)과 지역 모듈(`IZoneModuleProcessor::getAuthority`)이 같이 쓴다(언리얼 `HasAuthority()` 자리).
     */
    struct SW_GF_API TriggerAuthorityRule
    {
        /** @brief 월드 @p manager 가 @p authority 의 일을 판정하면 true 입니다. Server 는 넷 링크가 클라이언트인 월드에서 거짓이고, Local 과 넷 링크가 없는 월드(혼자 하는 게임)는 늘 참이다. */
        [[nodiscard]] static bool hasAuthority( TriggerAuthority authority, const GameObjectManager& manager );
    };

    /**
     * @brief 이미 안에 있는 채로 나타난 활성자(스폰 · 로그인 · 부활 · 불러오기, 볼륨이 스트리밍으로 올라옴 · 다시 켜짐)를 어떻게 셀지입니다.
     * @details 트리거 계약은 "안에서 생성 = 진입" 이다(언리얼 `UpdateOverlaps` 가 등록 때 시작을 낸다). 그 진입을 받지 않으려는 볼륨(도착 컷씬을 부활 자리에서 다시 틀지 않기)은
     *          `Ignore` 를 고른다 — 언리얼은 `GetGameTimeSinceCreation` 을 보는 블루프린트, 유니티는 스폰 프레임을 거르는 스크립트로 게임마다 적는 일을 데이터 칸으로 둔다.
     */
    ENUM()
    enum class TriggerSpawnInside : uint8
    {
        Activate = 0, ///< 들어온 것으로 친다(트리거 계약 그대로)
        Ignore        ///< 세지 않는다 — 나갔다 다시 들어올 때 들어온 것이다
    };

    /**
     * @class TriggerSpawnWindow
     * @brief "안에서 나타남" 판정 한 벌입니다 — 진입이 나타남(활성자가 막 생겼거나 볼륨이 막 생겼거나 켜짐)에서 왔는지 가립니다. 활성자 볼륨과 지역 볼륨(`ZoneVolumeComponent`)이 같이 쓴다.
     * @details 볼륨 쪽 나타남은 진입 길마다 첫 판정 한 번이다 — 물리 겹침 사건은 볼륨이 나타난 뒤 첫 물리 스텝(그때 안에 있던 몸의 겹침 시작이 그 스텝에 온다), 프레임 표본(거리 · 지역)은 첫 표본 프레임.
     *          활성자 쪽 나타남: 겹침 길은 물리가 표시한다(`OverlapInfo::_bOtherSpawnedInside` — 상대 첫 바디의 첫 스텝 진입). 스텝으로 재므로 스텝 없는 프레임(높은 프레임률)이 몇이 끼어도 맞다.
     *          표본 길은 오브젝트 id 창이다 — id 는 만든 차례로 오르므로 창 전에 적어 둔 다음 id 보다 크거나 같으면 그 뒤에 생긴 오브젝트다. 불러오기는 저장된 id 를 되살리지만 볼륨도 함께 다시 서므로 볼륨 쪽이 덮는다.
     *          프레임 수 창으로 볼륨 쪽을 재면 나타난 직후 몇 프레임 안에 걸어 들어온 활성자까지 나타남으로 걸렀다(허브 입구 — 씬이 선 지 4 프레임에 들어온 플레이어).
     */
    class SW_GF_API TriggerSpawnWindow
    {
    public:
        /**
         * @brief 표본 길의 활성자 쪽 나타남(막 생긴 오브젝트) 프레임 창입니다. 막 생긴 개체가 처음 표본에 드는 데(시작 · 공간 색인 단계 한 번) 걸리는 프레임을 덮는다.
         *        시간이 흐른 프레임만 센다(`GameObjectManager::getRunningUpdateCount`) — 로딩 화면이 길다고 창이 먼저 닫히지 않는다.
         * @details 표본은 프레임마다 재므로 그 늦음은 프레임 수로 정해진다(물리 스텝과 무관). 겹침 길은 이 창을 쓰지 않는다(`isOverlapSpawnEntry`).
         */
        static constexpr uint64 kFrames = 8;

        /** @brief 볼륨이 나타났다(시작 · 다시 켜짐) — 창을 다시 연다. */
        void markAppeared( const GameObjectManager& manager );
        /** @brief 표본 길의 창을 민다 — 나타남을 거르는 볼륨이 프레임마다 부른다. */
        void advance( const GameObjectManager& manager );
        /** @brief 겹침 진입 @p overlap 이 나타남에서 왔으면 true 입니다 — 볼륨이 나타난 뒤 첫 물리 스텝이거나 상대가 안에서 생겼다. 이미 있던 활성자를 끄고 켠 것은 나타남이 아니다. */
        [[nodiscard]] bool isOverlapSpawnEntry( const GameObjectManager& manager, const OverlapInfo& overlap ) const;
        /** @brief 프레임마다 재는 표본(거리 측정 · 지역 표본)의 @p activator 진입이 나타남에서 왔으면 true 입니다 — 볼륨이 나타난 뒤 첫 표본 프레임이거나 활성자가 창 안에 생겼다. */
        [[nodiscard]] bool isSampledSpawnEntry( const GameObjectManager& manager, const GameObject& activator ) const;

    private:
        uint64 _appearedFrame{ 0 };     ///< 볼륨이 나타난(시작 · 다시 켜짐) 씬 프레임
        uint64 _appearedStep{ 0 };      ///< 볼륨이 나타날 때의 물리 스텝 번호(`ScenePhysics::getStepCount`)
        uint64 _watermarkFrame{ 0 };    ///< `_watermarkCurrent` 를 적은 씬 프레임(표본 길)
        uint64 _watermarkPrevious{ 0 }; ///< 창 하나 전에 적은 다음 오브젝트 id — 이보다 크거나 같은 id 는 그 뒤에 생겼다
        uint64 _watermarkCurrent{ 0 };  ///< 마지막에 적은 다음 오브젝트 id
    };

    /**
     * @class TriggerComponent
     * @brief 활성자가 들어오는 길은 둘입니다. 같은 오브젝트의 트리거 콜라이더에 겹치거나(겹침 사건 `GameObjectEvents::_overlapBegin`), `_activator` 가 `_radius` 안에 들어옵니다.
     * @details 거리 판정을 함께 두는 까닭: 말에 탄 기수(말은 물리 몸이 없다)나 격자로 걷는 농부처럼 물리 몸이 없는 활성자는 겹침을 내지 못한다. 언리얼 `ATriggerSphere` 와 유니티 `OnTriggerEnter` 는 몸을 요구하고
     *          그런 게임은 스크립트로 거리를 재는데(기억에 의존한 비교), 볼륨마다 되풀이되어 바탕에 둔다. `_radius` 가 0 이면 거리는 재지 않는다.
     *          거리는 틱(워커)에서 읽고, 일은 틱 뒤 게임 스레드에서 한다 — UI · 저장은 틱 안에서 부르지 않는다. 안에 있는 동안 다시 하지 않는다(들어오는 순간만).
     *          거리 판정도 트리거 계약(docs/agents/Gameplay.md "트리거 · 물리")을 따른다: 안에서 생겨난 활성자는 첫 틱에 들어오고, 사라지거나 꺼진 활성자 · 꺼진 볼륨은
     *          안의 활성자를 내보낸다(다시 켜면 안에 있는 활성자가 들어오는 순간이 된다). 거리로 재는 활성자의 몸 겹침은 받지 않는다 — 한 활성자는 한 길로만 들어온다.
     *          발동 수(`_fireCount`)는 상태 칸이고 이 계층에 `SaveGame` 칸이 없어 세이브가 모든 칸을 싣는다 — 한 번만 하는 볼륨은 불러온 뒤에도 다시 하지 않는다.
     *          진행 플래그(`_progressFlag`)를 적으면 발동 기록은 판 상태(`GameStateComponent` — 이 층에서는 `IGameFlagsOwner` 로 찾는다)의 플래그다 — 세이브 슬롯에 실리고, 같은 플래그를 적은 볼륨끼리
     *          나누며(마을 입구 둘에 도착 컷씬 하나), 대화 · 퀘스트 조건식이 같은 이름으로 읽는다. 언리얼 `USaveGame` 의 진행 bool, 유니티 저장 데이터의 "봤음" 칸 자리다.
     *          플래그를 적었는데 열린 판 상태가 없으면 오류를 남기고 일하지 않는다(볼륨의 발동 수로 대신하지 않는다).
     *          판정 자리(`TriggerAuthority`)는 파생이 생성자 인자로 반드시 고른다 — 서버만(시퀀스 트리거 · 자동 저장) 또는 월드마다(튜토리얼 힌트).
     */
    REFLECT( Abstract, Category = "Gameplay", DisplayName = "Activator Volume", Tooltip = "Base of the volumes that do something when an activator enters (trigger overlap or distance)" )
    class SW_GF_API TriggerComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @param authority 진입을 어느 월드가 판정하는지 — 볼륨 형식의 성질이라 파생이 고르고 배치 데이터로 바꾸지 않는다. */
        explicit TriggerComponent( TriggerAuthority authority );
        ~TriggerComponent() override = default;

        void onTick( TickContext& context ) override;
        /** @brief 겹침 사건을 구독합니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onOwnerActiveInHierarchyChanged() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 활성자가 들어온 것으로 칩니다(겹침 · 거리 판정 · 테스트). 한 번만 하는 볼륨이 이미 했거나 태그가 맞지 않으면 false 이고, 일을 했으면 true 입니다. */
        bool activate( const GameObject* pActivator );
        /** @brief 이 볼륨이 일을 했거나 진행 플래그가 서 있으면 true 입니다(게임 스레드 — 판 상태를 읽는다). 플레이어별 기록은 활성자 없이 묻지 않으므로 발동 수만 본다. */
        bool hasFired() const;
        /** @brief 거리 판정의 활성자와 반지름을 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void setProximity( GameObjectHandle activator, float32 radius );
        /** @brief 처음 한 번만 할지(참) 들어올 때마다 할지(거짓)를 정합니다. 코드로 만든 볼륨 · 테스트가 쓴다. 씬에 놓은 볼륨은 PROPERTY 가 채운다. */
        void setOnce( bool bOnce ) { _bOnce = bOnce; }
        /** @brief 발동을 적을 판 상태 플래그를 정합니다(빈 이름: 이 볼륨의 발동 수). 코드로 만든 볼륨 · 테스트가 쓴다. 씬에 놓은 볼륨은 PROPERTY 가 채운다. */
        void setProgressFlag( const hashed_string& progressFlag ) { _progressFlag = progressFlag; }
        /** @brief 이미 안에 있는 채로 나타난 활성자를 셀지 정합니다. 코드로 만든 볼륨 · 테스트가 쓴다. 씬에 놓은 볼륨은 PROPERTY 가 채운다. */
        void setSpawnInside( TriggerSpawnInside spawnInside ) { _spawnInside = spawnInside; }
        void onBeginPlay() override;

    protected:
        /** @brief 활성자가 들어왔을 때의 일입니다. 일을 했으면 true 입니다(false 면 한 번도 안 한 것으로 남아 다음에 다시 시도한다). 게임 스레드입니다. */
        [[nodiscard]] virtual bool onVolumeActivated( const GameObject& activator ) = 0;
        /** @brief 태그를 정하고 발동 기록을 지웁니다. 파생의 `configure` 가 부릅니다. */
        void resetActivation( TagID activatorTag );
        /**
         * @brief 진행 플래그(`_progressFlag` 가 있을 때만 부른다)를 적을 곳입니다. 바탕은 판 상태(세션)이고, 플레이어별 기록을 고르는 파생이 덮는다.
         * @param pActivator 들어온 활성자입니다. nullptr 이면 활성자 없이 묻는 것(`hasFired`)이다 — 플레이어별 기록은 이때 nullptr 을 돌려준다.
         * @return 적을 곳이 없으면 오류를 남기고 nullptr 입니다(볼륨은 일하지 않는다).
         */
        [[nodiscard]] virtual GameFlags* findProgressFlags( const GameObject* pActivator ) const;
        /**
         * @brief 이 월드가 이 볼륨의 진입을 판정하는지입니다(언리얼 `HasAuthority`). 거짓이면 `activate` 가 일하지 않고 진행 플래그도 찾지 않는다(발동 아님, 로그 없음).
         * @details `Server` 볼륨은 넷 링크가 클라이언트인 월드에서 거짓, `Local` 볼륨은 늘 참이다.
         */
        [[nodiscard]] bool   hasActivationAuthority() const;
        const hashed_string& getProgressFlag() const { return _progressFlag; }
        /** @brief @p object 가 거리로 재는 활성자인지 · 그래서 지금 안에 있는지입니다(거리로 재는 활성자는 이탈 사건이 없다). */
        bool isMeasuredByDistance( GameObjectHandle object ) const { return _radius > 0.0f && _activator == object; }
        bool isInsideByDistance( GameObjectHandle object ) const { return isMeasuredByDistance( object ) && _bActivatorInside; }

    private:
        /** @brief 볼륨이 나타났다(시작 · 다시 켜짐) — 나타남 창을 다시 연다. */
        void markAppeared();
        /** @brief 겹침 진입(`GameObjectEvents::_overlapBegin`)을 활성자의 진입으로 칩니다(상대 트리거 · 거리로 재는 활성자 · 받지 않는 나타남은 거른다). */
        void handleOverlapBegin( const OverlapInfo& overlap );

        Subscription _overlapSubscription; ///< 겹침 진입 구독(`handleOverlapBegin`)

        PROPERTY( Category = "Volume", DisplayName = "Activator Tag", Tooltip = "Only objects with this tag activate it (empty: any object)" )
        TagID _activatorTag;
        PROPERTY( Category = "Volume", DisplayName = "Activator", Tooltip = "Object whose distance is measured (for activators without a physics body); None: only trigger overlaps" )
        GameObjectHandle _activator;
        PROPERTY( Category = "Volume", DisplayName = "Radius", Min = 0.0, Tooltip = "Activator inside this distance counts as entering (0: distance is not measured)", Units = m )
        float32 _radius;
        PROPERTY( RuntimeState, Category = "Volume", DisplayName = "Fire Count", Tooltip = "Runtime" )
        int32 _fireCount;
        PROPERTY( Category = "Volume", DisplayName = "Progress Flag", Tooltip = "Session flag that records the activation (saved with the session, shared by volumes with the same flag, read by conditions); empty: this volume's fire count" )
        hashed_string      _progressFlag;
        TriggerSpawnWindow _spawnWindow; ///< 나타남 판정(`_spawnInside` 가 Ignore 일 때 쓴다)
        TriggerAuthority   _authority;   ///< 판정 자리(파생이 정한다)
        PROPERTY( Category = "Volume", DisplayName = "Once", Tooltip = "Fire only the first time" )
        bool _bOnce;
        PROPERTY( Category = "Volume", DisplayName = "Spawn Inside", Tooltip = "An activator that appears already inside (spawn, login, respawn, load, the volume streaming in or turning on): Activate counts it as entering, Ignore waits until it leaves and comes back" )
        TriggerSpawnInside _spawnInside;
        bool               _bActivatorInside; ///< 지난 틱에 활성자가 반지름 안에 있었다(들어오는 순간만 일을 한다)
    };
} // namespace sw
