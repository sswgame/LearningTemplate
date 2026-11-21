/**
 * @file FixedStepTickList.h
 * @brief 고정 스텝 그룹(`FixedStepGroup`)의 구성원과 그룹마다의 일괄 구간입니다 — 틱 스케줄러가 프레임 그룹 레지스트리(`TickRegistry`)와 나란히 소유합니다(`SceneTickScheduler::getFixedStepTickList`).
 *
 * [왜 틱 시스템에 있는가]
 * 고정 스텝 훅(물리 프레임 시작 · 스텝 앞 · 스텝 뒤 · 프레임 끝)은 주기가 프레임 틱과 다르지만 "누구를 어떤 차례로, 어떤 함수로 부르나"는 같은 문제다.
 * 그래서 등록은 프레임 틱과 같은 표(`TickBatchRegistry::FixedStepEntry`, 생성기가 낸 타입별 일괄 함수)에서, 디스패치는 같은 스케줄러
 * (`SceneTickScheduler::tickFixedStepGroup`)에서 한다. 가상 호출 길은 없다 — 등록이 없는 타입의 훅은 부르지 않는다.
 * 유니티 `FixedUpdate` · Godot `_physics_process` 가 프레임 콜백과 같은 실행 순서 장치(Script Execution Order · process priority)를 쓰는 것과 같다.
 *
 * [차례]
 * 구성원은 단계(`TickPhase` — 64 간격 칸 하나가 버킷 하나)마다 넣은 차례로 놓이고, 빼면 끝 구성원이 그 자리로 온다. 그룹 구간은 그 차례 그대로에서
 * 이어진 같은 함수만 한 구간으로 묶는다 — 타입으로 다시 줄 세우면 바디를 만드는 차례(Jolt 바디 id)가 바뀌어 시뮬레이션 결과(골든 · 결정성)가 달라진다.
 * 오브젝트 사이 차례는 단계로만 정한다(물리: 바디 Early → 관절 Normal → 캐릭터 Late). 서브틱 선행 조건 DAG 는 고정 스텝에 두지 않는다 — 쓰는 곳이 없고,
 * 유니티 · Godot 도 고정 스텝 차례를 우선순위 하나로 정한다.
 *
 * [켜고 끄기]
 * 구성원은 등록 범위(`add` ~ `remove`)이고, 켜짐(`setEnabled`)은 값이다 — 구간을 지을 때 그때의 값만 본다. 바꾸는 쪽에서 지금 값과 견주어 일찍 돌아가지
 * 않으므로 "끄기를 미루고 켜기는 같다고 버림" 같은 늦은 비교 결함이 없다. 그룹을 도는 중에 바꾸면 그 그룹의 구간은 그대로 끝까지 돌고, 다음 그룹부터 맞다.
 * 계층 활성으로 빼지 않는다 — 물리는 꺼진 컴포넌트에서도 프레임 시작 훅으로 백엔드 객체를 놓는다. 매니저의 비우기도 따로 비우지 않는다 —
 * 구성원은 컴포넌트의 등록 · 해제가 넣고 빼므로, 오브젝트를 지우는 길이 해제에서 빼 간다.
 *
 * @note 게임 스레드에서만 부릅니다(물리 프레임 · 등록 · 해제). 틱 본문은 const 매니저만 받아 이 목록을 바꿀 수 없다(`TickContext`).
 */
#pragma once
#include "Core/Container/EnumArray.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Tick/FixedStepContext.h"

namespace sw
{
    class Component;
    class TickBatchRegistry;
    struct TypeInfo;
    enum class TickPhase : uint8;

    /** @class FixedStepTickList @brief 고정 스텝 그룹의 구성원 · 일괄 구간입니다. 파일 머리말 참고. */
    class SW_API FixedStepTickList
    {
    public:
        /** @brief 단계 버킷 수입니다(`TickPhase` 의 64 간격 칸). */
        static constexpr uint32 kPhaseBucketCount = 4;
        /** @brief 단계 버킷 하나의 너비입니다(`TickPhase` 는 64 간격 — 그 사이는 서브틱 우선순위 자리). */
        static constexpr uint32 kPhaseBucketWidth = 64;

