/**
 * @file ValeWorld.h
 * @brief LanternVale(작은 액션 MMO 수직 슬라이스)의 서버 권위 규칙입니다 — 등불 골짜기에서 그림자 짐승을 치고(전투) 빛버섯을 캐며(생활) 경험 · 동전을 얻어 자라고,
 *        파티로 함께 사냥해 경험을 나눕니다. 클라이언트는 이 규칙을 돌리지 않는다 — 결과는 서버가 정해 알린다(`ValeNetLink`).
 * @details 엔진 · 망 없이 도는 순수 규칙이라 단위 테스트가 그대로 부른다. 수치는 씬 데이터(`ValeRules` — `game/lanternvale/maps/vale.scene.xml`)다.
 *          - 판정: 공격은 사거리 · 재사용 대기 · 대상 생존, 채집은 거리 · 대상 생존, 이동은 한 번 걸음 상한과 골짜기 경계를 서버가 본다(클라이언트 값은 요청일 뿐이다).
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

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 골짜기에 놓이는 것의 갈래입니다. */
    ENUM()
    enum class ValePlacementKind : uint8
    {
        Creature = 0, ///< 그림자 짐승 — 쳐서 쓰러뜨린다
        Herb          ///< 빛버섯 — 곁에서 캔다
    };

    /** @brief 행동 하나를 서버가 판정한 결과입니다. 클라이언트에 그대로 알린다(`ValeAvatarAgent::receiveActionResult`). */
    ENUM()
    enum class ValeActionResult : uint8
    {
        Ok = 0,
        UnknownTarget,  ///< 그런 대상이 없다
        OutOfRange,     ///< 대상이 사거리 · 채집 거리 밖이다
        CoolingDown,    ///< 공격 재사용 대기 중이다
        TargetGone,     ///< 대상이 이미 쓰러졌거나 캐였다(다시 나기를 기다린다)
        StepTooLong,    ///< 한 번에 걸을 수 있는 거리를 넘었다
        PartyFull,      ///< 파티 인원이 찼다
        AlreadyInParty, ///< 초대받은 쪽이 이미 다른 파티에 있다
        NotInvited,     ///< 그 사람에게 받은 초대가 없다
        NotInParty      ///< 파티에 있지 않다
    };
} // namespace sw

