/**
 * @file Component.h
 * @brief GameObject 에 붙는 컴포넌트의 기반 클래스와 TickGroup · TickPhase 정의입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Animation/AnimPlayback.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{

    class GameObject;
    class GameObjectManager;
    class PoolAllocator;
    /**
     * @enum TickGroup
     * @brief 한 프레임 안에서 컴포넌트 틱이 도는 순서 슬롯입니다.
     * @details 이름대로 물리 파이프라인 단계(언리얼 ETickingGroup)입니다 — 물리는 **DuringPhysics 와 PostPhysics 사이**에 한 번 step 합니다
     *          (`GameObjectManager::tick`: PrePhysics · DuringPhysics → 틱 결과 적용(구조 변경 · 틱 쓰기 · 틱 뒤 큐) → 애니메이션 → 물리(겹침 · 강체, 이벤트) →
     *          PostPhysics · PostUpdate → 틱 결과 적용). 그래서 PostPhysics 이후의 틱은 **이번 프레임의** 바디 자세 · 겹침을 보고, 앞의 두 그룹은 지난 프레임의
     *          것을 봅니다. 애니메이션 파라미터를 PostPhysics 이후에 쓰면 다음 프레임 포즈에 듭니다.
     */
    enum class TickGroup : uint8
    {
        PrePhysics,    ///< 이른 업데이트 단계
        DuringPhysics, ///< 기본 tick 단계
        PostPhysics,   ///< 늦은 업데이트 단계
        PostUpdate,    ///< 렌더 직전 등 최종 단계
    };

    /** @brief `TickGroup` 이 유효한 값(PrePhysics..PostUpdate)인지입니다. 정수에서 캐스트한 값은 넘을 수 있습니다. */
    constexpr bool isValidTickGroup( TickGroup group )
    {
        return static_cast<uint32>( group ) <= static_cast<uint32>( TickGroup::PostUpdate );
    }

    /**
     * @enum TickPhase
     * @brief 같은 TickGroup 안의 세부 실행 단계입니다. 단계는 64 칸 간격이고, 서브틱 우선순위(0..`kMaxTickPriority`)가 그 칸 안의 자리입니다.
     */
    enum class TickPhase : uint8
    {
        Early    = 0,   ///< 선행 연산 (데이터 준비, 물리 전처리)
        Normal   = 64,  ///< 기본 연산 (일반 게임플레이)
        Late     = 128, ///< 후행 연산 (래그돌 합성, 소켓 어태치먼트)
        Finalize = 192  ///< 최종 연산 (GPU 버퍼 업로드, LOD 계산)
    };

    /** @brief 서브틱 우선순위의 상한입니다. 넘으면 이 값으로 묶습니다 — 단계(64 칸)를 넘어 다음 단계로 가지 않습니다. */
    inline constexpr uint8 kMaxTickPriority = 63;

    /**
     * @struct SubTickHandle
     * @brief 컴포넌트의 틱 하나(서브틱, `_subTickId` 0 이면 주 틱 `onTick`)를 식별하고 선행 조건(prerequisite)을 잇는 데 쓰는 핸들입니다.
     */
    struct SubTickHandle
    {
        uint64 _componentId{ 0 };
        uint32 _subTickId{ 0 };
        /**
         * @brief 그 컴포넌트를 가진 오브젝트입니다. 비교 · 해시에는 쓰지 않습니다.
         * @details 등록부가 선행 조건이 가리키는 항목을 씬 전체를 훑지 않고 그 오브젝트에서만 찾는 데 씁니다. 0 이면(소유자 없이 만든 핸들)
         *          선행 조건으로 받지 않습니다.
         */
        uint64 _objectId{ 0 };

        constexpr bool isValid() const
        {
            return _componentId != 0;
        }

        constexpr bool operator==( const SubTickHandle& other ) const
        {
            return _componentId == other._componentId && _subTickId == other._subTickId;
        }

        constexpr bool operator!=( const SubTickHandle& other ) const
        {
            return !( *this == other );
        }
    };
} // namespace sw