        FixedStepTickList();
        ~FixedStepTickList() = default;

        FixedStepTickList( const FixedStepTickList& )            = delete;
        FixedStepTickList& operator=( const FixedStepTickList& ) = delete;

        /** @brief @p component 를 단계 @p phase 의 끝에 켜진 채로 넣습니다. 이미 있으면 아무것도 하지 않습니다(false). */
        bool add( Component& component, TickPhase phase );
        /** @brief @p component 를 뺍니다. 그 단계의 끝 구성원이 그 자리로 옵니다. 없으면 아무것도 하지 않습니다(false). */
        bool remove( const Component& component );
        /** @brief @p component 를 켜거나 끕니다(값 — 파일 머리말). 구성원이 아니면 false 입니다. */
        bool setEnabled( const Component& component, bool bEnabled );
        /** @brief @p component 가 구성원이고 켜져 있으면 true 입니다. */
        bool isEnabled( const Component& component ) const;
        /** @brief 구성원 수입니다(꺼진 것 포함). */
        uint32 getMemberCount() const { return static_cast<uint32>( _mapLocation.size() ); }

        /**
         * @brief 그룹 @p group 을 돕니다 — 구간이 낡았으면(구성원 · 켜짐 · 표 세대) 먼저 다시 짓고, 구간마다 그 타입의 일괄 함수를 한 번 부릅니다.
         * @param pRegistry 타입별 일괄 함수 표(비면 아무 훅도 부르지 않는다 — 엔진 없는 테스트 매니저)
         * @return 부른 구성원 수입니다.
         */
        uint32 run( FixedStepGroup group, const FixedStepContext& context, TickBatchRegistry* pRegistry );

    private:
        /** @brief 구성원 하나입니다. */
        struct Member
        {
            Component* _pComponent{ nullptr };
            uint8      _bEnabled{ SW_TRUE };
        };
        /** @brief 구성원의 자리입니다(`_arrListMember[_bucket][_index]`). */
        struct Location
        {
            uint32 _bucket{ 0 };
            uint32 _index{ 0 };
        };
        /** @brief 그룹 구간 하나입니다 — `_arrListComponent[group]` 의 `[_start, _start + _count)` 에 함수 하나를 부른다. */
        struct Run
        {
            FixedStepBatchFunction _pFunction{ nullptr };
            uint32                 _start{ 0 };
            uint32                 _count{ 0 };
        };

        /** @brief 타입 하나의 그룹별 일괄 함수입니다(표의 줄을 값으로 옮겨 둔다 — 줄이 내려가도 들고 있지 않게 세대가 바뀌면 비운다). */
        using FunctionTable = EnumArray<FixedStepGroup, FixedStepBatchFunction>;

        /** @brief 표 세대가 바뀌었으면 타입 기억을 비우고, 낡았으면 모든 그룹의 구간을 다시 짓습니다. */
        void refresh( TickBatchRegistry* pRegistry );
        /** @brief 정확히 @p pType 의 그룹별 일괄 함수입니다(타입마다 한 번 표에 묻고 기억 — 줄이 없으면 모두 nullptr 로 기억한다). */
        const FunctionTable& findFunctions( TickBatchRegistry& registry, const TypeInfo* pType );

    private:
        vector<Member>                                _arrListMember[kPhaseBucketCount]; ///< 단계마다 넣은 차례의 구성원
        unordered_map<const Component*, Location>     _mapLocation;                      ///< 구성원 → 자리(빼기 · 켜고 끄기)
        EnumArray<FixedStepGroup, vector<Component*>> _arrListComponent;                 ///< 그룹마다 부를 구성원(구간 순)
        EnumArray<FixedStepGroup, vector<Run>>        _arrListRun;                       ///< 그룹마다 구간
        unordered_map<const TypeInfo*, FunctionTable> _mapFunctions;                     ///< 타입 → 그룹별 일괄 함수(`findFunctions`)
        uint32                                        _registryGeneration;               ///< 마지막으로 본 `TickBatchRegistry::getGeneration`
        uint8                                         _bStale;                           ///< 구간을 다시 지어야 한다
    };
} // namespace sw
