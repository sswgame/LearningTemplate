/**
 * @file ZoneModules.cpp
 * @brief 기본 모듈 종류(처리기) — 할 일(`Action`), 물리 필드(`PhysicsField`), NPC 판단 질의(`Blackboard`), 내비 비용(`NavArea`), 표면 채널(`SurfaceChannel`),
 *        표면 도장(`SurfaceStamp` — 변형 층의 파묻힘 테두리 · 깔때기 · 끌린 자국).
 * @details 처리기는 서로를 모른다. 가라앉음 · 감속 · 화상 · 버프 · 스킬 · 즉사 · 이동 · 규칙 신호는 모두 `Action` 의 동작 어휘(이펙트가 정본 — 스택 · 속성 · 태그)이고,
 *          무중력 · 부력 · 해류 · 당김 · 소용돌이 · 빙판은 `PhysicsField` 의 값이다. 외형 내리기 · 점프 탈출은 대상 쪽 반응 컴포넌트가 속성 · 태그만 읽어 한다
 *          (`VisualSinkReactionComponent` · `JumpEscapeReactionComponent` · `ScreenReactionComponent`). 새 효과는 처리기 하나를 더한다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Environment/Surface/SurfaceStamp.h"
#include "Engine/Navigation/Component/SceneNavigation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/PhysicsField.h"

#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneModule.h"

namespace sw
{
    namespace
    {
        struct ZoneModulesInternal
        {
            static bool isOneOf( const string& text, std::initializer_list<const utf8*> listAllowed )
            {
                for ( const utf8* pAllowed : listAllowed )
                {
                    if ( text == pAllowed )
                        return true;
                }
                return false;
            }

            static bool checkChoice( const ZoneCompiledModule& module, const utf8* pParam, std::initializer_list<const utf8*> listAllowed, string_view sourceName )
            {
                const string& text = module.getText( hashed_string( pParam ) );
                if ( isOneOf( text, listAllowed ) )
                    return true;
                SW_LOG_ERROR_ID( "Zone.ChoiceInvalid", "%# module '%#': %# '%#' is not one of the allowed values", sourceName, module._name.c_str(), pParam, text.c_str() );
                return false;
            }
        };

        /**
         * @brief 할 일입니다 — 사건(진입 · 체류 주기 · 이탈 · 조건이 참이 됨 · 거짓이 됨)마다 동작 목록이나 첫 맞는 경우의 동작을 부릅니다.
         * @details `on="Became"` 은 조건이 `delay` 초 이어서 참이면 한 번(사구 중심 2 초 → 삼켜짐), `Lost` 는 참이던 조건이 거짓이 되거나 나갈 때 한 번(끌려감 해제)이다.
         *          표 밖의 수치 칸은 동작이 이름으로 읽는 값이다(`value=damage` 의 `damage="20 * (1 - target.FireResist)"`).
         */
        class ZoneActionProcessor final : public ZoneModuleProcessor
        {
        public:
            const utf8* getKindName() const override { return "Action"; }
            void        collectParamSpecs( vector<ZoneParamSpec>& outListSpec ) const override
            {
                outListSpec.push_back( ZoneParamSpec{ "on", ZoneParamKind::Text, "Enter" } );
                outListSpec.push_back( ZoneParamSpec{ "interval", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "delay", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "do", ZoneParamKind::Actions, "" } );
            }
            bool supportsActionParams() const override { return true; }
            bool supportsCaseList() const override { return true; }
            bool checkModule( const ZoneCompiledModule& module, string_view sourceName ) const override
            {
                return ZoneModulesInternal::checkChoice( module, "on", { "Enter", "Stay", "Exit", "Became", "Lost" }, sourceName );
            }

            void onEnter( ZoneEventContext& context ) const override
            {
                context._pMemory->_timer = 0.0f; // 체류 주기는 들어온 프레임에 한 번 하고 시작한다
                if ( readOn( context ) == "Enter" && context._bConditionTrue == SW_TRUE )
                    run( context );
            }

            void onStay( ZoneEventContext& context, float32 deltaTime ) const override
            {
                const string&     on     = readOn( context );
                ZoneModuleMemory& memory = *context._pMemory;
                if ( on == "Stay" )
                {
                    memory._timer -= deltaTime;
                    if ( memory._timer > 0.0f )
                        return;
                    const float32 interval = context._pModule->evaluateNumber( hashed_string( "interval" ), context._world );
                    memory._timer          = interval > 0.0f ? memory._timer + interval : 0.0f;
                    if ( memory._timer < 0.0f )
                        memory._timer = 0.0f; // 긴 프레임에 밀린 주기는 따라잡지 않는다(한 번만 한다)
                    if ( context._bConditionTrue == SW_TRUE )
                        run( context );
                    return;
                }
                if ( on == "Became" )
                {
                    if ( context._bConditionTrue == SW_FALSE )
                    {
                        memory._heldSeconds = 0.0f;
                        memory._bFired      = SW_FALSE;
                        return;
                    }
                    memory._heldSeconds += deltaTime;
                    const float32 delay = context._pModule->evaluateNumber( hashed_string( "delay" ), context._world );
                    if ( memory._bFired == SW_FALSE && memory._heldSeconds >= delay )
                    {
                        memory._bFired = SW_TRUE;
                        run( context );
                    }
                    return;
                }
                if ( on == "Lost" )
                {
                    if ( context._bConditionTrue == SW_TRUE )
                    {
                        memory._bWasTrue = SW_TRUE;
                        return;
                    }
                    if ( memory._bWasTrue == SW_TRUE )
                    {
                        memory._bWasTrue = SW_FALSE;
                        run( context );
                    }
                }
            }

            void onExit( ZoneEventContext& context ) const override
            {
                const string& on = readOn( context );
                // 나가는 순간도 참 구간의 끝이다 — `Lost` 는 참이었으면 한 번 한다(다시 나오는 순간 해제).
                const bool bLost = on == "Lost" && context._pMemory->_bWasTrue == SW_TRUE;
                if ( ( on == "Exit" && context._bConditionTrue == SW_TRUE ) || bLost )
                    run( context );
                context._pMemory->_bWasTrue = SW_FALSE;
            }

        private:
            static const string& readOn( const ZoneEventContext& context ) { return context._pModule->getText( hashed_string( "on" ) ); }
            static void          run( const ZoneEventContext& context ) { context.runFirstMatchingCase( context._pModule->getActions( hashed_string( "do" ) ) ); }
        };

        /**
         * @brief 물리 필드입니다 — 안의 모든 물리 바디(캐릭터 · 상자 · 파편)에 중력 배율 · 가속 · 흐름 · 유체 저항 · 부력 · 접지를 줍니다.
         * @details 방향 종류(균일 · 중심으로 당김 · 회전 · 흐름) × 세기(위치 값의 식 — 곡선)다. 소용돌이 · 블랙홀 · 강물은 방향 종류와 식만 다르다.
         *          물체마다 뜨고 가라앉음은 바디 쪽 밀도가 정한다(유체 밀도 > 바디 밀도면 뜬다 — 나무 상자는 뜨고 돌은 가라앉는다).
         */
        class ZonePhysicsFieldProcessor final : public ZoneModuleProcessor
        {
        public:
            const utf8* getKindName() const override { return "PhysicsField"; }
            void        collectParamSpecs( vector<ZoneParamSpec>& outListSpec ) const override
            {
                outListSpec.push_back( ZoneParamSpec{ "direction", ZoneParamKind::Text, "Uniform" } );
                outListSpec.push_back( ZoneParamSpec{ "axis", ZoneParamKind::Vector, "0,0,0" } );
                outListSpec.push_back( ZoneParamSpec{ "strength", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "flowSpeed", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "gravityScale", ZoneParamKind::Number, "1" } );
                outListSpec.push_back( ZoneParamSpec{ "drag", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "fluidDensity", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "friction", ZoneParamKind::Number, "1" } );
            }
            bool checkModule( const ZoneCompiledModule& module, string_view sourceName ) const override
            {
                return ZoneModulesInternal::checkChoice( module, "direction", { "Uniform", "ToCenter", "Swirl", "Flow" }, sourceName );
            }

            void onStay( ZoneEventContext& context, float32 deltaTime ) const override
            {
                (void)deltaTime;
                if ( context._bConditionTrue == SW_FALSE )
                    return;
                const ZoneCompiledModule& module = *context._pModule;
                const GameplayContext&    world  = context._world;
                const float3              dir    = computeDirection( module, *context._pSample );
                PhysicsFieldSample        field;
                field._acceleration = dir * module.evaluateNumber( hashed_string( "strength" ), world );
                field._flowVelocity = dir * module.evaluateNumber( hashed_string( "flowSpeed" ), world );
                field._gravityScale = module.evaluateNumber( hashed_string( "gravityScale" ), world );
                field._drag         = MathUtil::max( 0.0f, module.evaluateNumber( hashed_string( "drag" ), world ) );
                field._fluidDensity = MathUtil::max( 0.0f, module.evaluateNumber( hashed_string( "fluidDensity" ), world ) );
                field._friction     = MathUtil::saturate( module.evaluateNumber( hashed_string( "friction" ), world ) );
                field._immersion    = context._pSample->_immersion;
                if ( CharacterControllerComponent* pController = context._pTarget->getComponent<CharacterControllerComponent>() )
                    pController->applyPhysicsField( field );
                if ( RigidBodyComponent* pBody = context._pTarget->getComponent<RigidBodyComponent>() )
                    pBody->applyPhysicsField( field );
            }

        private:
            /** @brief 대상 자리의 방향(단위)입니다. 정할 수 없으면(중심 위 · 축 0 · 흐름 없음) 0 입니다. */
            static float3 computeDirection( const ZoneCompiledModule& module, const ZoneSample& sample )
            {
                const string& kind     = module.getText( hashed_string( "direction" ) );
                float3        toCenter = sample._center - sample._position;
                toCenter._y            = 0.0f;
                const float32 distance = toCenter.getLength();
                const float3  inward   = distance > kMinDistance ? toCenter * ( 1.0f / distance ) : float3{};
                if ( kind == "ToCenter" )
                    return inward;
                if ( kind == "Swirl" )
                    return float3{ inward._z, 0.0f, -inward._x }; // 위 축 둘레의 접선(위에서 보면 시계 반대)
                if ( kind == "Flow" )
                    return sample._flowDirection;
                const float3  axis   = module.getVector( hashed_string( "axis" ) );
                const float32 length = axis.getLength();
                return length > 0.0f ? axis * ( 1.0f / length ) : float3{};
            }

            static constexpr float32 kMinDistance = 0.01f; ///< 중심에서 이보다 가까우면 당길 방향이 없다
        };

        /**
         * @brief NPC 의 판단 질의입니다 — 안에 있는 동안 대상 AI 의 블랙보드 키에 값을 두고, 나가면 지웁니다. 행동 트리 · 스케줄이 "지금 어느 지역 · 무엇을 받나" 를 이 키로 읽는다.
         */
        class ZoneBlackboardProcessor final : public ZoneModuleProcessor
        {
        public:
            const utf8* getKindName() const override { return "Blackboard"; }
            void        collectParamSpecs( vector<ZoneParamSpec>& outListSpec ) const override
            {
                outListSpec.push_back( ZoneParamSpec{ "key", ZoneParamKind::Text, nullptr } );
                outListSpec.push_back( ZoneParamSpec{ "value", ZoneParamKind::Number, "1" } );
            }

            void onStay( ZoneEventContext& context, float32 deltaTime ) const override
            {
                (void)deltaTime;
                Blackboard* const pBoard = context._world.find<BlackboardWriteSlot>();
                if ( pBoard == nullptr )
                    return; // AI 가 없는 대상(플레이어 · 물체)은 할 일이 없다
                const hashed_string key( context._pModule->getText( hashed_string( "key" ) ).c_str() );
                if ( context._bConditionTrue == SW_TRUE )
                    pBoard->setFloat( key, context._pModule->evaluateNumber( hashed_string( "value" ), context._world ) );
                else
                    pBoard->clearValue( key );
            }

            void onExit( ZoneEventContext& context ) const override
            {
                if ( Blackboard* const pBoard = context._world.find<BlackboardWriteSlot>() )
                    pBoard->clearValue( hashed_string( context._pModule->getText( hashed_string( "key" ) ).c_str() ) );
            }
        };

        /**
         * @brief 내비 비용입니다 — 에이전트 종류(행위자 종류)마다 이 지역의 내비 영역을 지나는 비용 · 통행 금지를 정합니다(일반 NPC 는 용암을 돌아가고 화염 정령은 지난다).
         * @details 영역 표시는 지역 아래 기하의 `NavMeshModifierComponent` 가 하고(베이크), 비용 · 금지는 이 모듈이 씬의 거름에 건다(`SceneNavigation::setAgentAreaCost`).
         *          대상과 상관없는 일이라 입력이 시작할 때 한 번 한다.
         */
        class ZoneNavAreaProcessor final : public ZoneModuleProcessor
        {
        public:
            const utf8* getKindName() const override { return "NavArea"; }
            void        collectParamSpecs( vector<ZoneParamSpec>& outListSpec ) const override
            {
                outListSpec.push_back( ZoneParamSpec{ "area", ZoneParamKind::Text, nullptr } );
                outListSpec.push_back( ZoneParamSpec{ "agents", ZoneParamKind::Text, "" } );
                outListSpec.push_back( ZoneParamSpec{ "cost", ZoneParamKind::Number, "1" } );
                outListSpec.push_back( ZoneParamSpec{ "blocked", ZoneParamKind::Number, "0" } );
            }

            void onSourceBegin( GameObjectManager& manager, const ZoneCompiledModule& module ) const override
            {
                const GameplayContext empty;
                const hashed_string   area( module.getText( hashed_string( "area" ) ).c_str() );
                const float32         cost     = module.evaluateNumber( hashed_string( "cost" ), empty );
                const bool            bBlocked = module.evaluateNumber( hashed_string( "blocked" ), empty ) != 0.0f;
                SceneNavigation&      nav      = manager.getSubsystem<SceneNavigation>();
                const string&         agents   = module.getText( hashed_string( "agents" ) );
                if ( agents.empty() )
                {
                    (void)nav.setAgentAreaCost( hashed_string{}, area, cost, bBlocked == false ); // 빈 이름은 기본 종류 — 모르는 영역은 내비가 알린다
                    return;
                }
                for ( const string_view agent : StringUtil::split( agents, ",", StringSplitOptions{ true, true } ) )
                {
                    (void)nav.setAgentAreaCost( hashed_string( string( agent ).c_str() ), area, cost, bBlocked == false ); // 모르는 종류는 내비가 알린다
                }
            }
        };

        /** @brief 표면 채널입니다 — 안에 있는 동안 외형의 몸 표면 채널(젖음 · 진흙 · 그을음)을 초당 `rate` 만큼 올립니다(음수면 내린다). */
        class ZoneSurfaceChannelProcessor final : public ZoneModuleProcessor
        {
        public:
            const utf8* getKindName() const override { return "SurfaceChannel"; }
            void        collectParamSpecs( vector<ZoneParamSpec>& outListSpec ) const override
            {
                outListSpec.push_back( ZoneParamSpec{ "channel", ZoneParamKind::Text, nullptr } );
                outListSpec.push_back( ZoneParamSpec{ "rate", ZoneParamKind::Number, nullptr } );
            }

            void onStay( ZoneEventContext& context, float32 deltaTime ) const override
            {
                CharacterAppearanceComponent* const pAppearance = context._pTarget->getComponent<CharacterAppearanceComponent>();
                if ( context._bConditionTrue == SW_FALSE || pAppearance == nullptr || pAppearance->hasSurfaceState() == false )
                    return; // 표면 채널이 없는 대상(물체 · 외형을 끈 캐릭터)은 할 일이 없다
                const hashed_string    channel( context._pModule->getText( hashed_string( "channel" ) ).c_str() );
                const float32          amount = context._pModule->evaluateNumber( hashed_string( "rate" ), context._world ) * deltaTime;
                CharacterSurfaceState& state  = pAppearance->getSurfaceState();
                bool                   bKnown = true;
                for ( uint32 region = 0; region < state.getRegionCount(); ++region )
                {
                    bKnown = state.addRegionValue( region, channel, amount ) && bKnown;
                }
                if ( bKnown == false )
                    SW_LOG_ERROR_ONCE_ID( "Zone.SurfaceChannelUnknown", "Zone module '%#': surface channel '%#' is not in '%#' appearance channels", context._pModule->_name.c_str(), channel.c_str(),
                                          context._pTarget->getName().c_str() );
            }
        };

        /**
         * @brief 표면 도장입니다 — 안에 있는 동안 표면 변형 층(`SurfaceDeformSystem`)에 도장을 찍습니다: 대상 둘레의 파묻힘 테두리, 중심의 빨려드는 깔때기, 끌린 자국.
         * @details 도장 칸(`shape` · `size` · `depth` · `rimHeight` · `rimWidth` · `fillSpeed` · `spinSpeed` · `armCount`)은 `SurfaceStampDef` 와 같고, 수치 칸은 식이라
         *          위치 값을 읽는다(`depth="0.12 * zoneDepth01"` — 안쪽일수록 깊은 깔때기). 자리는 `at`(대상 발 `Target` · 당김 중심 `Center`)이고 `interval` 초마다(0 = 매 프레임) 찍는다.
         *          매 프레임 다시 찍는 도장은 붙잡은 도장과 같다 — 머무는 동안 메워지지 않고, 나가면 자기 속도로 메워진다(처리기는 상태를 갖지 않아 핸들을 들지 않는다).
         *          깔때기 홈은 머문 시간 × `spinSpeed` 만큼 돈다. 충돌 · 내비는 고치지 않는다(보이는 것만). 눈 · 모래 차이는 프리셋의 칸이다(눈 깊고 느리게, 모래 얕고 빠르게).
         */
        class ZoneSurfaceStampProcessor final : public ZoneModuleProcessor
        {
        public:
            const utf8* getKindName() const override { return "SurfaceStamp"; }
            void        collectParamSpecs( vector<ZoneParamSpec>& outListSpec ) const override
            {
                outListSpec.push_back( ZoneParamSpec{ "shape", ZoneParamKind::Text, "Print" } );
                outListSpec.push_back( ZoneParamSpec{ "size", ZoneParamKind::Vector, nullptr } );
                outListSpec.push_back( ZoneParamSpec{ "depth", ZoneParamKind::Number, nullptr } );
                outListSpec.push_back( ZoneParamSpec{ "rimHeight", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "rimWidth", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "fillSpeed", ZoneParamKind::Number, nullptr } );
                outListSpec.push_back( ZoneParamSpec{ "spinSpeed", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "armCount", ZoneParamKind::Number, "0" } );
                outListSpec.push_back( ZoneParamSpec{ "at", ZoneParamKind::Text, "Target" } );
                outListSpec.push_back( ZoneParamSpec{ "interval", ZoneParamKind::Number, "0" } );
            }
            bool checkModule( const ZoneCompiledModule& module, string_view sourceName ) const override
            {
                bool bOk = ZoneModulesInternal::checkChoice( module, "shape", { "Print", "Trail", "Funnel" }, sourceName );
                bOk      = ZoneModulesInternal::checkChoice( module, "at", { "Target", "Center" }, sourceName ) && bOk;
                // shortcut: 칸 사이 규칙(크기 · 메워지는 속도)은 대상 없는 문맥으로 평가해 본다 — 대상 속성만 읽는 식은 0 으로 보여 오류가 된다. 그런 식이 필요해지면 찍을 때 검사로 옮긴다.
                return bOk && makeStamp( module, GameplayContext{} ).validate( sourceName, module._name.c_str() );
            }

            void onEnter( ZoneEventContext& context ) const override { context._pMemory->_timer = 0.0f; } // 들어온 프레임에 찍고 시작한다

            void onStay( ZoneEventContext& context, float32 deltaTime ) const override
            {
                if ( context._bConditionTrue == SW_FALSE )
                    return;
                ZoneModuleMemory& memory = *context._pMemory;
                memory._timer -= deltaTime;
                if ( memory._timer > 0.0f )
                    return;
                const ZoneCompiledModule& module   = *context._pModule;
                const float32             interval = module.evaluateNumber( hashed_string( "interval" ), context._world );
                memory._timer                      = interval > 0.0f ? MathUtil::max( 0.0f, memory._timer + interval ) : 0.0f; // 밀린 주기는 따라잡지 않는다
                const SurfaceStampDef stamp        = makeStamp( module, context._world );
                const ZoneSample&     sample       = *context._pSample;
                const bool            bAtCenter    = module.getText( hashed_string( "at" ) ) == "Center";
                // 깔때기는 머문 시간만큼 홈을 돌리고, 나머지는 대상이 보는 쪽으로 눕힌다(끌린 자국 · 발 모양).
                float32 yaw = sample._timeInside * stamp._spinSpeed;
                if ( stamp._shape != SurfaceStampShape::Funnel )
                {
                    const SceneComponent* const pRoot = context._pTarget->getPrimarySceneComponent();
                    yaw                               = pRoot != nullptr ? pRoot->getLocalRotation()._y : 0.0f;
                }
                context._pManager->getSubsystem<SurfaceDeformSystem>().stamp( stamp, bAtCenter ? sample._center : sample._position, yaw );
            }

        private:
            /** @brief 모듈 칸을 @p world 로 평가한 도장입니다. */
            static SurfaceStampDef makeStamp( const ZoneCompiledModule& module, const GameplayContext& world )
            {
                const string&   shape = module.getText( hashed_string( "shape" ) );
                const float3    size  = module.getVector( hashed_string( "size" ) );
                SurfaceStampDef stamp;
                stamp._shape     = shape == "Funnel" ? SurfaceStampShape::Funnel : ( shape == "Trail" ? SurfaceStampShape::Trail : SurfaceStampShape::Print );
                stamp._size      = float2{ size._x, size._y };
                stamp._depth     = MathUtil::max( 0.0f, module.evaluateNumber( hashed_string( "depth" ), world ) );
                stamp._rimHeight = MathUtil::max( 0.0f, module.evaluateNumber( hashed_string( "rimHeight" ), world ) );
                stamp._rimWidth  = MathUtil::max( 0.0f, module.evaluateNumber( hashed_string( "rimWidth" ), world ) );
                stamp._fillSpeed = MathUtil::max( 0.0f, module.evaluateNumber( hashed_string( "fillSpeed" ), world ) );
                stamp._spinSpeed = module.evaluateNumber( hashed_string( "spinSpeed" ), world );
                stamp._armCount  = static_cast<uint32>( MathUtil::clamp( module.evaluateNumber( hashed_string( "armCount" ), world ), 0.0f, kMaxArmCount ) );
                return stamp;
            }

            static constexpr float32 kMaxArmCount = 16.0f; ///< 깔때기 홈 수 상한(`SurfaceStampDef::_armCount` 의 PROPERTY Max 와 같다)
        };
    } // namespace

    SW_ZONE_MODULE( Action, ZoneActionProcessor );
    SW_ZONE_MODULE( PhysicsField, ZonePhysicsFieldProcessor );
    SW_ZONE_MODULE( Blackboard, ZoneBlackboardProcessor );
    SW_ZONE_MODULE( NavArea, ZoneNavAreaProcessor );
    SW_ZONE_MODULE( SurfaceChannel, ZoneSurfaceChannelProcessor );
    SW_ZONE_MODULE( SurfaceStamp, ZoneSurfaceStampProcessor );
} // namespace sw
