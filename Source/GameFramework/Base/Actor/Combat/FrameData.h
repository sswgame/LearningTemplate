/**
 * @file FrameData.h
 * @brief 프레임 데이터 — 기술 하나의 발생 · 지속 · 후딜, 피해 · 경직, 공격 높이 · 판정 플래그, 캔슬 창, 프레임별 히트박스와 그 진행(타임라인)입니다.
 * @details 철권 · 캡슐파이터 · 소울라이크 · 위쳐 · 젤다의 근접 공격이 같은 표를 씁니다. 시간 단위는 프레임(정수)이라 결정적입니다 —
 *          게임은 `FixedStepTimer` 로 고정 걸음마다 `advanceFrame` 을 부릅니다.
 *
 *          프레임 번호는 1 부터 셉니다. `_startup` 은 첫 지속(active) 프레임의 번호입니다(철권 "i10" = 10 프레임째에 맞는다).
 *          그래서 발생 구간은 1 .. startup−1, 지속 구간은 startup .. startup+active−1, 후딜은 그다음 recovery 프레임,
 *          전체 길이 = startup − 1 + active + recovery 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class XmlNode;

    /** @brief 공격 높이입니다(철권 상단 · 중단 · 하단 · 잡기). */
    enum class AttackHeight : uint8
    {
        High = 0,
        Mid,
        Low,
        Throw
    };

    /** @brief 맞는 쪽의 가드 자세입니다. */
    enum class GuardStance : uint8
    {
        None = 0, ///< 가드하지 않는다(공격 · 후딜 중, 또는 앞으로 걷는 중)
        Standing, ///< 서서 가드(뒤로 누름)
        Crouching ///< 앉아 가드(뒤아래)
    };

    /** @brief 공격 높이 대 가드 자세의 결과입니다. */
    enum class GuardOutcome : uint8
    {
        Hit = 0, ///< 맞았다
        Blocked, ///< 막았다(가드 피해 · 가드 경직)
        Evaded   ///< 높이 때문에 헛쳤다(앉아서 상단 · 잡기를 피함)
    };

    /** @brief 히트박스 모양입니다. */
    enum class HitboxShape : uint8
    {
        Box = 0, ///< 중심 (x, y), 크기 (w, h)
        Capsule  ///< 중심 (x, y), 크기 (w, h) 의 둥근 끝 상자 — 반지름 = min(w, h) / 2
    };

    /** @brief 프레임 구간 하나의 히트박스입니다. 좌표는 기술을 쓰는 쪽 기준(x = 앞, y = 위), 단위는 게임이 정한다. */
    struct MoveHitbox
    {
        float32     _x{ 0.0f };
        float32     _y{ 0.0f };
        float32     _width{ 0.0f };
        float32     _height{ 0.0f };
        int32       _fromFrame{ 0 }; ///< 이 프레임부터(포함)
        int32       _toFrame{ 0 };   ///< 이 프레임까지(포함)
        HitboxShape _shape{ HitboxShape::Box };
    };
} // namespace sw

namespace sw
{
    /** @brief 캔슬 창 하나 — 이 프레임 구간에 이어 낼 수 있는 기술들입니다. */
    struct MoveCancelWindow
    {
        vector<hashed_string> _listMoveID{};
        int32                 _fromFrame{ 0 };
        int32                 _toFrame{ 0 };
        uint8                 _bOnHitOnly{ SW_FALSE }; ///< 맞거나 막혔을 때만(헛치면 캔슬 불가 — 철권 · 스트리트 파이터의 일반기 캔슬)
    };
} // namespace sw

