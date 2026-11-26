/**
 * @file HubWorld.h
 * @brief 허브의 서버 권위 규칙(작은 액션 MMO)입니다 — 허브에서 그림자 짐승을 치고(전투) 빛버섯을 캐며(생활) 경험 · 동전을 얻어 자라고,
 *        파티로 함께 사냥해 경험을 나눕니다. 클라이언트는 이 규칙을 돌리지 않는다 — 결과는 서버가 정해 알린다(`HubNetLink`).
 * @details 엔진 · 망 없이 도는 순수 규칙이라 단위 테스트가 그대로 부른다. 수치는 씬 데이터(`HubRules` — `game/playground/maps/hub.scene.xml`)다.
 *          - 판정: 공격은 사거리 · 재사용 대기 · 대상 생존, 채집은 거리 · 대상 생존을 서버가 본다(클라이언트 값은 요청일 뿐이다).
 *          - 자리: 아바타의 자리는 서버가 움직인 폰의 자리다(`HubPawnReplicationComponent` — 클라이언트는 조종 의도만 보낸다). 규칙은 그 자리가 허브 경계 안이고 걸음 · 탑승의
 *            속도 한도(`_moveSpeed` · `_rideSpeed`)로 갈 수 있는 거리인지 다시 본다 — 순간이동 · 발사처럼 이동기 밖에서 옮긴 자리를 규칙이 받지 않는다(심층 방어).
 *          - 보상: 짐승을 쓰러뜨리면 경험을 나눔 거리 안의 파티원에게 똑같이(올림) 나누고 동전은 마지막으로 친 사람이 갖는다. 경험이 단계표(`_listLevelExperience`)를 넘으면 레벨이 오른다.
 *          - 파티: 초대 → 수락. 초대한 쪽이 파티가 없으면 새로 만들고 이끈다. 이끄는 사람이 떠나면 남은 첫 사람이 이끈다. 한 사람만 남으면 파티가 풀린다.
 *          - 상용 MMO 의 이름 · 내용을 따르지 않는다 — 장르의 접속 → 전투/생활 → 보상 · 성장 → 저장 후 재접속 루프만 보인다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Time/Countdown.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 허브에 놓이는 것의 갈래입니다. */
    ENUM()
    enum class HubPlacementKind : uint8
    {
        Creature = 0, ///< 그림자 짐승 — 쳐서 쓰러뜨린다
        Herb          ///< 빛버섯 — 곁에서 캔다
    };

    /** @brief 행동 하나를 서버가 판정한 결과입니다. 클라이언트에 그대로 알린다(`HubAvatarAgent::receiveActionResult`). */
    ENUM()
    enum class HubActionResult : uint8
    {
        Ok = 0,
        UnknownTarget,  ///< 그런 대상이 없다
        OutOfRange,     ///< 대상이 사거리 · 채집 거리 밖이다
        CoolingDown,    ///< 공격 재사용 대기 중이다
        TargetGone,     ///< 대상이 이미 쓰러졌거나 캐였다(다시 나기를 기다린다)
        StepTooLong,    ///< 속도 한도로 갈 수 있는 거리를 넘었거나 허브 경계 밖이다
        PartyFull,      ///< 파티 인원이 찼다
        AlreadyInParty, ///< 초대받은 쪽이 이미 다른 파티에 있다
        NotInvited,     ///< 그 사람에게 받은 초대가 없다
        NotInParty      ///< 파티에 있지 않다
    };
} // namespace sw