namespace sw
{
    /** @brief 골짜기에 놓이는 것 하나입니다(씬 데이터). */
    REFLECT()
    struct ValePlacement
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Creature to strike or herb to gather" )
        ValePlacementKind _kind{ ValePlacementKind::Creature };
        PROPERTY()
        float3 _position{};
    };

    /**
     * @brief 골짜기 규칙의 수치입니다(씬의 `ValeWorldComponent` 칸). 범위는 칸의 `PROPERTY( Min · Max )` 이고, 칸 사이의 관계(단계표 오름차순 · 놓을 것 수)는 `ValeWorld::initialize` 가 봅니다.
     */
    REFLECT()
    struct ValeRules
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
        PROPERTY( Min = 0.1, Units = m, Tooltip = "Longest walk one move request may ask for - also the most walk budget an avatar can save up" )
        float32 _maxMoveStep{ 12.0f };
        PROPERTY( Required, Min = 0.1, Units = "m/s", Tooltip = "Fastest an avatar may walk - the server refills this much walk budget per second and moves spend it" )
        float32 _moveSpeed{ 0.0f };
        PROPERTY( Min = 1.0, Units = m, Tooltip = "The vale spans -extent..extent on X and Z" )
        float32 _worldHalfExtent{ 64.0f };
        PROPERTY( Min = 2, Max = 8 )
        int32 _partyMaxSize{ 4 };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Party members within this distance of a felled creature share its experience" )
        float32 _partyShareRange{ 30.0f };
        PROPERTY( Min = 1.0, Units = s, Tooltip = "How often the server writes the profiles of everyone online" )
        float32 _autosaveSeconds{ 30.0f };
        PROPERTY()
        float3 _avatarSpawn{};
        /** @brief 레벨 n + 1 에 오르는 누적 경험(첫 칸이 레벨 2)입니다. 오름차순이어야 하고, 마지막 칸을 넘으면 최고 레벨입니다. */
        PROPERTY( Required )
        vector<int32> _listLevelExperience{};
        PROPERTY( Required )
        vector<ValePlacement> _listPlacement{};
    };

    /**
     * @brief 계정 하나의 영속 진행입니다. 서버가 쓰고 읽는다(`ValeProfileStore`) — 클라이언트는 받은 값을 보여 줄 뿐이다.
     * @details 바이트는 손으로 쓰는 판 있는 형식이다(`kFormatVersion`). 칸을 바꾸면 판을 올리고 옛 판 읽기를 둔다 — 지금은 판 1 하나다.
     */
    REFLECT()
    struct ValeProfile
    {
        REFLECT_BODY();

        static constexpr uint32 kFormatVersion = 1;

        PROPERTY( SaveGame )
        int32 _level{ 1 };
        PROPERTY( SaveGame )
        int32 _experience{ 0 };
        PROPERTY( SaveGame )
        int32 _coins{ 0 };
        PROPERTY( SaveGame )
        int32 _herbCount{ 0 };
        PROPERTY( SaveGame )
        int32 _defeatCount{ 0 };
        PROPERTY( SaveGame )
        float3 _position{};

        void write( BitWriter& writer ) const;
        /** @brief 읽습니다. 깨졌거나 값이 범위 밖(음수 · 레벨 0)이면 false 이고 그때 값은 쓰지 않습니다. */
        [[nodiscard]] bool read( BitReader& reader );
    };
} // namespace sw

namespace sw
{
    /** @brief 골짜기에 접속한 아바타 하나입니다. */
    struct ValeAvatar
    {
        ValeProfile _profile{};
        uint64      _accountID{ 0 };
        uint32      _avatarID{ 0 };
        uint32      _partyID{ 0 };      ///< 0 이면 파티가 없다
        uint32      _inviteFromID{ 0 }; ///< 받은 초대를 보낸 아바타. 0 이면 없다
        float32     _attackCooldown{ 0.0f };
        float32     _moveBudget{ 0.0f };   ///< 지금 걸을 수 있는 거리(m) — 틱이 `_moveSpeed × dt` 만큼 채우고(`_maxMoveStep` 까지) 이동이 쓴다(감사 K-1)
        uint8       _bChanged{ SW_FALSE }; ///< 알릴 진행 · 파티가 바뀌었다(`ValeWorld::consumeDirtyFlag`)
    };

    /** @brief 놓인 것 하나의 지금 상태입니다. */
    struct ValePlacementState
    {
        ValePlacement _def{};
        uint32        _placementID{ 0 };
        int32         _health{ 0 };
        float32       _respawnTimer{ 0.0f };
        uint8         _bPresent{ SW_TRUE }; ///< 쓰러졌거나 캐였으면 다시 날 때까지 거짓
    };