namespace sw
{
    struct SubTickHandleHash
    {
        size_t operator()( const SubTickHandle& handle ) const noexcept
        {
            return static_cast<size_t>( handle._componentId ^ ( static_cast<uint64>( handle._subTickId ) << 32 ) );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 부모에 붙이거나 뗄 때 무엇을 지킬지입니다(언리얼 `EAttachmentRule` · 유니티 `SetParent( parent, worldPositionStays )`).
     * @details `KeepRelative` 는 로컬 값을 그대로 두어 새 부모를 따라 월드 자리가 바뀝니다 — 상태 읽기 · 코드의 기본입니다(저장된 로컬은 부모 기준이다).
     *          `KeepWorld` 는 월드 자리를 지키도록 로컬 값을 다시 구합니다 — 에디터의 재부모 · 부모 떼기가 씁니다(`KeepRelative` 로 재부모하면
     *          계층 창에서 끌어 놓은 오브젝트가 새 부모만큼 **튄다**).
     */
    enum class AttachRule : uint8
    {
        KeepRelative,
        KeepWorld
    };

    /**
     * @struct SubTickInfo
     * @brief 컴포넌트에 등록된 서브틱 하나의 메타데이터와 선행 조건 정보입니다.
     * @details 활성은 둘이다. `_bActive` 는 틱 등록부가 읽는 값이라 틱 중이면 틱 뒤에 바뀌고, `_bRunnable` 은 실행 직전에 보는 값이라 틱 중에도
     *          바로 바뀐다(다른 워커가 쓰므로 원자다). 틱 동안 목록의 모양은 얼어 있어(구조 변경은 미룬다) 원소를 가리켜 써도 된다.
     *          서브틱 1~63 은 컴포넌트의 원자 마스크가 같은 일을 O(1) 로 한다 — `_bRunnable` 은 64 번부터의 정본이다.
     */
    struct SubTickInfo
    {
        uint32                _subTickId;
        TickGroup             _group;
        TickPhase             _phase;
        uint8                 _priority;
        uint8                 _bActive;
        atomic<uint8>         _bRunnable;
        vector<SubTickHandle> _listPrerequisite;

        SubTickInfo()
            : _subTickId{ 0 }
            , _group{ TickGroup::DuringPhysics }
            , _phase{ TickPhase::Normal }
            , _priority{ 0 }
            , _bActive{ SW_TRUE }
            , _bRunnable{ SW_TRUE }
            , _listPrerequisite{}
        {
        }

        SubTickInfo( const SubTickInfo& other )
            : _subTickId{ other._subTickId }
            , _group{ other._group }
            , _phase{ other._phase }
            , _priority{ other._priority }
            , _bActive{ other._bActive }
            , _bRunnable{ other._bRunnable.load( std::memory_order_relaxed ) }
            , _listPrerequisite{ other._listPrerequisite }
        {
        }

        SubTickInfo( SubTickInfo&& other ) noexcept
            : _subTickId{ other._subTickId }
            , _group{ other._group }
            , _phase{ other._phase }
            , _priority{ other._priority }
            , _bActive{ other._bActive }
            , _bRunnable{ other._bRunnable.load( std::memory_order_relaxed ) }
            , _listPrerequisite{ std::move( other._listPrerequisite ) }
        {
        }

        SubTickInfo& operator=( const SubTickInfo& other )
        {
            SubTickInfo copy( other );
            return *this = std::move( copy );
        }

        SubTickInfo& operator=( SubTickInfo&& other ) noexcept
        {
            _subTickId = other._subTickId;
            _group     = other._group;
            _phase     = other._phase;
            _priority  = other._priority;
            _bActive   = other._bActive;
            _bRunnable.store( other._bRunnable.load( std::memory_order_relaxed ), std::memory_order_relaxed );
            _listPrerequisite = std::move( other._listPrerequisite );
            return *this;
        }

        /** @brief 지금 실행해도 되는지 반환합니다(틱 중에 다른 스레드가 바꿀 수 있습니다). */
        bool isRunnable() const { return _bRunnable.load( std::memory_order_acquire ) == SW_TRUE; }
        /** @brief 실행 여부를 바로 바꿉니다. 틱 중에도 부를 수 있습니다. */
        void setRunnable( bool bRunnable ) { _bRunnable.store( bRunnable ? SW_TRUE : SW_FALSE, std::memory_order_release ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct OverlapInfo
     * @brief 겹침 시작 · 끝 하나입니다 — 상대 오브젝트와, 어느 콜라이더끼리였는지(트리거 여부) · 닿은 때입니다(언리얼 `OnComponentBeginOverlap` 의 인자들).
     * @details 겹침은 바디(콜라이더) 쌍마다 납니다. 오브젝트 하나에 막는 콜라이더와 트리거(감지 범위)가 함께 있으면 같은 상대에게서 둘이 올 수 있고,
     *          받는 쪽은 트리거 여부로 가립니다 — 투사체는 상대의 트리거에 막히지도 피해를 주지도 않는다.
     */
    struct OverlapInfo
    {
        GameObject*            _pOther{ nullptr }; ///< 상대 오브젝트. 끝 이벤트에서 상대가 이미 사라졌으면 nullptr
        PhysicsBodyHandle      _selfBody{};        ///< 강체 물리의 트리거면 이 오브젝트 쪽 바디(히트 존 · 래그돌 뼈를 가린다). 겹침 월드(`PhysicsWorld`)면 무효
        PhysicsBodyHandle      _otherBody{};       ///< 강체 물리의 트리거면 상대 쪽 바디
        float32                _time{ 1.0f };      ///< 이번 물리 step 안에서 닿은 때(0..1). 연속 바디가 쓸려서 닿은 시작만 1 보다 작다
        uint8                  _bSelfTrigger  : 1; ///< 이 오브젝트 쪽 콜라이더가 트리거인지
        uint8                  _bOtherTrigger : 1; ///< 상대 쪽 콜라이더가 트리거인지
        [[maybe_unused]] uint8 _reserved      : 6;

        /** @brief 상대 없음 · 막는 콜라이더끼리로 둡니다. */
        OverlapInfo() noexcept
            : _bSelfTrigger{ SW_FALSE }
            , _bOtherTrigger{ SW_FALSE }
            , _reserved{ 0 } {}
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CollisionInfo
     * @brief 강체 물리의 막는 접촉 하나입니다(유니티 `Collision` · 언리얼 `FHitResult` 의 자리). 2D 접촉은 Z = 0 입니다.
     * @details 접촉은 바디 쌍마다 납니다 — 래그돌 · 컴파운드처럼 한 오브젝트에 바디가 여럿이면 `_selfBody` 로 어느 바디(뼈 · 파편)였는지 가립니다.
     *          `_impulse` 는 충격량 크기(뉴턴초)입니다: 시작은 부딪힌 충격, 유지는 그 스텝의 충격량, 끝은 0 — 피해 · 파괴 · 소리 세기에 씁니다.
     */
    struct CollisionInfo
    {
        GameObject*       _pOther{ nullptr }; ///< 상대 오브젝트. 상대가 오브젝트에 속하지 않거나 사라졌으면 nullptr
        PhysicsBodyHandle _selfBody{};        ///< 이 오브젝트 쪽 바디
        PhysicsBodyHandle _otherBody{};       ///< 상대 쪽 바디
        float3            _point{};           ///< 대표 접촉점(월드)
        float3            _normal{};          ///< 이 오브젝트에서 상대를 향하는 법선(월드)
        float32           _impulse{ 0.0f };   ///< 접촉 충격량 크기(뉴턴초)
        bool              _bIs2D{ false };    ///< 2D 물리 씬의 접촉이면 true
    };
} // namespace sw

namespace sw
{
    /**
     * @struct HitInfo
     * @brief 이 오브젝트가 맞았습니다 — 근접 판정(애니메이션 알림의 칼 궤적) · 무기 레이캐스트가 냅니다(언리얼 `FHitResult` + `TakeDamage` 인자).
     * @details 엔진은 체력을 모릅니다 — 피해(`_damage` × `_damageMultiplier`)를 어떻게 쓸지는 받는 컴포넌트(게임의 체력 · 래그돌의 움찔)가 정합니다.
     *          히트 존은 맞은 바디로 고릅니다(래그돌 · 히트박스의 물리 에셋, 강체의 히트 존 속성). 2D 맞음은 Z = 0 입니다.
     */
    struct HitInfo
    {
        GameObject*       _pInstigator{ nullptr };   ///< 때린 오브젝트(없으면 nullptr)
        PhysicsBodyHandle _body{};                   ///< 맞은 바디
        hashed_string     _zone{};                   ///< 히트 존 이름(없으면 빈 이름)
        hashed_string     _kind{};                   ///< 때린 것의 이름(알림 이름 · 무기 종류)
        float3            _point{};                  ///< 맞은 점(월드)
        float3            _normal{};                 ///< 맞은 면의 법선(월드)
        float3            _direction{};              ///< 때린 방향(월드, 단위)
        float32           _damage{ 0.0f };           ///< 기본 피해
        float32           _damageMultiplier{ 1.0f }; ///< 히트 존 배율
        float32           _impulse{ 0.0f };          ///< 맞은 바디에 줄 충격량 크기(뉴턴초)
        int32             _bodyIndex{ -1 };          ///< 래그돌 · 히트박스의 바디 번호(물리 에셋 순서). 없으면 -1
        bool              _bIs2D{ false };           ///< 2D 물리 씬의 맞음이면 true
        bool              _bFatal{ false };          ///< 때린 쪽이 이 맞음으로 죽는다고 판정했다(절단 · 래그돌 전환의 신호)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimNotifyInfo
     * @brief 애니메이션 알림이 이 오브젝트의 컴포넌트에 보내는 게임플레이 이벤트입니다(알림 표의 `GameplayEvent` 처리기 — 언리얼 AnimNotify 의 이벤트).
     */
    struct AnimNotifyInfo
    {
        hashed_string   _notify{};                          ///< 클립의 알림 이름
        hashed_string   _event{};                           ///< 알림 표가 정한 이벤트 이름
        float32         _weight{ 1.0f };                    ///< 울린 클립의 섞임 가중치
        AnimNotifyPhase _phase{ AnimNotifyPhase::Instant }; ///< 구간 알림이면 시작 · 끝
    };
} // namespace sw

namespace sw
{
    /**
     * @class Component
     * @brief GameObject 에 기능과 데이터를 덧붙이는 컴포넌트의 기반 클래스입니다.
     * @details 리플렉션에는 **만들 수 없는 기반**(`Abstract`)으로 등록한다 — 팩토리가 없어 이름으로 붙일 수 없다. 주의: 등록하지 않으면
     *          모든 컴포넌트의 부모 사슬이 여기서 끊긴다(`SceneComponent : Component` 의 부모가 "없음").
     */
    REFLECT( Abstract, Category = "Core", DisplayName = "Component", Tooltip = "Base of every component" )
    class SW_API Component
    {
        friend class GameObject;
        friend class GameObjectManager; ///< 생성이 `_pPool` 을 적고 파괴가 읽습니다

    public:
        REFLECT_BODY();

        /** @brief 기본 컴포넌트를 만듭니다. */
        Component();

        /** @brief 복사를 금지합니다. */
        Component( const Component& ) = delete;
        /** @brief 대입을 금지합니다. */
        Component& operator=( const Component& ) = delete;

        /**
         * @brief **옮기지 않습니다.** 컴포넌트는 풀 안의 제자리에서 만들고 없앱니다.
         * @details 컴포넌트를 옮기는 곳은 없습니다. 옮기면 원본과 사본이 같은 `_componentId` 를 갖고 `findComponentById` 가
         *          어느 쪽이든 내놓을 수 있으므로 막습니다. 파생 타입도 이동 연산을 선언하지 않습니다.
         * @see SceneComponent 도 계층 포인터까지 얽혀 있어 같은 이유로 막혀 있습니다.
         */
        Component( Component&& other )            = delete;
        Component& operator=( Component&& other ) = delete;

        /** @brief 가상 소멸자입니다. 해체는 GameObjectManager::destroyComponentInstance 가 맡습니다(해제 콜백 · 소멸 · 풀 반납). */
        virtual ~Component() = default;

        /** @brief 게임 컴포넌트 기본값 XML(gamesettings.xml) 경로를 지정합니다. 비어 있으면 주입하지 않습니다. */
        static void setDefaultGameSettingsPath( string_view path );
        /** @brief 현재 게임 컴포넌트 기본값 XML 경로를 반환합니다. */
        static string getDefaultGameSettingsPath();

        /**
         * @brief 게임플레이가 시작될 때 불리는 초기화 콜백입니다. 인스턴스마다 **정확히 한 번**, `onEndPlay` 와 짝을 이룹니다.
         * @details 월드가 플레이 중이면(에디터 Play · 에디터 없는 App · Shipping — `SceneManager::setWorldPlaying`) 활성 씬의 컴포넌트는
         *          플레이가 시작될 때, 플레이 중에 붙은 컴포넌트는 **다음 틱 단계**에서 불립니다 — 붙인 직후 세팅한 필드 · 상태 로드가 채운
         *          PROPERTY 를 봅니다. 활성 여부와 무관하게 불립니다(언리얼과 같다). 되돌리기 · 핫 리로드로 다시 만든 인스턴스는 새 인스턴스라
         *          다시 불립니다 — 런타임에만 있는 상태를 짓는 곳입니다. 직접 부르지 말고 `dispatchBeginPlay` 를 쓰십시오.
         */
        virtual void onBeginPlay();
        /** @brief 게임플레이가 끝날 때(플레이 종료 · 컴포넌트 해체) 불리는 정리 콜백입니다. `onBeginPlay` 가 불린 인스턴스에만 한 번 불립니다. */
        virtual void onEndPlay();
        /**
         * @brief 아직 시작하지 않았으면 `onBeginPlay` 를 부르고 "시작됨" 으로 적습니다. 삭제 대기면 부르지 않습니다.
         * @details 게임 스레드에서 병렬 틱 밖에서만 부릅니다 — "시작됨" 비트가 틱 워커가 읽는 비트(`_bCanEverTick`)와 한 바이트입니다.
         */
        void dispatchBeginPlay();
        /** @brief 시작했으면 `onEndPlay` 를 부르고 "시작됨" 을 지웁니다. 시작한 적 없으면 아무것도 하지 않습니다. */
        void dispatchEndPlay();
        /** @brief `onBeginPlay` 가 불렸고 아직 `onEndPlay` 가 불리지 않았으면 true 입니다. */
        bool hasBegunPlay() const { return _bHasBegunPlay == SW_TRUE; }
        /** @brief 프레임마다 불리는 주 업데이트 콜백입니다. */
        virtual void onTick( float32 deltaTime );
        /** @brief 프레임마다 서브틱별로 불리는 보조 업데이트 콜백입니다. */
        virtual void onSubTick( uint32 subTickId, float32 deltaTime );
        /**
         * @brief 이 오브젝트의 콜라이더가 다른 오브젝트의 콜라이더와 겹치기 시작했습니다(유니티 `OnTriggerEnter2D` · 언리얼 `BeginOverlap`).
         * @details 틱 · 트랜스폼 적용 뒤에 게임 스레드에서 오브젝트의 켜진 컴포넌트마다 불립니다 — 스폰 · 파괴 · 구조 변경을 그 자리에서 해도 됩니다.
         *          한 step 의 겹침은 닿은 때(`OverlapInfo::_time`) 순서로 옵니다.
         */
        virtual void onOverlapBegin( const OverlapInfo& overlap ) { (void)overlap; }
        /** @brief 겹침이 끝났습니다(떨어짐 · 꺼짐 · 사라짐). 상대가 이미 사라졌으면 `OverlapInfo::_pOther` 는 nullptr 입니다. */
        virtual void onOverlapEnd( const OverlapInfo& overlap ) { (void)overlap; }
        /** @brief 트리거와 겹친 채로 물리 스텝 하나가 지났습니다(유니티 `OnTriggerStay`). 강체 물리의 트리거만 냅니다 — 겹침 월드는 내지 않는다. */
        virtual void onOverlapStay( const OverlapInfo& overlap ) { (void)overlap; }
        /**
         * @brief 이 오브젝트의 강체가 다른 바디와 막는 접촉을 시작했습니다(유니티 `OnCollisionEnter` · 언리얼 `OnComponentHit`).
         * @details 물리 스텝들이 끝나고 보간한 자리를 적은 뒤 게임 스레드에서 켜진 컴포넌트마다 불립니다 — 스폰 · 파괴 · 구조 변경을 해도 됩니다.
         *          한 프레임에 스텝이 여럿이면 스텝 순서대로 옵니다.
         */
        virtual void onCollisionBegin( const CollisionInfo& collision ) { (void)collision; }
        /** @brief 막는 접촉이 물리 스텝 하나 동안 이어졌습니다(유니티 `OnCollisionStay`). */
        virtual void onCollisionStay( const CollisionInfo& collision ) { (void)collision; }
        /** @brief 막는 접촉이 끝났습니다(떨어짐 · 바디 사라짐). */
        virtual void onCollisionEnd( const CollisionInfo& collision ) { (void)collision; }
        /**
         * @brief 이 오브젝트가 맞았습니다(근접 판정 · 무기 레이캐스트 — `CharacterHitUtil::deliverHit`). 게임 스레드에서 켜진 컴포넌트마다 불립니다.
         * @details 스폰 · 파괴 · 구조 변경을 해도 됩니다(틱 밖이거나, 틱 중이면 틱 뒤로 미뤄 부릅니다).
         */
        virtual void onHitReceived( const HitInfo& hit ) { (void)hit; }
        /** @brief 이 오브젝트의 애니메이션이 게임플레이 이벤트 알림을 울렸습니다(알림 표의 `GameplayEvent`). 게임 스레드에서 켜진 컴포넌트마다 불립니다. */
        virtual void onAnimNotify( const AnimNotifyInfo& notify ) { (void)notify; }
        /**
         * @brief 소유 GameObject 에 붙은 직후 불립니다.
         * @details 자기가 어떤 등록부에 들어가야 하는지는 자기가 압니다. GameObject 가 `castTo` 로
         *          타입을 골라 대신 등록해 주면, 등록부가 하나 늘 때마다 GameObject 를 고쳐야 하고
         *          GameObject 가 MeshComponent 같은 하위 타입을 알게 됩니다.
         * @param manager 자기가 속한 매니저. 필요한 것(등록부 · 물리 월드 등)을 여기서 꺼내 **들고
         *        있습니다**. 쓸 때마다 소유자를 거슬러 올라가 찾지 않습니다. 그 조회는 소유자가 이미
         *        끊긴 파괴 시점에 엉뚱한 씬을 가리킵니다.
         */
        virtual void onRegister( GameObjectManager& manager ) { (void)manager; }
        /**
         * @brief 소유 GameObject 에서 떨어지기 직전, 등록(`onRegister`) 한 번마다 **정확히 한 번** 불립니다.
         * @details 해체는 `GameObjectManager::destroyComponentInstance` 하나를 지나므로 구현이 멱등일 필요는 없습니다.
         */
        virtual void onUnregister( GameObjectManager& manager ) { (void)manager; }
        /**
         * @brief 소유 GameObject 의 계층 활성(`isActiveInHierarchy`)이 바뀐 직후 불립니다. 값이 그대로면 불리지 않습니다.
         * @details 계층 활성을 따라 무엇을 켜고 끄는 컴포넌트(렌더 프리미티브는 제 칸을 더티로)가 씁니다.
         */
        virtual void onOwnerActiveInHierarchyChanged() {}
        /** @brief 컴포넌트가 파괴될 때 불리는 콜백입니다. */
        virtual void onDestroy();
        /** @brief 프로퍼티가 바뀌었을 때 불리는 콜백입니다. */
        virtual void onPropertyChanged( hashed_string propertyName );
        /**
         * @brief 상태(저장된 PROPERTY)를 읽어 이 컴포넌트를 채운 뒤에 불립니다 — 씬 · 프리팹 로드, 되돌리기, 복제, 핫 리로드(언리얼 `PostLoad` ·
         *        유니티 `OnAfterDeserialize`).
         * @details 값을 자원으로 바꾸는 자리입니다(메시 id → 메시, 머티리얼 참조 → 머티리얼). 플레이 중이 아니어도(편집 중) 불립니다 — 주의: 이
         *          일을 `onBeginPlay` 에 두면 편집 중에 되돌리기 · 프리팹 드래그로 다시 만든 메시가 그려지지 않는다.
         *          비동기 씬 로드에서는 워커 스레드에서 불릴 수 있으니 공유 상태는 잠그는 API 로만 만집니다.
         */
        virtual void onPostLoad() {}
        /**
         * @brief 반사 값을 직렬화기로 **직접** 쓴 뒤 부릅니다 — 프로퍼티마다 `onPropertyChanged` 를, 그다음 `onPostLoad` 를 부릅니다(언리얼
         *        `PostEditChangeProperty` · 유니티 `OnValidate` 의 자리).
         * @details 에디터의 컴포넌트 값 붙여넣기 · 새로 붙여넣기 · 프리셋 · 오버라이드 되돌리기 · 기본값 되돌리기는 값을 `BinarySerializer` ·
         *          `XmlSerializer` · `JsonSerializer` 로 바로 쓰는데, 그 길은 알림을 부르지 않는다 — 이것을 빼면 트랜스폼이 더티가 되지 않고(값은
         *          바뀌었는데 화면에서 움직이지 않는다) 렌더 에셋(메시 · 머티리얼 · 텍스처)을 다시 풀지 않는다. 상태를 통째로 읽는 길
         *          (`ObjectStateSerializer`)은 컴포넌트를 새로 만들고 `onPostLoad` 를 부르므로 이것이 필요 없다.
         */
        void notifyStateWritten();

        /**
         * @brief 서브틱을 등록합니다(TickGroup · Phase · Priority 지정).
         * @details 우선순위는 단계 안의 자리(0..`kMaxTickPriority`)이고, 넘으면 묶습니다(경고). 그룹이 유효하지 않으면 등록하지 않고 빈 핸들을
         *          줍니다.
         *
         *          **서브틱 목록(`_listSubTick`)을 바꾸는 넷(등록 · 해제 · 선행 조건 · 활성)은 틱 중이면 그 변경을 틱 직후로 미룹니다**(구조 변경 큐,
         *          부른 순서대로 — 이어서 부른 선행 조건 추가도 등록 뒤에 돈다). 목록은 이 컴포넌트를 틱하는 워커가 읽는데(64 번부터의 활성 · 자기 틱
         *          안의 등록 · 해제), 다른 오브젝트의 틱이 그것을 늘리면 벡터가 다시 잡혀 그 워커가 해제된 메모리를 읽고 두 워커가 같은 컴포넌트에
         *          등록하면 `push_back` 이 겹친다(형제 `setTickGroup` · `setCanEverTick` 도 같은 이유로 미룬다). 미뤄도 핸들은 바로 줍니다.
         *          실행 여부(서브틱 1~63 은 원자 마스크, 64 번부터는 `SubTickInfo::_bRunnable`)는 틱 중에 바꾸라고 원자다 — 해제 · 끄기는 그것을 바로
         *          내려 이번 틱의 남은 항목이 곧바로 건너뛴다.
         */
        SubTickHandle registerSubTick( TickGroup group, uint32 subTickId, TickPhase phase = TickPhase::Normal, uint8 priority = 0 );
        /** @brief 서브틱 하나의 등록을 해제합니다. 있었으면 true 입니다. 틱 중이면 마스크만 바로 내리고 목록은 틱 직후로 미루며 true(받아 둠)입니다. */
        bool unregisterSubTick( uint32 subTickId );
        /**
         * @brief 서브틱에 선행 조건을 추가합니다(prerequisiteHandle 이 먼저 실행되어야 합니다).
         * @details 선행 조건이 뒤 그룹에 있으면 이 서브틱이 그 그룹으로 옮겨 가 돕니다(언리얼 `ActualStartTickGroup`). 사슬을 따라 옮깁니다.
         *          선행 조건을 가진 서브틱만 스테이지로 가고, 이 컴포넌트의 다른 틱과 사슬 밖 오브젝트는 보통 길 그대로입니다. 앞에서 돈 선행 조건이
         *          쓴 트랜스폼은 이 서브틱이 돌기 전에 적용되어 같은 프레임에 보입니다. 소유자 없이 만든 핸들(`_objectId` 0)은 받지 않습니다(false).
         *          틱 중이면 인자만 보고 틱 직후로 미루며 true(받아 둠)입니다.
         */
        bool addSubTickPrerequisite( uint32 subTickId, const SubTickHandle& prerequisiteHandle );
        /** @brief 선행 조건 하나를 뗍니다. 있었으면 true 입니다. 틱 중이면 틱 직후로 미루며 true(받아 둠)입니다 — 대상이 바뀌면 갈아 걸 때 씁니다. */
        [[nodiscard]] bool removeSubTickPrerequisite( uint32 subTickId, const SubTickHandle& prerequisiteHandle );
        /**
         * @brief 이 컴포넌트의 주 틱(`onTick`)을 가리키는 핸들입니다. 다른 컴포넌트의 서브틱 선행 조건으로 겁니다(언리얼 `AddTickPrerequisiteComponent` 의 대상).
         * @details 오브젝트에 붙은 뒤에 얻으십시오(소유 오브젝트 id 가 든다). 그 컴포넌트가 틱하지 않거나 꺼져 있으면 순서를 만들지 않습니다.
         */
        SubTickHandle getTickHandle() const { return makeTickHandle( 0 ); }
        /** @brief 서브틱의 활성 여부를 설정합니다. 틱 중이면 마스크(1~63)만 바로 바꾸고 목록의 값은 틱 직후로 미룹니다. */
        void setSubTickActive( uint32 subTickId, bool bActive );
        /** @brief 서브틱이 활성 상태인지 확인합니다(비트마스크로 O(1)). */
        bool isSubTickActive( uint32 subTickId ) const
        {
            if ( subTickId == 0 )
                return false;
            if ( subTickId < 64 )
                return ( _subTickActiveMask.load( std::memory_order_relaxed ) & ( 1ULL << subTickId ) ) != 0;
            return isSubTickActiveSlow( subTickId );
        }
        /** @brief 등록된 모든 서브틱 목록을 반환합니다. */
        const vector<SubTickInfo>& getAllSubTicks() const { return _listSubTick; }

        /** @brief 구체 타입의 TypeInfo 로 gamesettings 기본값을 주입합니다. */
        void applyTypeDefaults( const TypeInfo* pTypeInfo );
        /** @brief 소유자 GameObject 를 설정합니다. */
        void setOwner( GameObject* pOwner ) { _pOwner = pOwner; }
        /** @brief 컴포넌트를 켜거나 끕니다. */
        void setActive( bool bActive );
        /** @brief 틱 그룹을 바꿉니다. 유효하지 않은 그룹(`isValidTickGroup`)은 경고하고 무시합니다. */
        void setTickGroup( TickGroup group );
        /**
         * @brief 주 틱에 들어갈지 설정합니다. 생성자에서 끄면(언리얼 `bCanEverTick = false`) 끈 것이 이깁니다.
         * @details **기본은 "`onTick` 을 오버라이드했는가"** 입니다(유니티: `Update` 가 있으면 부른다) — `GameObject::addComponent` 가
         *          `HasOnTickOverride_v` 로 판정해, 오버라이드하지 않은 타입은 끕니다. `Component` · `SceneComponent` 모두 같은 규칙입니다.
         */
        void setCanEverTick( bool bCanEverTick );
        /**
         * @brief 컴포넌트의 이름표를 설정합니다(기본은 타입 이름). **타입이 아닙니다.** 상태와 함께 저장됩니다.
         * @details 오브젝트 안에서 컴포넌트를 가리키는 키(`ComponentStableKey` — 부착 대상 · 프리팹 오버라이드 · 에디터 선택 복원)가 이 이름으로
         *          셉니다. 언리얼의 컴포넌트 이름이 참조 · 오버라이드의 키인 것과 같은 자리입니다. 이름표가 없는 상태는 기본값(타입 이름)으로
         *          읽힙니다. 타입은 만들 때 받은 `TypeInfo`(`_pTypeInfo`)이고 이름과 무관합니다 — 타입 이름이 필요하면 `getTypeName`.
         * @note 풀 키도 **아닙니다.** 파괴는 `_pPool` 로 돌아갑니다. 이름을 바꾸면 그 컴포넌트를 키로 가리키던 저장된 참조(다른 컴포넌트의 부착 ·
         *       프리팹 오버라이드)는 다음 로드에서 가리킬 곳을 잃습니다 — 언리얼이 컴포넌트 이름을 바꿀 때와 같습니다.
         */
        void setComponentName( hashed_string name ) { _componentName = name; }

        /** @brief 이 인스턴스의 컴포넌트 핸들을 반환합니다. */
        sw::ComponentHandle getHandle() const;

        /**
         * @brief 런타임 타입 리플렉션 정보(TypeInfo)입니다 — 만들 때 받은 타입이고, 그 타입이 해제됐으면 nullptr 입니다.
         * @details 이름표(`_componentName`)와 무관합니다 — `setComponentName` 으로 이름을 바꿔도 타입은 그대로입니다. TypeInfo 의 주소는
         *          고정이라(`TypeRegistry`) 포인터 하나를 들면 됩니다.
         */
        virtual const TypeInfo* getTypeInfo() const;
        /** @brief 타입 이름입니다(타입이 없으면 이름표). 복사 · 붙여넣기 · 프리셋 · 타입 필터처럼 "무슨 타입인가" 를 묻는 자리가 씁니다. */
        hashed_string getTypeName() const;
        /**
         * @brief 만들 때 받은 TypeInfo 를(살아 있으면), 아니면 가상 `getTypeInfo()` 의 답을 반환합니다.
         * @details `castTo` 의 핫패스입니다. 적중이면 가상 호출 없이 포인터 하나와 원자 로드 하나로 끝납니다. 테스트 목처럼
         *          `getTypeInfo()` 를 오버라이드하는 타입도 `addComponent<T>` 가 같은 `T::StaticType()` 을 넘겨 같은 답입니다.
         */
        const TypeInfo* findCachedTypeInfo() const
        {
            if ( _pTypeInfo != nullptr && _pTypeInfo->isAlive() )
                return _pTypeInfo;
            return getTypeInfo();
        }
        /** @brief 소유자 GameObject 를 반환합니다. */
        GameObject* getOwner() const { return _pOwner; }
        /** @brief 활성 상태인지 확인합니다(자기 활성 비트와 소유자 활성을 모두 봅니다). */
        bool isActive() const;
        /** @brief 자기 활성 비트만 봅니다. 소유자 활성을 이미 확인한 자리(오브젝트 단위 틱 디스패치)에서 씁니다. */
        bool isSelfActive() const { return _bActive.load( std::memory_order_relaxed ); }
        /** @brief 현재 틱 그룹을 반환합니다. */
        TickGroup getTickGroup() const { return static_cast<TickGroup>( _tickGroup ); }
        /** @brief 주 틱에 참여하면 true 입니다. */
        bool canEverTick() const { return _bCanEverTick == SW_TRUE; }
        /** @brief 틱에 참여할 일이 있는지 반환합니다. 주 틱이 켜졌거나 서브틱이 하나라도 등록되어 있으면 true 이고, 소유 오브젝트의 틱 항목을 다시 지을지 정하는 기준입니다. */
        bool hasTickWork() const { return canEverTick() || _listSubTick.empty() == false; }
        /**
         * @brief 씬 컴포넌트(트랜스폼을 가진 것)인지 리플렉션 없이 답합니다.
         * @details `castTo<SceneComponent>` 는 가상 호출 + 캐시 조회 둘 + 조상 표 비교입니다(약 8 ns). 워커 열넷이
         *          건마다 그것을 부르자 배치 트랜스폼 쓰기가 세터보다 **느려졌습니다**(사슬을 걷던 때 8000 건 2.1 ms).
         *          생성자에서 세우는 비트 하나면 됩니다.
         */
        bool isSceneComponent() const { return _bIsSceneComponent == SW_TRUE; }

        /** @brief 컴포넌트 고유 ID 를 반환합니다. */
        uint64 getComponentId() const { return _componentId; }

        /** @brief 삭제 예정(묘비) 표시를 세웁니다. */
        void markPendingDestroy() { (void)tryMarkPendingDestroy(); } // 이미 표시돼 있어도 된다

        /**
         * @brief 삭제 예정 표시를 **이 호출이 처음으로 세웠는지** 반환합니다.
         * @details `isPendingDestroy()` 로 보고 나서 `markPendingDestroy()` 하는 두 걸음은 원자적이지
         *          않습니다. 두 스레드가 그 사이를 나란히 통과하면 파괴 목록에 같은 포인터가 **두 번**
         *          들어가고, 풀이 같은 블록을 두 번 반납합니다. `onTick` 은 병렬로 돌기 때문에
         *          (총알 둘이 같은 적을 같은 프레임에 맞히는) 흔한 경우입니다. 없애는 쪽은 반드시
         *          이 함수가 `true` 를 준 스레드 **하나만** 진행해야 합니다.
         */
        [[nodiscard]] bool tryMarkPendingDestroy() { return _bIsPendingDestroy.exchange( true, std::memory_order_acq_rel ) == false; }

        /** @brief 삭제 예정인지 확인합니다. */
        bool isPendingDestroy() const { return _bIsPendingDestroy.load( std::memory_order_acquire ); }
        /** @brief 컴포넌트 이름(해시)을 반환합니다. */
        hashed_string getComponentName() const { return _componentName; }

    private:
        bool isSubTickActiveSlow( uint32 subTickId ) const;
        /** @brief 이 컴포넌트의 틱 하나(@p subTickId, 0 이면 주 틱)를 가리키는 핸들입니다. 소유 오브젝트 id 를 함께 담습니다(없으면 0). */
        SubTickHandle makeTickHandle( uint32 subTickId ) const;
        /** @brief 서브틱의 실행 여부를 바로 바꿉니다 — 1~63 은 원자 마스크, 64 번부터는 목록 원소의 원자 칸입니다. 틱 중에도 부를 수 있습니다. */
        void setSubTickRunnable( uint32 subTickId, bool bRunnable );
        /**
         * @brief 소유 매니저가 구조 변경을 얼려 두었으면(컴포넌트 틱 중) @p func 를 틱 직후 구조 변경 큐로 미루고 true 를 돌려줍니다. 아니면 false 입니다.
         * @details 핸들로 다시 찾으므로 그 사이 파괴돼도 안전합니다. 틱 설정(그룹 · 틱 여부 · 서브틱)이 `GameObject` 의 setName · addTag 와 같은 규칙을 지킵니다.
         */
        bool                  deferIfStructureFrozen( Delegate<void( Component& )> func );
        static atomic<uint64> _s_nextComponentId; ///< ID 생성 카운터

    protected:
        GameObject* _pOwner;      ///< 소유자 GameObject
        uint64      _componentId; ///< 컴포넌트 고유 일련번호
        /** @brief 이름표(기본은 타입 이름)입니다. 저장됩니다 — 컴포넌트 키(`ComponentStableKey`)가 이것으로 셉니다. 타입이 아니다 — `setComponentName` */
        PROPERTY( HideInInspector )
        hashed_string   _componentName;
        const TypeInfo* _pTypeInfo; ///< 만들 때 받은 타입. `GameObject::attachCreatedComponent` 가 한 번 적는다(공개 전이라 원자가 아니다)
        /**
         * @brief 이 인스턴스를 내준 풀입니다. 힙에서 왔으면 nullptr 입니다. 생성이 한 번 적고 파괴가 읽습니다.
         * @details 주의: 파괴할 때 타입 이름으로 풀을 다시 찾지 말 것 — 이름이 바뀌었거나(`setComponentName`) 그 타입이 그새 해제되었으면
         *          풀을 찾지 못해 풀 블록을 힙으로 반납하고, Shipping 에서 힙이 깨진다(0xc0000374. Debug · ASan 은 조용하다).
         */
        PoolAllocator* _pPool;

        atomic<uint64> _subTickActiveMask; ///< 서브틱 1~63 의 활성 상태(원자 비트마스크, O(1))
        /**
         * @brief 컴포넌트 자기 활성 비트입니다. 저장됩니다(PROPERTY) — 스냅샷이 이 값을 실어야 끈 컴포넌트가 Stop · 되돌리기 · 씬 다시 열기 뒤에도
         *        꺼진 채로 남고 토글이 되돌리기에 남는다. 인스펙터는 컴포넌트 머리의 체크박스로 그린다.
         */
        PROPERTY( HideInInspector )
        atomic<bool>        _bActive;
        atomic<bool>        _bIsPendingDestroy; ///< 삭제 예정 표시
        TickGroup           _tickGroup;         ///< TickGroup 슬롯
        uint8               _bCanEverTick      : 1;
        uint8               _bIsSceneComponent : 1; ///< SceneComponent 생성자가 세웁니다
        uint8               _bHasBegunPlay     : 1; ///< onBeginPlay 가 불렸고 onEndPlay 는 아직(`dispatchBeginPlay` · `dispatchEndPlay` 만 만집니다)
        uint8               _reservedFlags     : 5;
        vector<SubTickInfo> _listSubTick; ///< 등록된 보조 서브틱 목록
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 타입 T(또는 그 조상 중 `Component` 가 아닌 것)가 `onTick` 을 오버라이드했는지입니다. 주 틱의 기본값이 이것입니다.
     * @details `&T::onTick` 의 타입이 `Component` 의 것이면 아무도 오버라이드하지 않았습니다. 볼 수 없으면(보호 · 비공개 오버라이드라
     *          접근이 막히면) 오버라이드한 것으로 칩니다 — 그때는 생성자의 값을 그대로 둡니다.
     */
    template <typename T, typename = void>
    struct HasOnTickOverride : std::true_type
    {
    };

    template <typename T>
    struct HasOnTickOverride<T, std::void_t<decltype( &T::onTick )>>
        : std::bool_constant<std::is_same_v<decltype( &T::onTick ), void ( Component::* )( float32 )> == false>
    {
    };

    template <typename T>
    inline constexpr bool HasOnTickOverride_v = HasOnTickOverride<T>::value;
} // namespace sw