namespace sw
{
    /** @brief 허브에 놓이는 것 하나입니다(씬 데이터). */
    REFLECT()
    struct HubPlacement
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Creature to strike or herb to gather" )
        HubPlacementKind _kind{ HubPlacementKind::Creature };
        PROPERTY()
        float3 _position{};
    };

    /**
     * @brief 허브 규칙의 수치입니다(씬의 `HubWorldComponent` 칸). 범위는 칸의 `PROPERTY( Min · Max )` 이고, 칸 사이의 관계(단계표 오름차순 · 놓을 것 수)는 `HubWorld::initialize` 가 봅니다.
     */
    REFLECT()
    struct HubRules
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.1, Units = m, Tooltip = "Greatest distance at which a strike lands" )
        float32 _attackRange{ 3.0f };
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Time between two strikes of one avatar" )
        float32 _attackCooldownSeconds{ 0.5f };
        PROPERTY( Min = 1, Tooltip = "Health a strike takes" )
        int32 _attackPower{ 10 };
        PROPERTY( Min = 1, Tooltip = "Health of a creature" )
        int32 _creatureHealth{ 30 };
        PROPERTY( Min = 0, Tooltip = "Experience a felled creature gives, shared by the party" )
        int32 _creatureExperience{ 60 };
        PROPERTY( Min = 0, Tooltip = "Coins the last striker takes" )
        int32 _creatureCoins{ 5 };
        PROPERTY( Min = 0.0, Units = s )
        float32 _creatureRespawnSeconds{ 3.0f };
        PROPERTY( Min = 0.1, Units = m, Tooltip = "Greatest distance at which a herb can be gathered" )
        float32 _gatherRange{ 2.0f };
        PROPERTY( Min = 0, Tooltip = "Experience a gathered herb gives" )
        int32 _herbExperience{ 15 };
        PROPERTY( Min = 0.0, Units = s )
        float32 _herbRespawnSeconds{ 5.0f };
        PROPERTY( Min = 0.1, Units = m, Tooltip = "Farthest an avatar may move at once - also the most move budget an avatar can save up" )
        float32 _maxMoveStep{ 12.0f };
        PROPERTY( Required, Min = 0.1, Units = "m/s", Tooltip = "Fastest an avatar may move on foot - each simulated step refills the move budget for its own seconds at this speed and spends the distance moved" )
        float32 _moveSpeed{ 0.0f };
        PROPERTY( Required, Min = 0.1, Units = "m/s", Tooltip = "Fastest an avatar may move while riding (not below the gallop speed of the horses)" )
        float32 _rideSpeed{ 0.0f };
        PROPERTY( Required, Min = 1, Tooltip = "Remote calls one connection may send at once before the server starts refusing them (RemoteCallServerConfig)" )
        int32 _remoteCallBurstCount{ 0 };
        PROPERTY( Required, Min = 1, Units = ms, Tooltip = "Time for one more remote call to be allowed - the steady limit is 1000 / this per second" )
        int32 _remoteCallRefillMilliseconds{ 0 };
        PROPERTY( Min = 1.0, Units = m, Tooltip = "The hub spans -extent..extent on X and Z - pawns and horses are kept inside" )
        float32 _worldHalfExtent{ 64.0f };
        PROPERTY( Min = 2, Max = 8 )
        int32 _partyMaxSize{ 4 };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Party members within this distance of a felled creature share its experience" )
        float32 _partyShareRange{ 30.0f };
        PROPERTY( Min = 1.0, Units = s, Tooltip = "How often the server writes the profiles of everyone online" )
        float32 _autosaveSeconds{ 30.0f };
        /** @brief 새 계정이 서는 시작 자리입니다(차례가 앞선 빈 자리부터 — `HubWorld::chooseAvatarStart`). */
        PROPERTY( Required )
        vector<RespawnSpawnPoint> _listAvatarStart{};
        PROPERTY( Min = 0.0, Units = m, Tooltip = "A start point is taken while another avatar stands within this distance" )
        float32 _startClearance{ 1.0f };
        /** @brief 레벨 n + 1 에 오르는 누적 경험(첫 칸이 레벨 2)입니다. 오름차순이어야 하고, 마지막 칸을 넘으면 최고 레벨입니다. */
        PROPERTY( Required )
        vector<int32> _listLevelExperience{};
        PROPERTY( Required )
        vector<HubPlacement> _listPlacement{};

        /** @brief 점을 허브 경계(XZ) 안으로 자릅니다. 폰의 걸음이 서버 · 예측에서 같은 이 함수로 경계를 지킨다. */
        AbsolutePosition clampInside( const AbsolutePosition& position ) const;
    };

    /**
     * @brief 계정 하나의 영속 진행입니다. 서버가 쓰고 읽는다(`HubProfileStore`) — 클라이언트는 받은 값을 보여 줄 뿐이다.
     * @details 진행 표(`HubProfileStore`)의 레코드 형식이다 — 판은 이 형식의 `REFLECT( Version )`, 모르는 칸은 `_unknownProperties` 가 들고 다시 쓴다(`ServiceRecordTable`).
     */
    REFLECT()
    struct HubProfile
    {
        REFLECT_BODY();

        PROPERTY( SaveGame, Replicated, Min = 1 )
        int32 _level{ 1 };
        PROPERTY( SaveGame, Replicated, Min = 0 )
        int32 _experience{ 0 };
        PROPERTY( SaveGame, Replicated, Min = 0 )
        int32 _coins{ 0 };
        PROPERTY( SaveGame, Replicated, Min = 0 )
        int32 _herbCount{ 0 };
        PROPERTY( SaveGame, Replicated, Min = 0 )
        int32 _defeatCount{ 0 };
        PROPERTY( SaveGame, Replicated, Min = -100000, Max = 100000 )
        float3 _position{}; ///< 범위는 NaN · 무한대를 거절하는 경계다

        ServiceRecordUnknownFields _unknownProperties{};
    };
} // namespace sw

