/**
 * @file Component.h
 * @brief GameObject 에 붙는 컴포넌트의 기반 클래스와 TickGroup, TickPhase 정의입니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Foundation/AnimationNotifyPhase.h"
#include "Engine/Foundation/PhysicsHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{

    struct ComponentPoolEntry;

    class GameObject;
    class GameObjectManager;
    class StateHashBuilder;
    /**
     * @enum TickGroup
     * @brief 한 프레임 안에서 컴포넌트 틱이 도는 순서 슬롯입니다.
     * @details 이름대로 물리 파이프라인 단계(언리얼의 ETickingGroup)를 뜻한다. 물리는 **DuringPhysics 와 PostPhysics 사이**에 한 번 step 한다.
     *          프레임 안에서 어느 단계가 무엇을 보는지는 프레임 단계 목록 `Engine/Object/GameObject/Tick/SceneFrameStepList.xxx` 한 곳에 있다.
     */
    enum class TickGroup : uint8
    {
        PrePhysics,    ///< 이른 업데이트 단계
        DuringPhysics, ///< 기본 tick 단계
        PostPhysics,   ///< 늦은 업데이트 단계
        PostUpdate,    ///< 렌더 직전 등 최종 단계
    };

    /** @brief `TickGroup` 이 유효한 값(PrePhysics..PostUpdate)이면 true 입니다. 정수에서 캐스트한 값은 범위를 넘을 수 있습니다. */
    constexpr bool isValidTickGroup( TickGroup group )
    {
        return static_cast<uint32>( group ) <= static_cast<uint32>( TickGroup::PostUpdate );
    }

    /**
     * @enum TickPhase
     * @brief 같은 TickGroup 안의 세부 실행 단계입니다. 단계는 64 간격이고, 서브틱 우선순위(0..`kMaxTickPriority`)는 한 단계 안에서의 순서입니다.
     * @details **한 오브젝트 안의** 컴포넌트 틱 순서를 정하는 수단이다(UE 의 컴포넌트 틱 순서 · Unity 의 Script Execution Order 자리). 같은 프레임, 같은 그룹 안에서 (단계, 우선순위) 순으로 돈다.
     *          **오브젝트 사이** 순서는 그룹(`TickGroup`)이나 서브틱 선행 조건(`addSubTickPrerequisite`)으로 정한다.
     *          예: 래그돌 합성이 애니메이션 평가 뒤에 와야 하는 컴포넌트는 `registerSubTick( TickGroup::PrePhysics, id, TickPhase::Late )` 로 등록한다.
     */
    enum class TickPhase : uint8
    {
        Early    = 0,   ///< 선행 연산 (데이터 준비, 물리 전처리)
        Normal   = 64,  ///< 기본 연산 (일반 게임플레이)
        Late     = 128, ///< 후행 연산 (래그돌 합성, 소켓 어태치먼트)
        Finalize = 192  ///< 최종 연산 (GPU 버퍼 업로드, LOD 계산)
    };

    /** @brief 서브틱 우선순위의 상한입니다. 넘으면 이 값으로 제한하며, 단계(64 간격)를 넘어 다음 단계로 가지 않습니다. */
    inline constexpr uint8 kMaxTickPriority = 63;

    /**
     * @struct SubTickHandle
     * @brief 컴포넌트의 틱 하나를 식별하는 핸들이며, 선행 조건(prerequisite)을 연결할 때 씁니다. `_subTickID` 가 0 이면 주 틱 `onTick` 입니다.
     */
    struct SubTickHandle
    {
        uint64 _componentID{ 0 };
        uint32 _subTickID{ 0 };
        /**
         * @brief 그 컴포넌트를 가진 오브젝트입니다. 비교와 해시에는 쓰지 않습니다.
         * @details 레지스트리가 선행 조건이 가리키는 항목을 씬 전체에서 찾지 않고 이 오브젝트에서만 찾는 데 쓴다. 0 이면(소유자 없이 만든 핸들)
         *          선행 조건으로 받지 않는다.
         */
        uint64 _objectID{ 0 };

        constexpr bool isValid() const
        {
            return _componentID != 0;
        }

        constexpr bool operator==( const SubTickHandle& other ) const
        {
            return _componentID == other._componentID && _subTickID == other._subTickID;
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
            return static_cast<size_t>( handle._componentID ^ ( static_cast<uint64>( handle._subTickID ) << 32 ) );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 부모에 붙이거나 뗄 때 무엇을 유지할지 정합니다(언리얼의 `EAttachmentRule`, 유니티의 `SetParent( parent, worldPositionStays )`).
     * @details `KeepRelative` 는 로컬 값을 그대로 두므로 새 부모를 따라 월드 위치가 바뀐다. 상태 로드와 코드의 기본값이며, 저장된 로컬은 부모 기준이기 때문이다.
     *          `KeepWorld` 는 월드 위치를 유지하도록 로컬 값을 다시 계산한다. 에디터에서 부모를 바꾸거나 떼어 낼 때 쓴다.
     *          이때 `KeepRelative` 로 부모를 바꾸면 계층 창에서 끌어 놓은 오브젝트가 새 부모만큼 **튄다**.
     */
    enum class AttachRule : uint8
    {
        KeepRelative,
        KeepWorld
    };

    /**
     * @struct SubTickInfo
     * @brief 컴포넌트에 등록된 서브틱 하나의 메타데이터와 선행 조건 정보입니다.
     * @details 활성 플래그는 둘이다. `_bActive` 는 틱 레지스트리가 읽는 값이라 틱 중이면 틱이 끝난 뒤에 바뀐다.
     *          `_bRunnable` 은 실행 직전에 보는 값이라 틱 중에도 바로 바뀐다. 다른 워커가 쓰므로 원자값이다.
     *          틱 동안 목록의 구조는 바뀌지 않으므로(구조 변경은 미룬다) 원소를 가리키는 포인터를 써도 된다.
     *          서브틱 1~63 은 컴포넌트의 원자 마스크가 같은 일을 O(1) 로 처리한다. `_bRunnable` 은 64 번부터의 원본이다.
     */
    struct SubTickInfo
    {
        uint32                _subTickID;
        TickGroup             _group;
        TickPhase             _phase;
        uint8                 _priority;
        uint8                 _bActive;
        atomic<uint8>         _bRunnable;
        vector<SubTickHandle> _listPrerequisite;

        SubTickInfo()
            : _subTickID{ 0 }
            , _group{ TickGroup::DuringPhysics }
            , _phase{ TickPhase::Normal }
            , _priority{ 0 }
            , _bActive{ SW_TRUE }
            , _bRunnable{ SW_TRUE }
            , _listPrerequisite{}
        {
        }

        SubTickInfo( const SubTickInfo& other )
            : _subTickID{ other._subTickID }
            , _group{ other._group }
            , _phase{ other._phase }
            , _priority{ other._priority }
            , _bActive{ other._bActive }
            , _bRunnable{ other._bRunnable.load( std::memory_order_relaxed ) }
            , _listPrerequisite{ other._listPrerequisite }
        {
        }

        SubTickInfo( SubTickInfo&& other ) noexcept
            : _subTickID{ other._subTickID }
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
            _subTickID = other._subTickID;
            _group     = other._group;
            _phase     = other._phase;
            _priority  = other._priority;
            _bActive   = other._bActive;
            _bRunnable.store( other._bRunnable.load( std::memory_order_relaxed ), std::memory_order_relaxed );
            _listPrerequisite = std::move( other._listPrerequisite );
            return *this;
        }

        /** @brief 지금 실행해도 되면 true 입니다. 틱 중에 다른 스레드가 바꿀 수 있습니다. */
        bool isRunnable() const { return _bRunnable.load( std::memory_order_acquire ) == SW_TRUE; }
        /** @brief 실행 여부를 바로 바꿉니다. 틱 중에도 부를 수 있습니다. */
        void setRunnable( bool bRunnable ) { _bRunnable.store( bRunnable ? SW_TRUE : SW_FALSE, std::memory_order_release ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct OverlapInfo
     * @brief 겹침 시작과 끝 이벤트 하나의 정보입니다. 상대 오브젝트, 어느 콜라이더끼리 겹쳤는지(트리거 여부), 닿은 시점을 담습니다(언리얼 `OnComponentBeginOverlap` 의 인자).
     * @details 겹침은 바디(콜라이더) 쌍마다 발생한다. 오브젝트 하나에 막는 콜라이더와 트리거(감지 범위)가 함께 있으면 같은 상대에게서 이벤트 둘이 올 수 있으므로,
     *          받는 쪽은 트리거 여부로 구분한다. 예를 들어 투사체는 상대의 트리거에 막히지도 피해를 주지도 않는다.
     */
    struct OverlapInfo
    {
        GameObject*            _pOther{ nullptr };   ///< 상대 오브젝트. 끝 이벤트에서 상대가 이미 사라졌으면 nullptr
        PhysicsBodyHandle      _selfBody{};          ///< 이 오브젝트 쪽 바디다(트리거 · 2D 콜라이더의 센서). 히트 존이나 래그돌 뼈를 구분한다
        PhysicsBodyHandle      _otherBody{};         ///< 강체 물리의 트리거면 상대 쪽 바디
        float32                _hitFraction{ 1.0f }; ///< 이번 물리 step 안에서 닿은 시점(0..1)이다. 연속 바디가 쓸려 가며 닿은 시작 이벤트만 1 보다 작다
        uint8                  _bSelfTrigger  : 1;   ///< 이 오브젝트 쪽 콜라이더가 트리거인지
        uint8                  _bOtherTrigger : 1;   ///< 상대 쪽 콜라이더가 트리거인지
        [[maybe_unused]] uint8 _reserved      : 6;

        /** @brief 상대가 없고 막는 콜라이더끼리 겹친 상태로 만듭니다. */
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
     * @brief 강체 물리의 막는 접촉 하나의 정보입니다(유니티의 `Collision`, 언리얼의 `FHitResult` 에 해당). 2D 접촉은 Z = 0 입니다.
     * @details 접촉은 바디 쌍마다 발생한다. 래그돌이나 컴파운드처럼 한 오브젝트에 바디가 여럿이면 `_selfBody` 로 어느 바디(뼈, 파편)인지 구분한다.
     *          `_impulse` 는 충격량의 크기(뉴턴초)다. 시작 이벤트는 부딪힌 충격, 유지 이벤트는 그 스텝의 충격량, 끝 이벤트는 0 이며, 피해, 파괴, 소리 세기에 쓴다.
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
     * @brief 이 오브젝트가 맞았을 때의 정보입니다. 근접 판정(애니메이션 알림의 칼 궤적)이나 무기 레이캐스트가 만듭니다(언리얼의 `FHitResult` 와 `TakeDamage` 인자에 해당).
     * @details 엔진은 체력을 모른다. 피해(`_damage` × `_damageMultiplier`)를 어떻게 쓸지는 받는 컴포넌트(게임의 체력, 래그돌의 움찔)가 정한다.
     *          히트 존은 맞은 바디로 고른다(래그돌과 히트박스는 물리 에셋, 강체는 히트 존 속성). 2D 에서 맞으면 Z = 0 이다.
     */
    struct HitInfo
    {
        GameObject*       _pInstigator{ nullptr };   ///< 때린 오브젝트(없으면 nullptr)
        PhysicsBodyHandle _body{};                   ///< 맞은 바디
        hashed_string     _zone{};                   ///< 히트 존 이름(없으면 빈 이름)
        hashed_string     _kind{};                   ///< 때린 것의 이름이다(알림 이름이나 무기 종류)
        float3            _point{};                  ///< 맞은 점(월드)
        float3            _normal{};                 ///< 맞은 면의 법선(월드)
        float3            _direction{};              ///< 때린 방향(월드, 단위)
        float32           _damage{ 0.0f };           ///< 기본 피해
        float32           _damageMultiplier{ 1.0f }; ///< 히트 존 배율
        float32           _impulse{ 0.0f };          ///< 맞은 바디에 줄 충격량 크기(뉴턴초)
        int32             _bodyIndex{ -1 };          ///< 래그돌이나 히트박스의 바디 번호(물리 에셋 순서). 없으면 -1
        bool              _bIs2D{ false };           ///< 2D 물리 씬에서 맞았으면 true
        bool              _bFatal{ false };          ///< 때린 쪽이 이 타격으로 죽는다고 판정했다는 뜻이다(절단과 래그돌 전환의 신호)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationNotifyInfo
     * @brief 애니메이션 알림이 이 오브젝트의 컴포넌트에 보내는 게임플레이 이벤트입니다(알림 테이블의 `AbilityEvent` 처리기이며, 언리얼 AnimationNotify 의 이벤트에 해당).
     */
    struct AnimationNotifyInfo
    {
        hashed_string        _notify{};                               ///< 클립의 알림 이름
        hashed_string        _event{};                                ///< 알림 테이블이 정한 이벤트 이름
        float32              _weight{ 1.0f };                         ///< 알림을 울린 클립의 블렌딩 가중치
        AnimationNotifyPhase _phase{ AnimationNotifyPhase::Instant }; ///< 구간 알림이면 시작인지 끝인지 나타낸다
    };
} // namespace sw

namespace sw
{
    /**
     * @class Component
     * @brief GameObject 에 기능과 데이터를 덧붙이는 컴포넌트의 기반 클래스입니다.
     * @details 리플렉션에는 **만들 수 없는 기반**(`Abstract`)으로 등록한다. 팩토리가 없어 이름으로 붙일 수 없다.
     *          등록하지 않으면 모든 컴포넌트의 부모 체인이 여기서 끊긴다(`SceneComponent : Component` 의 부모가 "없음"이 된다).
     */
    REFLECT( Abstract, Category = "Core", DisplayName = "Component", Tooltip = "Base of every component" )
    class SW_API Component
    {
        friend class GameObject;
        friend class GameObjectStore; ///< 해체(`destroyComponentInstance`)가 `_pPoolEntry` 의 풀로 반납하고 버킷에서 뺀다

    public:
        REFLECT_BODY();

        /** @brief 버킷 위치(`_poolEntrySlot`)의 "버킷에 없음" 값입니다. */
        static constexpr uint32 kNotInPoolEntry = 0xFFFFFFFFu;

        /** @brief 기본 컴포넌트를 만듭니다. */
        Component();

        /** @brief 복사를 금지합니다. */
        Component( const Component& ) = delete;
        /** @brief 대입을 금지합니다. */
        Component& operator=( const Component& ) = delete;

        /**
         * @brief **옮기지 않습니다.** 컴포넌트는 풀 안의 제자리에서 만들고 없앱니다.
         * @details 컴포넌트를 옮기는 코드는 없다. 옮기면 원본과 사본이 같은 `_componentID` 를 가지고, `findComponentByID` 가
         *          어느 쪽이든 반환할 수 있으므로 막는다. 파생 타입도 이동 연산을 선언하지 않는다.
         * @see SceneComponent 도 계층 포인터가 얽혀 있어 같은 이유로 막혀 있다.
         */
        Component( Component&& other )            = delete;
        Component& operator=( Component&& other ) = delete;

        /** @brief 가상 소멸자입니다. 해체는 GameObjectManager::destroyComponentInstance 가 맡으며, 해제 콜백 호출, 소멸, 풀 반납 순서로 진행합니다. */
        virtual ~Component() = default;

        /**
         * @brief 게임플레이가 시작될 때 불리는 초기화 콜백입니다. 인스턴스마다 **정확히 한 번** 불리며 `onEndPlay` 와 짝을 이룹니다.
         * @details 월드가 플레이 중이면(에디터 Play, 에디터 없는 App, Shipping 모두 `SceneManager::setWorldPlaying` 으로 판정한다) 활성 씬의 컴포넌트는
         *          플레이가 시작될 때 불리고, 플레이 중에 붙은 컴포넌트는 **다음 틱 단계**에서 불린다. 붙인 직후에 설정한 필드와 상태 로드가 채운
         *          PROPERTY 를 볼 수 있다. 활성 여부와 무관하게 불린다(언리얼과 같다). 되돌리기나 핫 리로드로 다시 만든 인스턴스는 새 인스턴스이므로
         *          다시 불린다. 런타임에만 있는 상태는 여기서 만든다. 직접 부르지 말고 `dispatchBeginPlay` 를 쓴다.
         */
        virtual void onBeginPlay();
        /** @brief 게임플레이가 끝날 때(플레이 종료, 컴포넌트 해체) 불리는 정리 콜백입니다. `onBeginPlay` 가 불린 인스턴스에만 한 번 불립니다. */
        virtual void onEndPlay();
        /**
         * @brief 아직 시작하지 않았으면 `onBeginPlay` 를 부르고 "시작됨"으로 기록합니다. 삭제 대기 중이면 부르지 않습니다.
         * @details 게임 스레드에서, 병렬 틱 밖에서만 부른다. "시작됨" 비트가 틱 워커가 읽는 비트(`_bCanEverTick`)와 같은 바이트에 있기 때문이다.
         */
        void dispatchBeginPlay();
        /** @brief 시작했으면 `onEndPlay` 를 부르고 "시작됨"을 지웁니다. 시작한 적이 없으면 아무것도 하지 않습니다. */
        void dispatchEndPlay();
        /** @brief `onBeginPlay` 가 불렸고 아직 `onEndPlay` 가 불리지 않았으면 true 입니다. */
        bool hasBegunPlay() const { return _bHasBegunPlay == SW_TRUE; }
        /** @brief 프레임마다 불리는 주 업데이트 콜백입니다. */
        virtual void onTick( float32 deltaTime );
        /** @brief 프레임마다 서브틱별로 불리는 보조 업데이트 콜백입니다. */
        virtual void onSubTick( uint32 subTickID, float32 deltaTime );
        /**
         * @brief 이 오브젝트의 콜라이더가 다른 오브젝트의 콜라이더와 겹치기 시작했을 때 불립니다(유니티의 `OnTriggerEnter2D`, 언리얼의 `BeginOverlap`).
         * @details 틱과 트랜스폼 적용 뒤에 게임 스레드에서 오브젝트의 켜진 컴포넌트마다 불린다. 이 안에서 스폰, 파괴, 구조 변경을 해도 된다.
         *          한 step 의 겹침 이벤트는 닿은 시점(`OverlapInfo::_hitFraction`) 순서로 온다.
         */
        virtual void onOverlapBegin( const OverlapInfo& overlap ) { (void)overlap; }
        /** @brief 겹침이 끝났을 때 불립니다(떨어짐, 꺼짐, 사라짐). 상대가 이미 사라졌으면 `OverlapInfo::_pOther` 는 nullptr 입니다. */
        virtual void onOverlapEnd( const OverlapInfo& overlap ) { (void)overlap; }
        /** @brief 트리거와 겹친 채로 물리 스텝 하나가 지났을 때 불립니다(유니티의 `OnTriggerStay`). 물리 스텝의 겹침(트리거 · 2D 콜라이더)이 발생시키고, 연속 콜라이더가 쓸어 낸 겹침은 발생시키지 않습니다. */
        virtual void onOverlapStay( const OverlapInfo& overlap ) { (void)overlap; }
        /**
         * @brief 이 오브젝트의 강체가 다른 바디와 막는 접촉을 시작했을 때 불립니다(유니티의 `OnCollisionEnter`, 언리얼의 `OnComponentHit`).
         * @details 물리 스텝이 끝나고 보간한 위치를 기록한 뒤 게임 스레드에서 켜진 컴포넌트마다 불린다. 스폰, 파괴, 구조 변경을 해도 된다.
         *          한 프레임에 스텝이 여러 개이면 스텝 순서대로 온다.
         */
        virtual void onCollisionBegin( const CollisionInfo& collision ) { (void)collision; }
        /** @brief 막는 접촉이 물리 스텝 하나 동안 이어졌을 때 불립니다(유니티의 `OnCollisionStay`). */
        virtual void onCollisionStay( const CollisionInfo& collision ) { (void)collision; }
        /** @brief 막는 접촉이 끝났을 때 불립니다(떨어짐, 바디 사라짐). */
        virtual void onCollisionEnd( const CollisionInfo& collision ) { (void)collision; }
        /**
         * @brief 이 오브젝트가 맞았을 때 불립니다. 근접 판정이나 무기 레이캐스트가 `CharacterHitUtil::deliverHit` 로 전달하며, 게임 스레드에서 켜진 컴포넌트마다 불립니다.
         * @details 스폰, 파괴, 구조 변경을 해도 된다. 틱 밖이면 바로, 틱 중이면 틱이 끝난 뒤로 미뤄서 부른다.
         */
        virtual void onHitReceived( const HitInfo& hit ) { (void)hit; }
        /** @brief 이 오브젝트의 애니메이션이 게임플레이 이벤트 알림을 울렸을 때 불립니다(알림 테이블의 `AbilityEvent`). 게임 스레드에서 켜진 컴포넌트마다 불립니다. */
        virtual void onAnimationNotify( const AnimationNotifyInfo& notify ) { (void)notify; }
        /**
         * @brief 소유 GameObject 에 붙은 직후 불립니다.
         * @details 컴포넌트가 어느 레지스트리에 들어가야 하는지는 컴포넌트 자신이 안다. GameObject 가 `castTo` 로
         *          타입을 골라 대신 등록해 주면, 레지스트리가 하나 늘 때마다 GameObject 를 고쳐야 하고
         *          GameObject 가 MeshComponent 같은 하위 타입을 알게 된다.
         * @param manager 자기가 속한 매니저. 필요한 것(레지스트리, 물리 월드 등)을 여기서 꺼내 **보관한다**.
         *        쓸 때마다 소유자를 거슬러 올라가 찾지 않는다. 소유자와의 연결이 이미 끊긴
         *        파괴 시점에는 그 조회가 엉뚱한 씬을 가리키기 때문이다.
         */
        virtual void onRegister( GameObjectManager& manager ) { (void)manager; }
        /**
         * @brief 소유 GameObject 에서 떨어지기 직전에 불립니다. 등록(`onRegister`) 한 번마다 **정확히 한 번** 불립니다.
         * @details 해체는 `GameObjectManager::destroyComponentInstance` 하나를 거치므로 구현이 멱등일 필요는 없다.
         */
        virtual void onUnregister( GameObjectManager& manager ) { (void)manager; }
        /**
         * @brief 소유 GameObject 의 계층 활성(`isActiveInHierarchy`)이 바뀐 직후 불립니다. 값이 그대로면 불리지 않습니다.
         * @details 계층 활성에 따라 무언가를 켜고 끄는 컴포넌트가 쓴다. 예를 들어 렌더 프리미티브는 자기 슬롯을 더티로 표시한다.
         */
        virtual void onOwnerActiveInHierarchyChanged() {}
        /** @brief 컴포넌트가 파괴될 때 불리는 콜백입니다. */
        virtual void onDestroy();
        /** @brief 프로퍼티가 바뀌었을 때 불리는 콜백입니다. */
        virtual void onPropertyChanged( hashed_string propertyName );
        /**
         * @brief 상태(저장된 PROPERTY)를 읽어 이 컴포넌트를 채운 직후에 불립니다. 씬과 프리팹 로드, 되돌리기, 복제, 핫 리로드에서 불립니다(언리얼의 `PostLoad`,
         *        유니티의 `OnAfterDeserialize`).
         * @details 저장된 값을 리소스로 바꾸는 곳이다(메시 id → 메시, 머티리얼 참조 → 머티리얼). 플레이 중이 아니어도(편집 중) 불린다.
         *          이 일을 `onBeginPlay` 에 두면 편집 중에 되돌리기나 프리팹 드래그로 다시 만든 메시가 그려지지 않는다.
         *          비동기 씬 로드에서는 워커 스레드에서 불릴 수 있으므로 공유 상태는 잠금을 지원하는 API 로만 만진다.
         */
        virtual void onPostLoad() {}
        /**
         * @brief 모듈 @p moduleName 을 내리기 직전(그 코드가 아직 있을 때)에 불립니다. 그 모듈 **타입**(`TypeInfo` 의 모듈 이름)의 객체를 든 컴포넌트가 그것을 놓는다.
         * @details 컴포넌트 자신이 그 모듈 타입이면 이 뒤에 지워진다(`destroyComponentsOfModule`) — 여기서 할 일은 엔진 · 키트 컴포넌트가 든 남의 모듈 객체(어빌리티 · 세트)다.
         *          모듈 언로드는 개발 빌드만 하고 게임 스레드에서 부른다(`engine::unregisterModuleTypes`). 코드 주소로 고르지 않는다(언리얼은 클래스의 패키지로 다시 만든다).
         */
        virtual void onModuleUnloading( string_view moduleName ) { (void)moduleName; }
        /**
         * @brief 씬 상태 해시(`SceneStateHash`)에 이 컴포넌트의 시뮬레이션 상태를 접습니다. 기본은 아무것도 넣지 않습니다(렌더 · 연출 컴포넌트).
         * @details 두 프로세스가 같은 입력으로 같은 상태가 되었는지 견주는 값이라 게임플레이를 바꾸는 상태(트랜스폼 · 속도 · 난수 상태 · 남은 시간)만 넣는다.
         *          게임 스레드에서, 해시 이력이 켜진 틱에만 불린다(`-gv_stateHashHistory`). 파생 타입은 부모 것을 먼저 부른다.
         */
        virtual void foldStateHash( StateHashBuilder& inoutBuilder ) const { (void)inoutBuilder; }
        /**
         * @brief 리플렉션 값을 직렬화기로 **직접** 쓴 뒤 부릅니다. 프로퍼티마다 `onPropertyChanged` 를 부르고, 그다음 `onPostLoad` 를 부릅니다(언리얼의
         *        `PostEditChangeProperty`, 유니티의 `OnValidate` 에 해당).
         * @details 에디터에서 컴포넌트 값 붙여넣기, 새로 붙여넣기, 프리셋, 오버라이드 되돌리기, 기본값 되돌리기는 `BinarySerializer`,
         *          `XMLSerializer`, `JSONSerializer` 로 값을 바로 쓰는데, 이 경로는 알림을 부르지 않는다. 이 함수를 빼면 트랜스폼이 더티가 되지 않고
         *          (값은 바뀌었는데 화면에서 움직이지 않는다) 렌더 에셋(메시, 머티리얼, 텍스처)을 다시 찾지 않는다. 상태를 통째로 읽는 경로
         *          (`ObjectStateSerializer`)는 컴포넌트를 새로 만들고 `onPostLoad` 를 부르므로 이 함수가 필요 없다.
         */
        void notifyStateWritten();

        /**
         * @brief 서브틱을 등록합니다(TickGroup, Phase, Priority 를 지정).
         * @details 우선순위는 한 단계 안에서의 순서(0..`kMaxTickPriority`)이고, 넘으면 상한으로 제한한다(경고). 그룹이 유효하지 않으면 등록하지 않고 빈 핸들을 반환한다.
         *
         *          **서브틱 목록(`_listSubTick`)을 바꾸는 네 함수(등록, 해제, 선행 조건, 활성)는 틱 중이면 변경을 틱 직후로 미룬다**(구조 변경 큐이며,
         *          부른 순서대로 처리하므로 이어서 부른 선행 조건 추가도 등록 뒤에 실행된다). 목록은 이 컴포넌트를 틱하는 워커가 읽는데(64 번부터의 활성 여부,
         *          자기 틱 안의 등록과 해제), 다른 오브젝트의 틱이 목록을 늘리면 벡터가 다시 할당되어 그 워커가 해제된 메모리를 읽는다. 두 워커가 같은 컴포넌트에
         *          등록하면 `push_back` 이 겹친다(형제 함수인 `setTickGroup` 과 `setCanEverTick` 도 같은 이유로 미룬다). 미루더라도 핸들은 바로 반환한다.
         *          실행 여부(서브틱 1~63 은 원자 마스크, 64 번부터는 `SubTickInfo::_bRunnable`)는 틱 중에 바꿀 수 있도록 원자값이다. 해제하거나 끄면 이 값을 바로
         *          내려서, 이번 틱의 남은 항목이 곧바로 건너뛴다.
         */
        SubTickHandle registerSubTick( TickGroup group, uint32 subTickID, TickPhase phase = TickPhase::Normal, uint8 priority = 0 );
        /** @brief 서브틱 하나의 등록을 해제합니다. 있었으면 true 입니다. 틱 중이면 마스크만 바로 내리고 목록은 틱 직후로 미루며, 이때도 true(접수됨)를 반환합니다. */
        bool unregisterSubTick( uint32 subTickID );
        /**
         * @brief 서브틱에 선행 조건을 추가합니다. prerequisiteHandle 이 먼저 실행되어야 합니다.
         * @details 선행 조건이 뒤쪽 그룹에 있으면 이 서브틱이 그 그룹으로 옮겨 가서 돈다(언리얼의 `ActualStartTickGroup`). 체인을 따라 옮긴다.
         *          선행 조건이 있는 서브틱만 별도 스테이지로 가고, 이 컴포넌트의 다른 틱과 체인 밖 오브젝트는 기본 경로 그대로 돈다.
         *          앞에서 실행된 선행 조건이 쓴 트랜스폼은 이 서브틱이 실행되기 전에 적용되므로 같은 프레임에 보인다.
         *          소유자 없이 만든 핸들(`_objectID` 0)은 받지 않는다(false). 틱 중이면 인자만 검사하고 틱 직후로 미루며, 이때는 true(접수됨)를 반환한다.
         */
        bool addSubTickPrerequisite( uint32 subTickID, const SubTickHandle& prerequisiteHandle );
        /** @brief 선행 조건 하나를 제거합니다. 있었으면 true 입니다. 틱 중이면 틱 직후로 미루며 true(접수됨)를 반환합니다. 대상이 바뀌어 선행 조건을 다시 등록할 때 씁니다. */
        [[nodiscard]] bool removeSubTickPrerequisite( uint32 subTickID, const SubTickHandle& prerequisiteHandle );
        /**
         * @brief 이 컴포넌트의 주 틱(`onTick`)을 가리키는 핸들입니다. 다른 컴포넌트의 서브틱 선행 조건으로 등록할 때 씁니다(언리얼의 `AddTickPrerequisiteComponent` 의 대상에 해당).
         * @details 오브젝트에 붙은 뒤에 얻어야 한다(소유 오브젝트 id 가 핸들에 들어간다). 그 컴포넌트가 틱하지 않거나 꺼져 있으면 순서를 만들지 않는다.
         */
        SubTickHandle getTickHandle() const { return makeTickHandle( 0 ); }
        /** @brief 서브틱의 활성 여부를 설정합니다. 틱 중이면 마스크(1~63)만 바로 바꾸고 목록의 값은 틱 직후로 미룹니다. */
        void setSubTickActive( uint32 subTickID, bool bActive );
        /** @brief 서브틱이 활성 상태이면 true 입니다(비트마스크로 O(1)). */
        bool isSubTickActive( uint32 subTickID ) const
        {
            if ( subTickID == 0 )
                return false;
            if ( subTickID < 64 )
                return ( _subTickActiveMask.load( std::memory_order_relaxed ) & ( 1ULL << subTickID ) ) != 0;
            return isSubTickActiveSlow( subTickID );
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
         * @brief 주 틱에 참여할지 설정합니다. 생성자에서 끄면(언리얼의 `bCanEverTick = false`) 끈 것이 이깁니다.
         * @details **기본값은 "`onTick` 을 오버라이드했는가"** 이다(유니티는 `Update` 가 있으면 부른다). `GameObject::addComponent` 가
         *          `HasOnTickOverride_v` 로 판정하고, 오버라이드하지 않은 타입은 끈다. `Component` 와 `SceneComponent` 모두 같은 규칙이다.
         */
        void setCanEverTick( bool bCanEverTick );
        /**
         * @brief 컴포넌트의 이름을 설정합니다(기본은 타입 이름). **타입이 아닙니다.** 상태와 함께 저장됩니다.
         * @details 오브젝트 안에서 컴포넌트를 가리키는 키(`ComponentStableKey`: 부착 대상, 프리팹 오버라이드, 에디터 선택 복원)가 이 이름으로
         *          만들어진다. 언리얼에서 컴포넌트 이름이 참조와 오버라이드의 키인 것과 같다. 이름이 없는 상태는 기본값(타입 이름)으로
         *          읽힌다. 타입은 만들 때 받은 `TypeInfo`(`_pTypeInfo`)이고 이름과 무관하다. 타입 이름이 필요하면 `getTypeName` 을 쓴다.
         * @note 풀 키도 **아니다.** 파괴할 때는 `_pPoolEntry` 로 돌아간다. 이름을 바꾸면 그 컴포넌트를 키로 가리키던 저장된 참조(다른 컴포넌트의 부착,
         *       프리팹 오버라이드)가 다음 로드에서 대상을 잃는다. 언리얼에서 컴포넌트 이름을 바꿀 때와 같다.
         */
        void setComponentName( hashed_string name ) { _componentName = name; }

        /** @brief 이 인스턴스의 컴포넌트 핸들을 반환합니다. */
        sw::ComponentHandle getHandle() const;

        /**
         * @brief 런타임 타입 리플렉션 정보(TypeInfo)입니다. 만들 때 받은 타입이며, 그 타입이 해제됐으면 nullptr 입니다.
         * @details 이름(`_componentName`)과는 무관하다. `setComponentName` 으로 이름을 바꿔도 타입은 그대로다. TypeInfo 의 주소는
         *          고정이므로(`TypeRegistry`) 포인터 하나만 보관하면 된다.
         */
        virtual const TypeInfo* getTypeInfo() const;
        /** @brief 타입 이름입니다(타입이 없으면 컴포넌트 이름). 복사, 붙여넣기, 프리셋, 타입 필터처럼 "무슨 타입인가"를 묻는 곳이 씁니다. */
        hashed_string getTypeName() const;
        /**
         * @brief 만들 때 받은 TypeInfo 가 살아 있으면 그것을, 아니면 가상 `getTypeInfo()` 의 결과를 반환합니다.
         * @details `castTo` 의 핫패스다. 캐시에 적중하면 가상 호출 없이 포인터 하나와 원자 로드 하나로 끝난다. 테스트 목처럼
         *          `getTypeInfo()` 를 오버라이드하는 타입도 `addComponent<T>` 가 같은 `T::StaticType()` 을 넘기므로 결과가 같다.
         */
        const TypeInfo* findCachedTypeInfo() const
        {
            if ( _pTypeInfo != nullptr && _pTypeInfo->isAlive() )
                return _pTypeInfo;
            return getTypeInfo();
        }
        /** @brief 소유자 GameObject 를 반환합니다. */
        GameObject* getOwner() const { return _pOwner; }
        /** @brief 활성 상태이면 true 입니다(자기 활성 비트와 소유자 활성을 모두 봅니다). */
        bool isActive() const;
        /** @brief 자기 활성 비트만 봅니다. 소유자 활성을 이미 확인한 곳(오브젝트 단위 틱 디스패치)에서 씁니다. */
        bool isSelfActive() const { return _bActive.load( std::memory_order_relaxed ); }
        /** @brief 현재 틱 그룹을 반환합니다. */
        TickGroup getTickGroup() const { return static_cast<TickGroup>( _tickGroup ); }
        /** @brief 주 틱에 참여하면 true 입니다. */
        bool canEverTick() const { return _bCanEverTick == SW_TRUE; }
        /** @brief 틱에 참여할 일이 있으면 true 입니다. 주 틱이 켜졌거나 서브틱이 하나라도 등록되어 있을 때이며, 소유 오브젝트의 틱 항목을 다시 만들지 정하는 기준입니다. */
        bool hasTickWork() const { return canEverTick() || _listSubTick.empty() == false; }
        /**
         * @brief 씬 컴포넌트(트랜스폼을 가진 것)인지 리플렉션 없이 답합니다.
         * @details `castTo<SceneComponent>` 는 가상 호출 하나, 캐시 조회 둘, 조상 테이블 비교로 이루어져 약 8 ns 가 든다. 워커 14개가
         *          건마다 이것을 부르자 배치 트랜스폼 쓰기가 세터보다 **느려졌다**(체인을 따라가던 때 8000건에 2.1 ms).
         *          생성자에서 세우는 비트 하나로 판정한다.
         */
        bool isSceneComponent() const { return _bIsSceneComponent == SW_TRUE; }

        /** @brief 컴포넌트 고유 ID 를 반환합니다. */
        uint64 getComponentID() const { return _componentID; }

        /** @brief 삭제 예정(묘비) 표시를 합니다. */
        void markPendingDestroy() { (void)tryMarkPendingDestroy(); } // 이미 표시돼 있어도 된다

        /**
         * @brief 삭제 예정 표시를 **이 호출이 처음으로 했는지** 반환합니다.
         * @details `isPendingDestroy()` 로 확인한 뒤 `markPendingDestroy()` 를 부르는 두 단계는 원자적이지
         *          않다. 두 스레드가 그 사이를 동시에 통과하면 파괴 목록에 같은 포인터가 **두 번**
         *          들어가고, 풀이 같은 블록을 두 번 반납한다. `onTick` 은 병렬로 실행되므로
         *          (총알 둘이 같은 적을 같은 프레임에 맞히는 경우처럼) 흔히 일어난다. 없애는 쪽은 반드시
         *          이 함수가 `true` 를 반환한 스레드 **하나만** 진행해야 한다.
         */
        [[nodiscard]] bool tryMarkPendingDestroy() { return _bIsPendingDestroy.exchange( true, std::memory_order_acq_rel ) == false; }

        /** @brief 삭제 예정인지 확인합니다. */
        bool isPendingDestroy() const { return _bIsPendingDestroy.load( std::memory_order_acquire ); }
        /** @brief 컴포넌트 이름(해시)을 반환합니다. */
        hashed_string getComponentName() const { return _componentName; }

    private:
        bool isSubTickActiveSlow( uint32 subTickID ) const;
        /** @brief 이 컴포넌트의 틱 하나(@p subTickID, 0 이면 주 틱)를 가리키는 핸들을 만듭니다. 소유 오브젝트 id 를 함께 담습니다(없으면 0). */
        SubTickHandle makeTickHandle( uint32 subTickID ) const;
        /** @brief 서브틱의 실행 여부를 바로 바꿉니다. 1~63 은 원자 마스크, 64 번부터는 목록 원소의 원자값을 바꿉니다. 틱 중에도 부를 수 있습니다. */
        void setSubTickRunnable( uint32 subTickID, bool bRunnable );
        /**
         * @brief 소유 매니저가 구조 변경을 막고 있으면(컴포넌트 틱 중) @p func 를 틱 직후 구조 변경 큐로 미루고 true 를 반환합니다. 아니면 false 입니다.
         * @details 핸들로 다시 찾으므로 그 사이에 파괴돼도 안전하다. 틱 설정(그룹, 틱 여부, 서브틱)이 `GameObject` 의 setName, addTag 와 같은 규칙을 따른다.
         */
        bool                  deferIfStructureFrozen( Delegate<void( Component& )> func );
        static atomic<uint64> _s_nextComponentID; ///< ID 생성 카운터

    protected:
        GameObject* _pOwner;      ///< 소유자 GameObject
        uint64      _componentID; ///< 컴포넌트 고유 일련번호
        /** @brief 이름입니다(기본은 타입 이름). 저장되며 컴포넌트 키(`ComponentStableKey`)가 이 이름으로 만들어집니다. 타입이 아닙니다. 타입은 `getTypeName` 을 씁니다. */
        PROPERTY( HideInInspector )
        hashed_string   _componentName;
        const TypeInfo* _pTypeInfo; ///< 만들 때 받은 타입이다. `GameObject::attachCreatedComponent` 가 한 번 기록하며, 공개 전이라 원자값이 아니다
        /**
         * @brief 이 인스턴스를 내준 타입 항목(풀과 버킷, `ComponentPoolEntry`)입니다. 힙에서 왔으면 nullptr 입니다. 생성할 때 한 번 기록하고 파괴할 때 읽습니다.
         * @details 파괴할 때 타입 이름으로 풀을 다시 찾으면 안 된다. 이름이 바뀌었거나(`setComponentName`) 그 타입이 그새 해제되었으면
         *          풀을 찾지 못해 풀 블록을 힙으로 반납하고, Shipping 에서 힙이 깨진다(0xc0000374. Debug 와 ASan 은 조용하다).
         *          같은 항목이 매니저의 타입별 목록(버킷)이라, 해체가 여기서 자기 자리(`_poolEntrySlot`)를 뺀다.
         */
        ComponentPoolEntry* _pPoolEntry;

        atomic<uint64> _subTickActiveMask; ///< 서브틱 1~63 의 활성 상태(원자 비트마스크, O(1))
        /**
         * @brief 컴포넌트 자기 활성 비트입니다. 저장됩니다(PROPERTY).
         * @details 스냅샷이 이 값을 실어야 끈 컴포넌트가 Stop, 되돌리기, 씬 다시 열기 뒤에도 꺼진 채로 남고 토글이 되돌리기 기록에 남는다.
         *          인스펙터는 컴포넌트 머리의 체크박스로 그린다.
         */
        PROPERTY( HideInInspector )
        atomic<bool> _bActive;
        atomic<bool> _bIsPendingDestroy; ///< 삭제 예정 표시
        TickGroup    _tickGroup;         ///< 이 컴포넌트가 속한 TickGroup
        uint8        _bCanEverTick      : 1;
        uint8        _bIsSceneComponent : 1; ///< SceneComponent 생성자가 세운다
        uint8        _bHasBegunPlay     : 1; ///< onBeginPlay 가 불렸고 onEndPlay 는 아직 불리지 않았다. `dispatchBeginPlay` 와 `dispatchEndPlay` 만 바꾼다
        uint8        _reservedFlags     : 5;
        /**
         * @brief `_pPoolEntry` 의 버킷 안 위치입니다. 버킷에 없으면 `kNotInPoolEntry` 입니다. 제거를 O(1) 로 하려고 둡니다.
         * @details 플래그(4 B) 뒤, `_listSubTick` 앞의 정렬 패딩에 들어간다. 앞쪽으로 옮기면 객체가 커진다. 저장소의 `_mutex` 아래에서만 바뀐다.
         */
        uint32              _poolEntrySlot;
        vector<SubTickInfo> _listSubTick; ///< 등록된 보조 서브틱 목록
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 타입 T(또는 그 조상 중 `Component` 가 아닌 것)가 `onTick` 을 오버라이드했는지 나타냅니다. 주 틱의 기본값이 이것입니다.
     * @details `&T::onTick` 의 타입이 `Component` 의 것이면 아무도 오버라이드하지 않은 것이다. 볼 수 없으면(보호나 비공개 오버라이드라
     *          접근이 막히면) 오버라이드한 것으로 취급한다. 그때는 생성자의 값을 그대로 둔다.
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
