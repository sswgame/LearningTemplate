/**
 * @file Component.h
 * @brief GameObject에 부착되는 컴포넌트의 기반 클래스 및 TickGroup 열거형 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

#include "Engine/Object/Component/ComponentHandle.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{

    class GameObject;
    class GameObjectManager;
    /**
     * @enum TickGroup
     * @brief 프레임 내 컴포넌트 tick 실행 순서 슬롯
     * @details 이름은 물리 파이프라인 단계를 따르지만, 현재 엔진에 물리 시스템이 없어도 정렬용으로 사용합니다.
     */
    enum class TickGroup : uint8
    {
        PrePhysics,    ///< 이른 업데이트 단계
        DuringPhysics, ///< 기본 tick 단계
        PostPhysics,   ///< 늦은 업데이트 단계
        PostUpdate,    ///< 렌더 직전 등 최종 단계
    };

    /**
     * @enum TickPhase
     * @brief 동일한 TickGroup 내에서의 세부 실행 페이즈 (Phase)
     */
    enum class TickPhase : uint8
    {
        Early    = 0,   ///< 선행 연산 (데이터 준비, 물리 전처리)
        Normal   = 64,  ///< 기본 연산 (일반 게임플레이)
        Late     = 128, ///< 후행 연산 (래그돌 합성, 소켓 어태치먼트)
        Finalize = 192  ///< 최종 연산 (GPU 버퍼 업로드, LOD 계산)
    };

    /**
     * @struct SubTickHandle
     * @brief 서브틱 식별 및 선행 종속성(Prerequisite) 연결을 위한 고유 핸들
     */
    struct SubTickHandle
    {
        uint64 _componentId{ 0 };
        uint32 _subTickId{ 0 };

        constexpr bool isValid() const
        {
            return _componentId != 0 && _subTickId != 0;
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

    struct SubTickHandleHash
    {
        size_t operator()( const SubTickHandle& handle ) const noexcept
        {
            return static_cast<size_t>( handle._componentId ^ ( static_cast<uint64>( handle._subTickId ) << 32 ) );
        }
    };

    /**
     * @struct SubTickInfo
     * @brief 컴포넌트에 등록된 개별 서브틱의 메타데이터 및 종속성 정보
     */
    struct SubTickInfo
    {
        uint32                _subTickId{ 0 };
        TickGroup             _group{ TickGroup::DuringPhysics };
        TickPhase             _phase{ TickPhase::Normal };
        uint8                 _priority{ 0 };
        uint8                 _bActive{ SW_TRUE };
        vector<SubTickHandle> _listPrerequisite;
    };

    /**
     * @class Component
     * @brief GameObject의 기능 및 데이터를 분리 확장하기 위한 컴포넌트 기반 클래스
     */
    class SW_API Component
    {
        friend class GameObject;

    public:
        /** @brief 기본 컴포넌트를 만듭니다. */
        Component();

        /** @brief 복사를 금지합니다. */
        Component( const Component& ) = delete;
        /** @brief 대입을 금지합니다. */
        Component& operator=( const Component& ) = delete;

        /** @brief 이동 생성자입니다. */
        Component( Component&& other ) noexcept;
        /** @brief 이동 대입입니다. */
        Component& operator=( Component&& other ) noexcept;

        /** @brief 가상 소멸. GameObject가 sw_delete로 해제합니다. */
        virtual ~Component() = default;

        /** @brief 게임 컴포넌트 기본값 XML(gamedata.xml) 경로를 지정합니다. 비어 있으면 주입하지 않습니다. */
        static void setDefaultGamedataPath( string_view path );
        /** @brief 현재 게임 컴포넌트 기본값 XML 경로를 반환합니다. */
        static string_view getDefaultGamedataPath();

        /** @brief 게임플레이 시작 시 초기화 콜백 */
        virtual void onBeginPlay();
        /** @brief 게임플레이 종료 시 정리 콜백 (beginPlay 대응) */
        virtual void onEndPlay();
        /** @brief 프레임 단위 메인 업데이트 콜백 */
        virtual void onTick( float32 deltaTime );
        /** @brief 프레임 단위 보조 서브틱 업데이트 콜백 */
        virtual void onSubTick( uint32 subTickId, float32 deltaTime );
        /**
         * @brief 소유 GameObject 에 붙은 직후 호출됩니다.
         * @details 자기가 어떤 등록부에 들어가야 하는지는 자기가 안다. GameObject 가 `castTo` 로
         *          타입을 골라 대신 등록해 주면, 등록부가 하나 늘 때마다 GameObject 를 고쳐야 하고
         *          GameObject 가 MeshComponent 같은 하위 타입을 알게 된다.
         * @param manager 자기가 속한 매니저. 필요한 것(등록부·물리 월드 등)을 여기서 꺼내 **들고
         *        있는다**. 쓸 때마다 소유자를 거슬러 올라가 찾지 않는다 — 그 조회는 소유자가 이미
         *        끊긴 파괴 시점에 엉뚱한 씬을 가리킨다.
         */
        virtual void onRegister( GameObjectManager& manager ) { (void)manager; }
        /**
         * @brief 소유 GameObject 에서 떨어지기 직전 호출됩니다.
         * @note 파괴 경로가 여러 갈래라 **두 번 이상 불릴 수 있다**. 구현은 멱등이어야 합니다.
         */
        virtual void onUnregister( GameObjectManager& manager ) { (void)manager; }
        /** @brief 컴포넌트 소멸 및 해제 시 콜백 */
        virtual void onDestroy();
        /** @brief 프로퍼티 변경 시 이벤트 콜백 */
        virtual void onPropertyChanged( hashed_string propertyName );

        /** @brief 서브틱을 등록합니다. (TickGroup + Phase + Priority 지원) */
        SubTickHandle registerSubTick( TickGroup group, uint32 subTickId, TickPhase phase = TickPhase::Normal, uint8 priority = 0 );
        /** @brief 특정 서브틱을 등록 해제합니다. */
        bool unregisterSubTick( uint32 subTickId );
        /** @brief 특정 서브틱에 선행 종속성(Prerequisite)을 추가합니다. (prerequisiteHandle이 먼저 실행되어야 함) */
        bool addSubTickPrerequisite( uint32 subTickId, const SubTickHandle& prerequisiteHandle );
        /** @brief 특정 서브틱의 활성화 여부를 설정합니다. */
        void setSubTickActive( uint32 subTickId, bool bActive );
        /** @brief 특정 서브틱이 활성화되어 있는지 확인합니다. (O(1) Bitmask Fast-Path) */
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

        /** @brief 구체 타입 TypeInfo로 gamedata 기본값을 주입합니다. */
        void applyTypeDefaults( const TypeInfo* pTypeInfo );
        /** @brief 소유자 GameObject 설정 */
        void setOwner( GameObject* pOwner ) { _pOwner = pOwner; }
        /** @brief 컴포넌트 활성화/비활성화 상태 설정 */
        void setActive( bool bActive );
        /** @brief Tick 그룹 변경 */
        void setTickGroup( TickGroup group );
        /** @brief Primary tick 등록 여부를 설정합니다. 비주얼 컴포넌트는 false가 기본입니다. */
        void setCanEverTick( bool bCanEverTick );
        /** @brief 컴포넌트 해시 명칭 설정 */
        void setComponentName( hashed_string name ) { _componentName = name; }

        /** @brief 이 인스턴스의 컴포넌트 핸들을 반환합니다. */
        sw::ComponentHandle getHandle() const;

        /** @brief 런타임 타입 리플렉션 정보(TypeInfo) 반환 */
        virtual const TypeInfo* getTypeInfo() const;
        /** @brief 소유자 GameObject 반환 */
        GameObject* getOwner() const { return _pOwner; }
        /** @brief 활성화 상태 확인 (자체 활성화 여부 및 소유자 활성화 여부 모두 확인) */
        bool isActive() const;
        /** @brief 현재 Tick 그룹 반환 */
        TickGroup getTickGroup() const { return static_cast<TickGroup>( _tickGroup ); }
        /** @brief 매니저 tick 웨이브에 들어가면 true. */
        bool canEverTick() const { return _bCanEverTick == SW_TRUE; }

        /** @brief 컴포넌트 고유 ID 반환 */
        uint64 getComponentId() const { return _componentId; }

        /** @brief 지연 삭제 (Tombstone) 플래그 마킹 */
        void markPendingKill() { _bIsPendingKill.store( true, std::memory_order_release ); }

        /** @brief 현재 이 컴포넌트가 삭제 예정인지 확인 */
        bool isPendingKill() const { return _bIsPendingKill.load( std::memory_order_acquire ); }
        /** @brief 컴포넌트 해시 명칭 반환 */
        hashed_string getComponentName() const { return _componentName; }

    private:
        void                  initialize();
        bool                  isSubTickActiveSlow( uint32 subTickId ) const;
        static atomic<uint64> _s_nextComponentId; ///< ID 생성 카운터

    protected:
        GameObject*   _pOwner;        ///< 소유자 GameObject 포인터 참조
        uint64        _componentId;   ///< 컴포넌트 고유 시리얼 ID
        hashed_string _componentName; ///< 컴포넌트 식별 이름

        atomic<uint64>      _subTickActiveMask{ 0 };                ///< 서브틱 1~63 활성 상태 O(1) 원자적 비트마스크
        atomic<bool>        _bActive{ true };                       ///< 컴포넌트 개별 활성화
        atomic<bool>        _bIsPendingKill{ false };               ///< 지연 삭제 플래그
        TickGroup           _tickGroup{ TickGroup::DuringPhysics }; ///< TickGroup 슬롯
        uint8               _bCanEverTick  : 1;
        uint8               _reservedFlags : 7;
        vector<SubTickInfo> _listSubTick; ///< 등록된 보조 서브틱 목록
    };
} // namespace sw