namespace sw
{
    /** @brief 허브에 접속한 아바타 하나입니다. */
    struct HubAvatar
    {
        HubProfile _profile{};
        uint64     _accountID{ 0 };
        uint32     _avatarID{ 0 };
        uint32     _partyID{ 0 };      ///< 0 이면 파티가 없다
        uint32     _inviteFromID{ 0 }; ///< 받은 초대를 보낸 아바타. 0 이면 없다
        Countdown  _attackCooldown{};
        float32    _moveBudget{ 0.0f };   ///< 지금 갈 수 있는 거리(m) — 이동이 그 걸음의 시간 × 속도 한도만큼 채우고(`_maxMoveStep` 까지) 간 거리만큼 쓴다(감사 K-1)
        uint8      _bChanged{ SW_FALSE }; ///< 알릴 진행 · 파티가 바뀌었다(`HubWorld::consumeDirtyFlag`)
    };

    /** @brief 놓인 것 하나의 지금 상태입니다. */
    struct HubPlacementState
    {
        HubPlacement _def{};
        uint32       _placementID{ 0 };
        int32        _health{ 0 };
        Countdown    _respawn{};
        uint8        _bPresent{ SW_TRUE }; ///< 쓰러졌거나 캐였으면 다시 날 때까지 거짓
    };

