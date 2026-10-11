/**
 * @file AnimNotifyComponent.h
 * @brief 알림 디스패치입니다. 같은 오브젝트의 애니메이터(3D 스켈레탈, 2D 스프라이트)가 울린 알림을 알림 테이블(`*.notifies.xml`)의 처리기로 보냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Notify/AnimNotifyListener.h"
#include "Engine/Character/AnimNotify/AnimNotifyTable.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct CharacterRayHit;

    class AnimNotifyComponent;

    /**
     * @struct AnimNotifyAction
     * @brief 처리기가 한 일 하나의 기록입니다(진단, 테스트, 디버그 표시용이며 이번 디스패치의 것만 남습니다).
     */
    struct AnimNotifyAction
    {
        hashed_string   _notify{};
        hashed_string   _handler{};
        hashed_string   _detail{}; ///< 처리기마다 다르다. 발소리는 바닥 재질, 소리는 경로, 판정은 맞은 히트 존이다.
        float3          _position{};
        uint64          _targetObjectID{ 0 }; ///< 맞힌 오브젝트(판정)이거나 스폰한 오브젝트다.
        AnimNotifyPhase _phase{ AnimNotifyPhase::Instant };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimNotifyListenerBinding
     * @brief 컴포넌트가 애니메이터에 노출하는 어댑터입니다(리플렉션 컴포넌트는 기반 클래스 하나만 둘 수 있으며, `SkeletalAnimatorBinding` 과 같은 방식입니다).
     */
    class SW_API AnimNotifyListenerBinding final : public IAnimNotifyListener
    {
    public:
        explicit AnimNotifyListenerBinding( AnimNotifyComponent& owner );
        void onAnimNotifiesFired( const AnimNotifyFrame& frame ) override;

    private:
        AnimNotifyComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimNotifyComponent
     * @brief 같은 오브젝트의 애니메이터에 받는 쪽으로 붙어 알림을 처리기로 보냅니다(언리얼 AnimNotify, AnimNotifyState 의 디스패치와 유니티 Animation Event 의 수신자에 해당합니다).
     * @details - **한 번씩**: 알림은 트랙의 의미(지나간 것을 정확히 한 번, 반복 경계 포함)대로 온다. 구간 알림은 시작, 프레임마다 틱, 끝 순서이고,
     *            클립이 재생에서 빠지면(상태 전이나 정지) 끝을 대신 낸다. 같은 구간은 열린 것이 하나뿐이다.
     *          - **스레드**: 3D 는 애니메이션 시스템의 게임 스레드 마무리에서 바로 받고, 2D 는 스프라이트 틱(워커)에서 받아 틱 뒤 게임 스레드로 미뤄서 처리한다.
     *            처리기는 항상 게임 스레드에서 돈다(스폰, 물리 질의, 맞음 알림).
     *          - **차원**: 같은 오브젝트에 `SkeletalMeshComponent` 가 있으면 3D 로, 없으면 2D 물리 씬에 묻는다.
     *          - 테이블은 공유 캐시(`AnimNotifyTableCache`)에서 받는다. 파일을 고치면 열린 구간을 닫고 새 테이블로 이어 간다.
     */
    REFLECT( Category = "Animation", DisplayName = "Anim Notify", Tooltip = "Dispatches animation notifies to handlers named in a notify table (*.notifies.xml)" )
    class SW_API AnimNotifyComponent : public Component
    {
        friend class AnimNotifyListenerBinding;

    public:
        REFLECT_BODY();

        AnimNotifyComponent();
        virtual ~AnimNotifyComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 알림 테이블 경로를 바꾸고 다시 받습니다. */
        void          setNotifyTablePath( string_view path );
        const string& getNotifyTablePath() const { return _notifyTablePath; }
        /** @brief 테이블을 직접 정합니다(테스트와 절차 생성용이며 저장되지 않습니다). */
        void setNotifyTable( shared_ptr<const AnimNotifyTable> table );
        /** @brief 지금 표입니다. 없으면 nullptr 입니다. */
        const AnimNotifyTable* getNotifyTable() const { return _table.get(); }

        /** @brief 한 프레임의 알림을 처리합니다(게임 스레드). 애니메이터가 받는 쪽으로 부르고, 테스트가 직접 부를 수도 있습니다. */
        void processFrame( const AnimNotifyFrame& frame );
        /** @brief 열린 구간을 모두 닫습니다(끝 처리기를 부릅니다). */
        void closeAllStates();
        /** @brief 열린 구간 수입니다. */
        uint32 getActiveStateCount() const { return static_cast<uint32>( _listActiveState.size() ); }
        /** @brief 마지막 처리에서 처리기가 한 일을 반환합니다(진단과 테스트용). */
        const vector<AnimNotifyAction>& getActions() const { return _listAction; }

        // --- 처리기가 쓰는 함수 ---
        /** @brief 2D 물리 씬에 묻는지 반환합니다(같은 오브젝트에 스켈레탈 유닛이 없는 경우). */
        bool is2D() const;
        /** @brief 소켓(또는 본) 이름의 월드 변환입니다. 이름이 비었거나 없으면 오브젝트 루트이고 false 입니다. */
        bool findSocketWorldTransform( const hashed_string& socketName, float4x4& outWorldTransform ) const;
        /** @brief 소켓의 월드 위치를 반환합니다. 찾지 못하면 오브젝트 루트 위치를 반환합니다. */
        float3 findSocketWorldPosition( const hashed_string& socketName ) const;
        /**
         * @brief 이 오브젝트의 바디를 모두 건너뛰고 @p from → @p to 를 쏩니다(차원에 맞는 씬). @p radius 가 0 보다 크면 구(원)를 쓸어 갑니다.
         * @return 맞은 것이 있으면 true 입니다.
         */
        bool castFromTo( const float3& from, const float3& to, float32 radius, uint32 layerMask, CharacterRayHit& outHit ) const;
        /** @brief 처리기가 한 일을 기록합니다. */
        void recordAction( const AnimNotifyAction& action ) { _listAction.push_back( action ); }

    private:
        /** @brief 열린 구간 하나입니다. 테이블 항목은 복사해서 보관합니다(테이블이 제자리에서 다시 읽혀도 끝을 낼 수 있게 하려는 것입니다). */
        struct ActiveState
        {
            AnimNotifyEntry      _entry;
            AnimFiredNotify      _fired;
            AnimNotifyStateData  _data;
            const IAnimPlayable* _pSource{ nullptr };
            uint32               _eventIndex{ 0 };
        };

        /** @brief 같은 오브젝트의 애니메이터에 받는 쪽으로 붙습니다. */
        void bindToAnimator();
        /** @brief 붙었던 애니메이터에서 뗍니다. */
        void unbindFromAnimator();
        /** @brief 테이블을 (다시) 받습니다. */
        void loadNotifyTable();
        /** @brief 2D 에서 워커가 받은 프레임을 복사해 두고, 틱 뒤에 처리하도록 예약합니다. */
        void deferFrame( const AnimNotifyFrame& frame );
        /** @brief 베껴 둔 프레임을 처리합니다(게임 스레드). */
        void processPendingFrame();
        /** @brief 구간 하나를 닫고 목록에서 뺍니다. */
        void endState( size_t stateIndex, const AnimFiredNotify* pEndFired, float32 deltaSeconds );
        /** @brief 처리기 문맥을 만들어 부릅니다. */
        void callHandler( const AnimNotifyEntry& entry, const AnimFiredNotify& fired, AnimNotifyStateData* pState, AnimNotifyPhase phase, bool bTick, float32 deltaSeconds );
        /** @brief 테이블 항목을 반환합니다(테이블이 없으면 nullptr 입니다). */
        const AnimNotifyEntry* findEntry( const hashed_string& notify ) const;

        PROPERTY( Category = "Animation", DisplayName = "Notify Table", AssetPath, AssetType = "AnimNotifyTable", Tooltip = "Notify name to handler table (*.notifies.xml)" )
        string _notifyTablePath;

        AnimNotifyListenerBinding         _binding;
        shared_ptr<const AnimNotifyTable> _table;
        vector<ActiveState>               _listActiveState;
        vector<AnimNotifyAction>          _listAction;
        vector<AnimFiredNotify>           _listPendingFired;    ///< 2D 에서 워커가 복사한 이번 틱의 알림이다.
        vector<const IAnimPlayable*>      _listPendingActive;   ///< 2D 에서 워커가 복사한 재생 중인 항목이다.
        ComponentHandle                   _animator;            ///< 받는 쪽으로 붙은 애니메이터(스켈레탈 또는 스프라이트)
        float32                           _pendingDeltaSeconds; ///< 2D 에서 복사한 프레임의 이동 구간이다.
        uint32                            _seenTableReloadCount;
        uint8                             _bPendingFrame;     ///< 2D: 처리를 기다리는 프레임이 있다
        uint8                             _bPendingRestarted; ///< 2D: 베낀 프레임 중 재생할 것이 바뀐 것이 있었다
    };
} // namespace sw
