/**
 * @file ZoneModule.h
 * @brief 지역 모듈의 처리기 계약 · 등록과, 읽을 때 컴파일한 프로필(`ZoneCompiledProfile`)입니다. 처리기는 모듈 종류마다 하나이고 서로를 모릅니다.
 * @details 처리기는 자기 매개변수 표(이름 · 형식 · 기본값)를 선언하고, 읽기가 그 표로 프로필을 검사한다 — 모르는 매개변수 · 빠진 필수 · 틀린 식 · 모르는 동작 · 카탈로그에 없는
 *          이펙트 · 스킬은 로드 실패다. 수치 칸은 조건식과 같은 문법 · 문맥의 식이다(`ConditionExpr` 의 산술): 대상 속성(`target.FireResist`), 태그(`tag(State.Fireproof)`),
 *          위치 값(`zoneDepth01` · `distanceToCenter` · `immersion` · `timeInside`), 접촉 세기(`impactSpeed` · `contactNormal`), 날씨 · 플래그.
 *          처리기는 사건(진입 · 체류 · 이탈)을 받고, 조건 전환(조건이 참/거짓이 되는 순간)은 처리기의 기억(`ZoneModuleMemory`)으로 낸다. 같은 처리 길을 볼륨 · 표면 · 런타임 영역이
 *          모두 지난다(`ZoneTracker`). 새 효과는 처리기 하나 — `SW_ZONE_MODULE` 한 줄로 오른다(언리얼 GAS 이펙트 Component · 유니티 Volume Override 의 자리, 기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Behavior/GameplayContext.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneProfile.h"

namespace sw
{
    class AbilityCatalog;
    class GameObject;
    class GameObjectManager;
    class ZoneModuleProcessor;

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
        hashed_string              _name{};
        hashed_string              _kind{};
        const ZoneModuleProcessor* _pProcessor{ nullptr };
        ConditionExpr              _condition{};
        ZoneTargetRole             _target{ ZoneTargetRole::Occupant };
        vector<ZoneCompiledParam>  _listParam{};
        vector<ZoneCompiledCase>   _listCase{};

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

namespace sw
{
    /** @brief 처리기가 대상 · 모듈 하나마다 드는 작은 기억입니다(주기 · 조건 전환 · 지연). */
    struct ZoneModuleMemory
    {
        float32 _timer{ 0.0f };        ///< 다음 주기까지 남은 초
        float32 _heldSeconds{ 0.0f };  ///< 조건이 이어서 참이었던 초
        uint8   _bWasTrue{ SW_FALSE }; ///< 지난 체류에서 조건이 참이었다
        uint8   _bFired{ SW_FALSE };   ///< 이번 참 구간에 전환 할 일을 했다
        uint8   _arrReserved[2]{};
    };

    /** @brief 처리기 사건 하나의 문맥입니다. 대상 역할을 푼 대상마다 하나입니다. */
    struct ZoneEventContext
    {
        const ZoneCompiledModule* _pModule{ nullptr };
        ZoneModuleMemory*         _pMemory{ nullptr };
        GameObject*               _pTarget{ nullptr };   ///< 역할을 푼 대상
        GameObject*               _pOccupant{ nullptr }; ///< 들어온 개체
        GameObjectManager*        _pManager{ nullptr };
        const ZoneSample*         _pSample{ nullptr };
        GameplayContext           _world{};                   ///< 위치 값 · 대상 태그 · 플래그 · 블랙보드 · 출처 · 대상 · 값 칸
        uint8                     _bConditionTrue{ SW_TRUE }; ///< 모듈 조건(`_condition`)이 이 대상에 참이다

        /** @brief 동작 목록을 이 문맥으로 부릅니다(대상 · 출처 · 값 칸이 든다). */
        void invoke( const vector<GameActionCall>& listCall ) const;
        /** @brief 경우 목록이 있으면 처음 맞는 경우의 동작을, 없으면 @p listDefault 를 부릅니다. */
        void runFirstMatchingCase( const vector<GameActionCall>& listDefault ) const;
    };

    /**
     * @class ZoneModuleProcessor
     * @brief 모듈 종류 하나의 처리기입니다. 정적 객체 하나가 `SW_ZONE_MODULE` 로 오르고, 상태는 갖지 않습니다(대상마다의 기억은 `ZoneModuleMemory`).
     */
    class SW_GF_API ZoneModuleProcessor
    {
    public:
        virtual ~ZoneModuleProcessor() = default;

        /** @brief 데이터가 적는 종류 이름입니다(`_kind`). */
        virtual const utf8* getKindName() const = 0;
        /** @brief 매개변수 표입니다. */
        virtual void collectParamSpecs( vector<ZoneParamSpec>& outListSpec ) const = 0;
        /** @brief 표 밖의 수치 칸을 동작 값으로 받으면 true 입니다(할 일 모듈). 아니면 모르는 이름은 오류다. */
        virtual bool supportsActionParams() const { return false; }
        /** @brief 경우 목록을 받으면 true 입니다. */
        virtual bool supportsCaseList() const { return false; }
        /** @brief 읽은 모듈의 칸 사이 규칙(고르기 글의 값 · 범위)을 봅니다. 틀리면 까닭을 오류 로그로 남기고 false 입니다(로드 실패). */
        virtual bool checkModule( const ZoneCompiledModule& module, string_view sourceName ) const
        {
            (void)module;
            (void)sourceName;
            return true;
        }

        /** @brief 입력(볼륨 · 표면)이 시작할 때 한 번입니다(대상과 상관없는 일 — 내비 비용). */
        virtual void onSourceBegin( GameObjectManager& manager, const ZoneCompiledModule& module ) const
        {
            (void)manager;
            (void)module;
        }
        /** @brief 대상이 들어왔습니다. */
        virtual void onEnter( ZoneEventContext& context ) const { (void)context; }
        /** @brief 대상이 안에 있는 동안 프레임마다입니다(게임 초 @p deltaTime). */
        virtual void onStay( ZoneEventContext& context, float32 deltaTime ) const
        {
            (void)context;
            (void)deltaTime;
        }
        /** @brief 대상이 나갔습니다(입력이 끝나도 남은 대상마다 불린다). */
        virtual void onExit( ZoneEventContext& context ) const { (void)context; }
    };

    /**
     * @struct ZoneModuleRegistry
     * @brief 종류 이름 → 처리기 표입니다. 등록은 모듈을 올리는 스레드에서, 찾기는 프로필을 읽는 스레드에서 오므로 잠금으로 지킵니다(`ConditionRegistry` 와 같은 규칙).
     */
    struct SW_GF_API ZoneModuleRegistry
    {
        static void registerProcessor( const ZoneModuleProcessor* pProcessor );
        static void unregisterProcessor( const ZoneModuleProcessor* pProcessor );
        /** @brief 종류 이름의 처리기입니다. 없으면 nullptr 입니다. */
        static const ZoneModuleProcessor* find( string_view kindName );
        /** @brief 등록된 종류 이름을 ", " 로 이은 글입니다(모르는 종류 오류 · 편집기). */
        static string joinNames();
    };

    /** @brief `SW_ZONE_MODULE` 이 두는 정적 등록자입니다. */
    class SW_GF_API ZoneModuleRegistrar
    {
    public:
        explicit ZoneModuleRegistrar( const ZoneModuleProcessor* pProcessor );
        ~ZoneModuleRegistrar();
        ZoneModuleRegistrar( const ZoneModuleRegistrar& )            = delete;
        ZoneModuleRegistrar& operator=( const ZoneModuleRegistrar& ) = delete;

    private:
        const ZoneModuleProcessor* _pProcessor;
    };
} // namespace sw

/**
 * @brief 처리기 @p TProcessor(기본 생성자 · `ZoneModuleProcessor` 파생)를 그 .cpp 에서 등록합니다(`namespace sw` 안, 파일 범위).
 * @param varName 파일 안에서 유일한 이름 조각
 */
#define SW_ZONE_MODULE( varName, TProcessor )                               \
    static const TProcessor                sw_zoneModule_##varName{};       \
    static const ::sw::ZoneModuleRegistrar sw_zoneModuleRegistrar_##varName \
    {                                                                       \
        &sw_zoneModule_##varName                                            \
    }