    /** @brief 파티 하나입니다. 첫 사람이 이끈다. */
    struct ValeParty
    {
        vector<uint32> _listMember{};
        uint32         _partyID{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ValeWorld
     * @brief 골짜기 하나의 서버 상태와 규칙입니다. 게임 스레드에서 씁니다.
     */
    class ValeWorld final
    {
    public:
        /** @brief 놓인 것의 id 는 이 수부터 놓인 차례대로입니다. 아바타 id 는 1 부터라 둘은 겹치지 않는다(복제 엔티티 id 로 같이 쓴다). */
        static constexpr uint32 kPlacementIDBase = 100000;
        static constexpr uint32 kMaxPlacement    = 1024;
        static constexpr int32  kMaxLevelStep    = 64;

        ValeWorld();

        /** @brief 규칙을 받아 골짜기를 세웁니다. 단계표가 비었거나 오름차순이 아니거나, 놓을 것이 없거나 너무 많거나 경계 밖이면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool initialize( const ValeRules& rules );

        /** @brief 아바타를 들입니다. 진행 @p profile 은 저장소에서 읽은 것(새 계정은 기본값)입니다. 돌려준 id 는 접속이 끝날 때까지 이 아바타를 가리킨다. */
        uint32 addAvatar( uint64 accountID, const ValeProfile& profile );
        /** @brief 아바타를 내보냅니다(파티를 떠난다). */
        void removeAvatar( uint32 avatarID );

        ValeActionResult attackCreature( uint32 avatarID, uint32 placementID );
        ValeActionResult gatherHerb( uint32 avatarID, uint32 placementID );
        ValeActionResult moveAvatar( uint32 avatarID, float32 x, float32 z );
        ValeActionResult inviteToParty( uint32 avatarID, uint32 targetAvatarID );
        ValeActionResult acceptPartyInvite( uint32 avatarID, uint32 leaderAvatarID );
        ValeActionResult leaveParty( uint32 avatarID );

        /** @brief 재사용 대기와 다시 나기를 흘립니다. */
        void tick( float32 deltaSeconds );

        /** @brief @p placementID 가 그 갈래의 놓인 것인지 봅니다(살아 있는지와 상관없이) — 클라이언트가 지어낸 번호를 거르는 검증이 씁니다. */
        bool isPlacementOfKind( uint32 placementID, ValePlacementKind kind ) const;
        /** @brief 점이 골짜기 경계 안인지 봅니다. 무한대 · NaN 은 밖입니다. */
        bool isInsideVale( float32 x, float32 z ) const;
        /** @brief 아바타 id 꼴인지 봅니다(1 이상, 놓인 것 id 아래). */
        static bool isValidAvatarID( uint32 avatarID ) { return avatarID != 0 && avatarID < kPlacementIDBase; }

        const ValeAvatar*         findAvatar( uint32 avatarID ) const;
        const ValeAvatar*         findAvatarByAccount( uint64 accountID ) const;
        const ValePlacementState* findPlacement( uint32 placementID ) const;
        /** @brief 아바타가 든 파티의 인원입니다. 파티가 없으면 1 입니다. 아바타가 없으면 0 입니다. */
        int32 getPartySize( uint32 avatarID ) const;
        /** @brief 진행 · 파티가 바뀌었으면 true 를 돌려주고 표시를 지웁니다(서버 링크가 알릴 때 부른다). */
        bool consumeDirtyFlag( uint32 avatarID );

        const ValeRules&                  getRules() const { return _rules; }
        const vector<ValePlacementState>& getPlacements() const { return _listPlacement; }
        template <typename Func>
        void forEachAvatar( Func&& func ) const
        {
            for ( const auto& entry : _mapAvatar )
            {
                func( entry.second );
            }
        }

    private:
        ValeAvatar* findAvatarMutable( uint32 avatarID );
        ValeParty*  findParty( uint32 partyID );
        /** @brief 경험을 더하고 단계표를 넘은 만큼 레벨을 올립니다. */
        void addExperience( ValeAvatar& avatar, int32 experience );
        /** @brief 쓰러뜨린 짐승의 보상 — 경험은 나눔 거리 안의 파티원에게 나누고 동전은 친 사람이 갖는다. */
        void grantDefeatReward( ValeAvatar& striker, const float3& at );
        /** @brief 파티에서 뺍니다. 한 사람만 남으면 파티를 풉니다. */
        void detachFromParty( ValeAvatar& avatar );

    private:
        ValeRules                         _rules;
        unordered_map<uint32, ValeAvatar> _mapAvatar;
        unordered_map<uint32, ValeParty>  _mapParty;
        vector<ValePlacementState>        _listPlacement;
        uint32                            _nextAvatarID;
        uint32                            _nextPartyID;
    };
} // namespace sw
