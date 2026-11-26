/**
 * @file ZoneCompiledModule.h
 * @brief 읽을 때 처리기 표로 검사 · 해석한 지역 모듈(`ZoneCompiledModule`)과 프로필(`ZoneCompiledProfile`), 그 매개변수 · 위치 값 형식입니다. 처리기 계약은 `IZoneModuleProcessor`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Registry/NamedEntryRegistry.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Behavior/GameActionCall.h"
#include "Engine/Behavior/GameplayContext.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneProfile.h"

namespace sw
{
    class AbilityCatalog;
    class GameObject;
    class GameObjectManager;
    class IZoneModuleProcessor;
    enum class TriggerAuthority : uint8; // 판정 자리(`TriggerComponent.h`) — 처리기가 고른다

    /** @brief 대상 하나의 위치 값입니다(처리기 · 식이 읽는다). 볼륨 모양으로 정규화돼 같은 프리셋이 어떤 모양에도 맞습니다. */
    struct ZoneSample
    {
        float3            _position{};              ///< 대상의 자리(발, 월드)
        float3            _center{};                ///< 당김 중심(월드) — 볼륨 중심 + 자리에서 준 오프셋
        float3            _flowDirection{};         ///< 대상 자리의 흐름 방향(단위, 월드). 흐름 점이 없으면 0
        float32           _zoneDepth01{ 0.0f };     ///< 가장자리 0 → 중심 1(볼륨 모양 기준). 표면은 1
        float32           _distanceToCenter{};      ///< 당김 중심까지의 수평 거리(m)
        float32           _immersion{ 0.0f };       ///< 유체에 잠긴 비율(0..1)
        float32           _timeInside{ 0.0f };      ///< 들어온 뒤 게임 초
        float32           _impactSpeed{ 0.0f };     ///< 부딪힌 상대 속도(m/s, 부딪힘 입력만)
        float32           _contactNormalUp{ 0.0f }; ///< 접촉 법선의 위쪽 성분(-1..1, 부딪힘 · 디딤 입력만)
        const GameObject* _pTarget{ nullptr };      ///< 값을 묻는 대상(`target.<속성>`)
    };

    /** @brief 식이 읽는 위치 값 칸입니다. */
    struct ZoneSampleSlot
    {
        using Type                      = const ZoneSample;
        static constexpr uint32 kSlotID = FourCCUtil::make( "ZSMP" );
    };

    /** @brief 매개변수 값의 형식입니다. */
    enum class ZoneParamKind : uint8
    {
        Number = 0, ///< 수나 식 — 대상마다 평가한다
        Text,       ///< 이름 · 고르기 글
        Vector,     ///< `x,y,z`
        Actions     ///< 동작 목록(`;` 로 나눈 `GameActionCall` 글)
    };

    /** @brief 처리기가 선언하는 매개변수 하나입니다. 기본값이 nullptr 이면 필수입니다. */
    struct ZoneParamSpec
    {
        const utf8*   _pName{ nullptr };
        ZoneParamKind _kind{ ZoneParamKind::Number };
        const utf8*   _pDefault{ nullptr };
    };

    /** @brief 읽을 때 형식대로 해석한 매개변수 하나입니다. */
    struct ZoneCompiledParam
    {
        hashed_string          _name{};
        ZoneParamKind          _kind{ ZoneParamKind::Number };
        ConditionExpr          _expr{};                  ///< Number
        string                 _text{};                  ///< Text(원문은 모든 형식이 든다 — 인스펙터 · 진단)
        float3                 _vector{};                ///< Vector
        vector<GameActionCall> _listCall{};              ///< Actions
        uint8                  _bExtraValue{ SW_FALSE }; ///< 처리기 표에 없는 수치 칸 — 동작이 이름으로 읽는 값(`ActionValues`)
    };

    /** @brief 경우 목록의 한 줄(조건 · 동작)입니다. */
    struct ZoneCompiledCase
    {
        hashed_string          _name{};
        ConditionExpr          _condition{};
        vector<GameActionCall> _listCall{};
    };

    /** @brief 읽을 때 검사 · 해석을 마친 모듈 하나입니다. 처리기는 이것만 읽습니다(글을 다시 해석하지 않는다). */
    class SW_GF_API ZoneCompiledModule
    {
    public:
        hashed_string               _name{};
        hashed_string               _kind{};
        const IZoneModuleProcessor* _pProcessor{ nullptr };
        ConditionExpr               _condition{};
        ZoneTargetRole              _target{ ZoneTargetRole::Occupant };
        vector<ZoneCompiledParam>   _listParam{};
        vector<ZoneCompiledCase>    _listCase{};

        /** @brief 이름의 매개변수입니다(처리기 표에 있는 이름이면 늘 있다 — 기본값으로 채웠다). */
        const ZoneCompiledParam* findParam( const hashed_string& name ) const;
        /** @brief 수치 칸을 @p context 로 평가합니다. 값이 없음(NaN — 대상에 없는 속성)이면 0 입니다(영향 없음). */
        float32 evaluateNumber( const hashed_string& name, const GameplayContext& context ) const;
        /** @brief 글 칸입니다(없는 이름이면 빈 글). */
        const string& getText( const hashed_string& name ) const;
        /** @brief 벡터 칸입니다(없는 이름이면 0). */
        float3 getVector( const hashed_string& name ) const;
        /** @brief 동작 칸입니다(없는 이름이면 빈 목록). */
        const vector<GameActionCall>& getActions( const hashed_string& name ) const;
        /** @brief 처리기 표 밖의 수치 칸(동작이 읽는 값)을 대상마다 평가해 @p outValues 에 넣습니다. */
        void evaluateActionParams( const GameplayContext& context, ActionValues& outValues ) const;
    };

    /**
     * @class ZoneCompiledProfile
     * @brief 합친 프로필(배치 덮어쓰기까지)을 처리기 표로 검사 · 해석한 한 벌입니다. 볼륨 · 표면 입력이 시작할 때 하나씩 만든다.
     */
    class SW_GF_API ZoneCompiledProfile
    {
    public:
        /**
         * @brief @p profile 을 컴파일합니다. 모르는 종류 · 매개변수, 빠진 필수, 틀린 식 · 동작, 카탈로그에 없는 이펙트 · 스킬(@p pCatalog)은 오류를 남기고 false 입니다(빈 프로필로 남는다).
         * @param sourceName 오류에 적는 이름(프로필 경로 · 배치 오브젝트)입니다.
         */
        [[nodiscard]] bool compile( const ZoneProfile& profile, string_view sourceName, const AbilityCatalog* pCatalog );

        const vector<ZoneCompiledModule>& getModules() const { return _listModule; }
        bool                              isEmpty() const { return _listModule.empty(); }
        const string&                     getSourceName() const { return _sourceName; }

    private:
        vector<ZoneCompiledModule> _listModule;
        string                     _sourceName;
    };
} // namespace sw
