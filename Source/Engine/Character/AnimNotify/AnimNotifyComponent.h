/**
 * @file AnimNotifyComponent.h
 * @brief 알림 디스패치 — 같은 오브젝트의 애니메이터(3D 스켈레탈 · 2D 스프라이트)가 울린 알림을 알림 표(`*.notifies.xml`)의 처리기로 보냅니다.
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
     * @brief 처리기가 한 일 하나의 기록입니다(진단 · 시험 · 디버그 표시 — 이번 디스패치의 것만 남는다).
     */
    struct AnimNotifyAction
    {
        hashed_string   _notify{};
        hashed_string   _handler{};
        hashed_string   _detail{}; ///< 처리기마다 — 발소리는 바닥 재질, 소리는 경로, 판정은 맞은 히트 존
        float3          _position{};
        uint64          _targetObjectId{ 0 }; ///< 맞힌 오브젝트(판정) · 스폰한 오브젝트
        AnimNotifyPhase _phase{ AnimNotifyPhase::Instant };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimNotifyListenerBinding
     * @brief 컴포넌트가 애니메이터에 보이는 얼굴입니다(리플렉션 컴포넌트는 기반 하나 — `SkeletalAnimatorBinding` 과 같은 자리).
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
     * @brief 같은 오브젝트의 애니메이터에 받는 쪽으로 붙어 알림을 처리기로 보냅니다(언리얼 AnimNotify · AnimNotifyState 의 디스패치, 유니티 Animation Event 의 수신자).
     * @details - **한 번씩**: 알림은 트랙의 의미(지나간 것을 정확히 한 번, 반복 경계 포함)대로 옵니다. 구간 알림은 시작 → 프레임마다 틱 → 끝이고,
     *            클립이 재생에서 빠지면(상태 전이 · 정지) 끝을 대신 냅니다. 같은 구간은 열린 것이 하나뿐입니다.
     *          - **스레드**: 3D 는 애니메이션 시스템의 게임 스레드 마무리에서 바로, 2D 는 스프라이트 틱(워커)에서 받아 틱 뒤 게임 스레드로 미뤄 처리합니다 —
     *            처리기는 늘 게임 스레드에서 돕니다(스폰 · 물리 질의 · 맞음 알림).
     *          - **차원**: 같은 오브젝트에 `SkeletalMeshComponent` 가 있으면 3D, 없으면 2D 물리 씬에 묻습니다.
     *          - 표는 공유 캐시(`AnimNotifyTableCache`)에서 받고, 파일을 고치면 열린 구간을 닫고 새 표로 잇습니다.
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

        /** @brief 알림 표 경로를 바꾸고 다시 받습니다. */
        void          setNotifyTablePath( string_view path );
        const string& getNotifyTablePath() const { return _notifyTablePath; }
        /** @brief 표를 직접 정합니다(시험 · 절차 생성, 저장되지 않음). */
        void setNotifyTable( shared_ptr<const AnimNotifyTable> table );
        /** @brief 지금 표입니다. 없으면 nullptr 입니다. */
        const AnimNotifyTable* getNotifyTable() const { return _table.get(); }

        /** @brief 한 프레임의 알림을 처리합니다(게임 스레드). 애니메이터가 받는 쪽으로 부르고, 시험이 직접 부를 수도 있습니다. */
        void processFrame( const AnimNotifyFrame& frame );
        /** @brief 열린 구간을 모두 닫습니다(끝 처리기를 부릅니다). */
        void closeAllStates();
        /** @brief 열린 구간 수입니다. */
        uint32 getActiveStateCount() const { return static_cast<uint32>( _listActiveState.size() ); }
        /** @brief 마지막 처리에서 처리기가 한 일입니다(진단 · 시험). */
        const vector<AnimNotifyAction>& getActions() const { return _listAction; }

        // --- 처리기가 쓰는 창구 ---
        /** @brief 2D 물리 씬에 묻는지입니다(같은 오브젝트에 스켈레탈 유닛이 없다). */
        bool is2D() const;
        /** @brief 소켓(또는 본) 이름의 월드 변환입니다. 이름이 비었거나 없으면 오브젝트 루트이고 false 입니다. */
        bool findSocketWorldTransform( const hashed_string& socketName, float4x4& outWorldTransform ) const;
        /** @brief 소켓의 월드 자리입니다. 찾지 못하면 오브젝트 루트 자리입니다. */
        float3 findSocketWorldPosition( const hashed_string& socketName ) const;
        /**
         * @brief 이 오브젝트의 바디를 모두 건너뛰고 @p from → @p to 를 쏩니다(차원에 맞는 씬). @p radius 가 0 보다 크면 구(원)를 쓸어 갑니다.
         * @return 맞은 것이 있으면 true 입니다.
         */
        bool castFromTo( const float3& from, const float3& to, float32 radius, uint32 layerMask, CharacterRayHit& outHit ) const;
        /** @brief 처리기가 한 일을 기록합니다. */
        void recordAction( const AnimNotifyAction& action ) { _listAction.push_back( action ); }

    private:
        /** @brief 열린 구간 하나입니다. 표의 줄은 베껴 듭니다(표가 제자리로 다시 읽혀도 끝을 낼 수 있게). */
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
        /** @brief 표를 (다시) 받습니다. */
        void loadNotifyTable();
        /** @brief 2D — 워커에서 받은 프레임을 베껴 두고 틱 뒤 처리를 겁니다. */
        void deferFrame( const AnimNotifyFrame& frame );
        /** @brief 베껴 둔 프레임을 처리합니다(게임 스레드). */
        void processPendingFrame();
        /** @brief 구간 하나를 닫고 목록에서 뺍니다. */
        void endState( size_t stateIndex, const AnimFiredNotify* pEndFired, float32 deltaSeconds );
        /** @brief 처리기 문맥을 만들어 부릅니다. */
        void callHandler( const AnimNotifyEntry& entry, const AnimFiredNotify& fired, AnimNotifyStateData* pState, AnimNotifyPhase phase, bool bTick, float32 deltaSeconds );
        /** @brief 표의 줄입니다(표가 없으면 nullptr). */
        const AnimNotifyEntry* findEntry( const hashed_string& notify ) const;

        PROPERTY( Category = "Animation", DisplayName = "Notify Table", AssetPath, AssetType = "AnimNotifyTable", Tooltip = "Notify name to handler table (*.notifies.xml)" )
        string _notifyTablePath;

        AnimNotifyListenerBinding         _binding;
        shared_ptr<const AnimNotifyTable> _table;
        vector<ActiveState>               _listActiveState;
        vector<AnimNotifyAction>          _listAction;
        vector<AnimFiredNotify>           _listPendingFired;    ///< 2D: 워커가 베낀 이번 틱의 알림
        vector<const IAnimPlayable*>      _listPendingActive;   ///< 2D: 워커가 베낀 재생 중인 것
        ComponentHandle                   _animator;            ///< 받는 쪽으로 붙은 애니메이터(스켈레탈 또는 스프라이트)
        float32                           _pendingDeltaSeconds; ///< 2D: 베낀 프레임의 걸음
        uint32                            _seenTableReloadCount;
        uint8                             _bPendingFrame;     ///< 2D: 처리를 기다리는 프레임이 있다
        uint8                             _bPendingRestarted; ///< 2D: 베낀 프레임 중 재생할 것이 바뀐 것이 있었다
    };
} // namespace sw