namespace sw
{
    /** @brief 기술 하나의 프레임 데이터입니다. 프레임 수는 모두 정수, 피해는 게임이 정한 단위입니다. */
    struct SW_GF_API MoveFrameData
    {
        hashed_string            _id{};
        string                   _name{};
        vector<MoveHitbox>       _listHitbox{};
        vector<MoveCancelWindow> _listCancel{};
        float32                  _damage{ 0.0f };
        float32                  _chipDamage{ 0.0f }; ///< 가드 피해(막혀도 깎이는 양)
        int32                    _startup{ 1 };       ///< 첫 지속 프레임 번호(1 이상)
        int32                    _active{ 1 };
        int32                    _recovery{ 0 };
        int32                    _hitstun{ 0 };   ///< 맞은 쪽이 못 움직이는 프레임(맞은 프레임 다음부터)
        int32                    _blockstun{ 0 }; ///< 막은 쪽이 못 움직이는 프레임
        int32                    _hitstop{ 0 };   ///< 닿은 순간 양쪽이 멈추는 프레임(손맛 — 이득 계산에는 들지 않는다)
        AttackHeight             _height{ AttackHeight::Mid };
        uint8                    _bLauncher{ SW_FALSE };    ///< 띄운다(콤보 시동)
        uint8                    _bKnockdown{ SW_FALSE };   ///< 눕힌다
        uint8                    _bWallSplat{ SW_FALSE };   ///< 벽에 붙인다
        uint8                    _bUnblockable{ SW_FALSE }; ///< 가드 불가(높이로 피하는 것은 된다)

