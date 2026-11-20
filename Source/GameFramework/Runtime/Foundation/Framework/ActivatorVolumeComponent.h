/**
 * @file ActivatorVolumeComponent.h
 * @brief 활성자가 들어오면 한 번 일을 하는 볼륨의 바탕입니다(튜토리얼 힌트 · 자동 저장). 트리거 콜라이더의 겹침과, 물리 몸이 없는 활성자를 위한 거리 판정을 한 벌로 둡니다.
 */
#pragma once
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

    /**
     * @brief 이미 안에 있는 채로 나타난 활성자(스폰 · 로그인 · 부활 · 불러오기, 볼륨이 스트리밍으로 올라옴 · 다시 켜짐)를 어떻게 셀지입니다.
     * @details 트리거 계약은 "안에서 생성 = 진입" 이다(언리얼 `UpdateOverlaps` 가 등록 때 시작을 낸다). 그 진입을 받지 않으려는 볼륨(도착 컷씬을 부활 자리에서 다시 틀지 않기)은
     *          `Ignore` 를 고른다 — 언리얼은 `GetGameTimeSinceCreation` 을 보는 블루프린트, 유니티는 스폰 프레임을 거르는 스크립트로 게임마다 적는 일을 데이터 칸으로 둔다.
     */
    ENUM()
    enum class ActivatorSpawnInside : uint8
    {
        Activate = 0, ///< 들어온 것으로 친다(트리거 계약 그대로)
        Ignore        ///< 세지 않는다 — 나갔다 다시 들어올 때 들어온 것이다
    };

    /**
     * @class ActivatorVolumeComponent
     * @brief 활성자가 들어오는 길은 둘입니다. 같은 오브젝트의 트리거 콜라이더에 겹치거나(`onOverlapBegin`), `_activator` 가 `_radius` 안에 들어옵니다.
     * @details 거리 판정을 함께 두는 까닭: 말에 탄 기수(말은 물리 몸이 없다)나 격자로 걷는 농부처럼 물리 몸이 없는 활성자는 겹침을 내지 못한다. 언리얼 `ATriggerSphere` 와 유니티 `OnTriggerEnter` 는 몸을 요구하고
     *          그런 게임은 스크립트로 거리를 재는데(기억에 의존한 비교), 볼륨마다 되풀이되어 바탕에 둔다. `_radius` 가 0 이면 거리는 재지 않는다.
     *          거리는 틱(워커)에서 읽고, 일은 틱 뒤 게임 스레드에서 한다 — UI · 저장은 틱 안에서 부르지 않는다. 안에 있는 동안 다시 하지 않는다(들어오는 순간만).
     *          거리 판정도 트리거 계약(DesignBacklog "트리거 Enter · Stay · Exit 계약")을 따른다: 안에서 생겨난 활성자는 첫 틱에 들어오고, 사라지거나 꺼진 활성자 · 꺼진 볼륨은
     *          안의 활성자를 내보낸다(다시 켜면 안에 있는 활성자가 들어오는 순간이 된다). 거리로 재는 활성자의 몸 겹침은 받지 않는다 — 한 활성자는 한 길로만 들어온다.
     *          발동 수(`_fireCount`)는 상태 칸이고 이 계층에 `SaveGame` 칸이 없어 세이브가 모든 칸을 싣는다 — 한 번만 하는 볼륨은 불러온 뒤에도 다시 하지 않는다.
     *          진행 플래그(`_progressFlag`)를 적으면 발동 기록은 판 상태(`SessionStateComponent` — 이 층에서는 `GameFlagsOwnerComponent` 로 찾는다)의 플래그다 — 세이브 슬롯에 실리고, 같은 플래그를 적은 볼륨끼리
     *          나누며(마을 입구 둘에 도착 컷씬 하나), 대화 · 퀘스트 조건식이 같은 이름으로 읽는다. 언리얼 `USaveGame` 의 진행 bool, 유니티 저장 데이터의 "봤음" 칸 자리다.
     *          플래그를 적었는데 열린 판 상태가 없으면 오류를 남기고 일하지 않는다(볼륨의 발동 수로 대신하지 않는다).
     */
    REFLECT( Abstract, Category = "Gameplay", DisplayName = "Activator Volume", Tooltip = "Base of the volumes that do something when an activator enters (trigger overlap or distance)" )
    class SW_GF_API ActivatorVolumeComponent : public Component
    {
    public:
        REFLECT_BODY();

        ActivatorVolumeComponent();
        ~ActivatorVolumeComponent() override = default;

        void onTick( TickContext& context ) override;
        void onOverlapBegin( const OverlapInfo& overlap ) override;
        void onOwnerActiveInHierarchyChanged() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 활성자가 들어온 것으로 칩니다(겹침 · 거리 판정 · 테스트). 한 번만 하는 볼륨이 이미 했거나 태그가 맞지 않으면 false 이고, 일을 했으면 true 입니다. */
        bool activate( const GameObject* pActivator );
        /** @brief 이 볼륨이 일을 했거나 진행 플래그가 서 있으면 true 입니다(게임 스레드 — 판 상태를 읽는다). */
        bool hasFired() const;
        /** @brief 거리 판정의 활성자와 반지름을 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void setProximity( GameObjectHandle activator, float32 radius );
        /** @brief 처음 한 번만 할지(참) 들어올 때마다 할지(거짓)를 정합니다. 코드로 만든 볼륨 · 테스트가 쓴다. 씬에 놓은 볼륨은 PROPERTY 가 채운다. */
        void setOnce( bool bOnce ) { _bOnce = bOnce; }
        /** @brief 발동을 적을 판 상태 플래그를 정합니다(빈 이름: 이 볼륨의 발동 수). 코드로 만든 볼륨 · 테스트가 쓴다. 씬에 놓은 볼륨은 PROPERTY 가 채운다. */
        void setProgressFlag( const hashed_string& progressFlag ) { _progressFlag = progressFlag; }
        /** @brief 이미 안에 있는 채로 나타난 활성자를 셀지 정합니다. 코드로 만든 볼륨 · 테스트가 쓴다. 씬에 놓은 볼륨은 PROPERTY 가 채운다. */
        void setSpawnInside( ActivatorSpawnInside spawnInside ) { _spawnInside = spawnInside; }
        void onBeginPlay() override;

    protected:
        /** @brief 활성자가 들어왔을 때의 일입니다. 일을 했으면 true 입니다(false 면 한 번도 안 한 것으로 남아 다음에 다시 시도한다). 게임 스레드입니다. */
        [[nodiscard]] virtual bool onVolumeActivated( const GameObject& activator ) = 0;
        /** @brief 태그를 정하고 발동 기록을 지웁니다. 파생의 `configure` 가 부릅니다. */
        void resetActivation( TagID activatorTag );

    private:
        /** @brief 나타남 판정의 프레임 창입니다. 안에서 생긴 겹침의 진입은 다음 물리 스텝에 오므로 그 사이를 덮는다. */
        static constexpr uint64 kSpawnWindowFrames = 8;

        /**
         * @brief 진입(겹침 · 거리)이 나타남(@p activator 가 막 생겼거나, 이 볼륨이 막 생겼거나 켜짐)에서 왔으면 true 입니다. 이미 있던 활성자를 끄고 켠 것은 나타남이 아니다(걸어 들어온 것과 같다).
         * @details 오브젝트 id 는 만든 차례로 오른다 — 창 전에 적어 둔 다음 id 보다 크거나 같으면 그 뒤에 생긴 오브젝트다. 불러오기는 저장된 id 를 되살리지만 볼륨도 함께 다시 서므로 볼륨 쪽 창이 덮는다.
         *          shortcut: 물리 스텝이 창(`kSpawnWindowFrames`)보다 늦게 오면(아주 높은 프레임률 · 낮은 물리 주기) 걸어 들어온 것으로 센다 — 그런 구성이 생기면 물리가 겹침에 "막 더한 바디" 표시를 싣는다.
         */
        bool isSpawnEntry( const GameObject& activator ) const;
        /** @brief 진행 플래그를 담는 판 상태입니다. 플래그가 없으면 nullptr 이고, 플래그가 있는데 열린 판 상태가 없으면 오류를 남기고 nullptr 입니다. */
        GameFlags* findProgressFlags() const;
        /** @brief 볼륨이 나타났다(시작 · 다시 켜짐) — 나타남 창을 다시 연다. */
        void markAppeared();

        PROPERTY( Category = "Volume", DisplayName = "Activator Tag", Tooltip = "Only objects with this tag activate it (empty: any object)" )
        TagID _activatorTag;
        PROPERTY( Category = "Volume", DisplayName = "Activator", Tooltip = "Object whose distance is measured (for activators without a physics body); None: only trigger overlaps" )
        GameObjectHandle _activator;
        PROPERTY( Category = "Volume", DisplayName = "Radius", Min = 0.0, Tooltip = "Activator inside this distance counts as entering (0: distance is not measured)", Units = m )
        float32 _radius;
        PROPERTY( RuntimeState, Category = "Volume", DisplayName = "Fire Count", Tooltip = "Runtime" )
        int32 _fireCount;
        PROPERTY( Category = "Volume", DisplayName = "Progress Flag", Tooltip = "Session flag that records the activation (saved with the session, shared by volumes with the same flag, read by conditions); empty: this volume's fire count" )
        hashed_string _progressFlag;
        uint64        _appearedFrame;          ///< 볼륨이 나타난(시작 · 다시 켜짐) 씬 프레임
        uint64        _spawnWatermarkFrame;    ///< `_spawnWatermarkCurrent` 를 적은 씬 프레임
        uint64        _spawnWatermarkPrevious; ///< 창 하나 전에 적은 다음 오브젝트 id — 이보다 크거나 같은 id 는 그 뒤에 생겼다
        uint64        _spawnWatermarkCurrent;  ///< 마지막에 적은 다음 오브젝트 id
        PROPERTY( Category = "Volume", DisplayName = "Once", Tooltip = "Fire only the first time" )
        bool _bOnce;
        PROPERTY( Category = "Volume", DisplayName = "Spawn Inside", Tooltip = "An activator that appears already inside (spawn, login, respawn, load, the volume streaming in or turning on): Activate counts it as entering, Ignore waits until it leaves and comes back" )
        ActivatorSpawnInside _spawnInside;
        bool                 _bActivatorInside; ///< 지난 틱에 활성자가 반지름 안에 있었다(들어오는 순간만 일을 한다)
    };
} // namespace sw