    /** @brief 파티 하나입니다. 첫 사람이 이끈다. */
    struct HubParty
    {
        vector<uint32> _listMember{};
        uint32         _partyID{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class HubWorld
     * @brief 허브 하나의 서버 상태와 규칙입니다. 게임 스레드에서 씁니다.
     */
    class HubWorld final
    {
    public:
        /** @brief 놓인 것의 id 는 이 수부터 놓인 차례대로입니다. 아바타 id 는 1 부터라 둘은 겹치지 않는다(복제 엔티티 id 로 같이 쓴다). */
        static constexpr uint32 kPlacementIDBase = 100000;
        static constexpr uint32 kMaxPlacement    = 1024;
        static constexpr int32  kMaxLevelStep    = 64;

        HubWorld();

        /** @brief 규칙을 받아 허브를 세웁니다. 단계표가 비었거나 오름차순이 아니거나, 놓을 것이 없거나 너무 많거나 경계 밖이면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool initialize( const HubRules& rules );

        /** @brief 아바타를 들입니다. 진행 @p profile 은 저장소에서 읽은 것(새 계정은 기본값)입니다. 돌려준 id 는 접속이 끝날 때까지 이 아바타를 가리킨다. */
        uint32 addAvatar( uint64 accountID, const HubProfile& profile );
        /** @brief 아바타를 내보냅니다(파티를 떠난다). */
        void removeAvatar( uint32 avatarID );

        HubActionResult attackCreature( uint32 avatarID, uint32 placementID );
        HubActionResult gatherHerb( uint32 avatarID, uint32 placementID );
        /**
         * @brief 서버가 움직인 폰의 자리를 아바타에 옮깁니다. 경계 밖이거나 속도 한도의 예산을 넘으면 StepTooLong 이고 자리는 그대로입니다(부르는 쪽이 폰을 되돌린다).
         * @details 예산은 그 걸음의 시간 @p stepSeconds 가 채운다 — 시계를 따로 재지 않는다. 걸음 수(흐른 시간)는 서버의 입력 몫(`ReplicationServer::accumulateInputBudget`)이 막는다.
         *          예산을 게임 시간으로 채우면 입력 몫(실제 시간)과 흐름이 달라, 서버가 프레임을 놓칠 때마다 몫이 모자라 정직한 걸음을 되돌렸다.
         */
        HubActionResult moveAvatar( uint32 avatarID, const AbsolutePosition& position, float32 stepSeconds, bool bRiding );
        HubActionResult inviteToParty( uint32 avatarID, uint32 targetAvatarID );
        HubActionResult acceptPartyInvite( uint32 avatarID, uint32 leaderAvatarID );
        HubActionResult leaveParty( uint32 avatarID );

        /** @brief 재사용 대기와 다시 나기를 흘립니다. */
        void update( float32 deltaSeconds );

        /** @brief @p placementID 가 그 갈래의 놓인 것인지 봅니다(살아 있는지와 상관없이) — 클라이언트가 지어낸 번호를 거르는 검증이 씁니다. */
        bool isPlacementOfKind( uint32 placementID, HubPlacementKind kind ) const;
        /** @brief 점이 허브 경계 안인지 봅니다. 무한대 · NaN 은 밖입니다. */
        bool isInsideHub( float32 x, float32 z ) const;
        /**
         * @brief 새 아바타가 설 시작 자리입니다 — 다른 아바타가 `_startClearance` 안에 없는 첫 자리, 다 찼으면 가장 가까운 아바타가 가장 먼 자리.
         * @details 언리얼 `AGameModeBase::ChoosePlayerStart` 의 자리(빈 `APlayerStart` 먼저)다. 물리로 막힌 자리는 폰을 세울 때 곁의 빈 자리로 비킨다(`CharacterControllerComponent::findFreeSpot`).
         */
        float3 chooseAvatarStart() const;
        /** @brief 아바타 id 꼴인지 봅니다(1 이상, 놓인 것 id 아래). */
        static bool isValidAvatarID( uint32 avatarID ) { return avatarID != 0 && avatarID < kPlacementIDBase; }

        const HubAvatar*         findAvatar( uint32 avatarID ) const;
        const HubAvatar*         findAvatarByAccount( uint64 accountID ) const;
        const HubPlacementState* findPlacement( uint32 placementID ) const;
        /** @brief 아바타가 든 파티의 인원입니다. 파티가 없으면 1 입니다. 아바타가 없으면 0 입니다. */
        int32 getPartySize( uint32 avatarID ) const;
        /** @brief 진행 · 파티가 바뀌었으면 true 를 돌려주고 표시를 지웁니다(서버 링크가 알릴 때 부른다). */
        bool consumeDirtyFlag( uint32 avatarID );

        const HubRules&                  getRules() const { return _rules; }
        const vector<HubPlacementState>& getPlacements() const { return _listPlacement; }
        template <typename Func>
        void forEachAvatar( Func&& func ) const
        {
            for ( const auto& entry : _mapAvatar )
            {
                func( entry.second );
            }
        }

    private:
        HubAvatar* findAvatarMutable( uint32 avatarID );
        HubParty*  findParty( uint32 partyID );
        /** @brief 경험을 더하고 단계표를 넘은 만큼 레벨을 올립니다. */
        void addExperience( HubAvatar& avatar, int32 experience );
        /** @brief 쓰러뜨린 짐승의 보상 — 경험은 나눔 거리 안의 파티원에게 나누고 동전은 친 사람이 갖는다. */
        void grantDefeatReward( HubAvatar& striker, const float3& at );
        /** @brief 파티에서 뺍니다. 한 사람만 남으면 파티를 풉니다. */
        void detachFromParty( HubAvatar& avatar );

    private:
        HubRules                         _rules;
        unordered_map<uint32, HubAvatar> _mapAvatar;
        unordered_map<uint32, HubParty>  _mapParty;
        vector<HubPlacementState>        _listPlacement;
        uint32                           _nextAvatarID;
        uint32                           _nextPartyID;
    };
} // namespace sw