        /** @brief 전체 길이(프레임) = startup − 1 + active + recovery 입니다. */
        int32 getTotalFrames() const { return _startup - 1 + _active + _recovery; }
        /** @brief 마지막 지속 프레임 번호입니다. */
        int32 getLastActiveFrame() const { return _startup + _active - 1; }
        /** @brief 첫 지속 프레임에 닿았을 때의 프레임 이득(표에 적는 숫자)입니다. 정의는 `MoveTimeline::computeFrameAdvantage`. */
        int32 computeFrameAdvantage( bool bOnBlock ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class MoveCatalog
     * @brief `<MoveCatalog><Move id=".." startup="10" active="2" recovery="15" damage="12" chip="2" hitstun="20" blockstun="12" hitstop="8"
     *        height="Mid" launcher="false" knockdown="false" wallSplat="false" unblockable="false">
     *        <Hitbox from="10" to="11" x=".." y=".." w=".." h=".." shape="Box"/><Cancel from="12" to="20" moves="a,b" onHit="true"/></Move></MoveCatalog>` 를 읽습니다.
     */
    class SW_GF_API MoveCatalog : public XmlCatalog<MoveCatalog>
    {
        friend class XmlCatalog<MoveCatalog>;

    public:
        MoveCatalog();

        void addMove( const MoveFrameData& move );

        const MoveFrameData*         findMove( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<MoveFrameData>& getMoves() const { return _catalog.getAll(); }

        /** @brief "High" · "Mid" · "Low" · "Throw"(대소문자 무시)를 읽습니다. 모르면 @p fallback 입니다. */
        static AttackHeight parseAttackHeight( string_view text, AttackHeight fallback );

    private:
        static constexpr const utf8* kXmlRootName = "MoveCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<MoveFrameData> _catalog;
    };
} // namespace sw

namespace sw
{
    /** @brief 기술 진행 위상입니다. */
    enum class MovePhase : uint8
    {
        Idle = 0, ///< 기술을 쓰지 않는다
        Startup,
        Active,
        Recovery,
        Finished ///< 마지막 프레임이 지났다(다음 기술 · 중립으로)
    };

    /**
     * @class MoveTimeline
     * @brief 기술 하나를 프레임 단위로 진행합니다 — 위상 · 지금 켜진 히트박스 · 캔슬 가능 여부 · 프레임 이득 · 히트스톱.
     * @details 정의는 복사해 들고 있습니다(카탈로그가 바뀌어도 진행 중인 기술은 그대로). 히트스톱이 남아 있으면 `advanceFrame` 은 프레임을 넘기지
     *          않고 히트스톱만 줄입니다 — 맞은 쪽도 같은 수만큼 멈추므로 이득에는 들지 않습니다.
     */
    class SW_GF_API MoveTimeline
    {
    public:
        MoveTimeline();

        /** @brief 기술을 시작합니다. 지금 프레임은 1(첫 프레임이 화면에 나온 상태)입니다. */
        void start( const MoveFrameData& move );
        /** @brief 기술을 버리고 중립으로 돌아갑니다(맞아서 끊김). */
        void cancel();
        /**
         * @brief 상태를 바로 넣습니다(롤백 · 리플레이 복원). @p frame 은 1 부터 기술 전체 길이까지, @p hitstopRemaining 은 0 이상이어야 합니다.
         * @details 기술 자체(`MoveFrameData`)는 바이트에 싣지 않고 쓰는 쪽이 id 로 다시 찾아 넘깁니다 — 상태는 프레임 · 히트스톱 · 닿음뿐입니다.
         * @return 범위를 벗어나면 false 이고 타임라인은 그대로입니다.
         */
        [[nodiscard]] bool restoreState( const MoveFrameData& move, int32 frame, int32 hitstopRemaining, bool bContact, bool bBlocked );
        /**
         * @brief 한 프레임 진행합니다. 히트스톱 중이면 히트스톱만 하나 줄이고 false 입니다.
         * @return 프레임이 실제로 넘어갔으면 true.
         */
        bool advanceFrame();
        /** @brief 닿았음을 표시하고 기술의 `_hitstop` 만큼 멈춥니다(`_bOnHitOnly` 캔슬 창을 엽니다). @p bBlocked 는 가드에 막혔는지. */
        void registerContact( bool bBlocked );
        /** @brief 히트스톱을 더 겁니다(이미 남은 것보다 길 때만). */
        void applyHitstop( int32 frames );

        MovePhase getPhase() const;
        /** @brief 지금 프레임 번호(1 부터, 시작 전 0)입니다. */
        int32                getFrame() const { return _frame; }
        const MoveFrameData& getMove() const { return _move; }
        bool                 isPlaying() const { return _bPlaying == SW_TRUE; }
        bool                 hasContact() const { return _bContact == SW_TRUE; }
        bool                 wasBlocked() const { return _bBlocked == SW_TRUE; }
        bool                 isInHitstop() const { return _hitstopRemaining > 0; }
        int32                getHitstopRemaining() const { return _hitstopRemaining; }
        /** @brief 지금 프레임에 켜진 히트박스를 @p outListHitbox 에 채웁니다(먼저 비운다). 개수입니다. */
        uint32 collectActiveHitboxes( vector<MoveHitbox>& outListHitbox ) const;
        /** @brief 지금 프레임에 @p moveID 로 캔슬할 수 있는가입니다. */
        bool canCancelInto( const hashed_string& moveID ) const;
        /**
         * @brief 지금 프레임에 닿았다고 보고 낸 프레임 이득입니다.
         * @details 이득 = (막혔으면 blockstun, 아니면 hitstun) − (남은 지속 프레임 + 후딜)
         *              = stun − (전체 길이 − 지금 프레임).
         *          지금 프레임이 닿은 프레임이고, 맞은 쪽의 경직은 그다음 프레임부터 셉니다. 양수면 공격 쪽이 먼저 움직입니다(+ 이득),
         *          음수면 막힌 뒤 반격당할 수 있는 프레임 수입니다(−10 이하는 철권에서 확정 반격). 히트스톱은 양쪽이 같이 멈춰 빠집니다.
         */
        int32 computeFrameAdvantage( bool bOnBlock ) const;

        /**
         * @brief 공격 높이 대 가드 자세(철권 규칙)입니다.
         * @details - 가드 없음: 다 맞는다.
         *          - 서서 가드: 상단 · 중단은 막고, 하단은 맞는다. 잡기는 가드로 막지 못한다(잡기 풀기는 게임이 따로).
         *          - 앉아 가드: 상단 · 잡기는 머리 위로 헛친다(피함), 하단은 막고, 중단은 맞는다.
         *          - 가드 불가(@p bUnblockable)는 막힐 자리에서 맞지만, 높이로 피하는 것은 그대로다.
         */
        static GuardOutcome computeGuardOutcome( AttackHeight height, GuardStance stance, bool bUnblockable = false );
        /** @brief `computeGuardOutcome( height, stance ) == Blocked` 입니다. */
        static bool isBlocked( AttackHeight height, GuardStance stance ) { return computeGuardOutcome( height, stance ) == GuardOutcome::Blocked; }

        /** @brief 재생 중이면 기술 id · 프레임 · 히트스톱 · 접촉 · 막힘을 씁니다. 기술 정의는 카탈로그의 것이라 id 만 싣습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다 — 기술은 @p catalog 에서 id 로 찾습니다. 깨졌거나 없는 기술이면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive, const MoveCatalog& catalog );

    private:
        MoveFrameData _move;
        int32         _frame;
        int32         _hitstopRemaining;
        uint8         _bPlaying;
        uint8         _bContact;
        uint8         _bBlocked;
    };
} // namespace sw
