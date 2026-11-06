#include "pch.h"

#include "GameFramework/Runtime/World/Query/GameConditions.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Runtime/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Runtime/World/Query/GameFlags.h"

namespace sw
{
    namespace
    {
        struct GameConditionsInternal
        {
            static constexpr uint32 kChanceSalt = 0x5c4ed01eu; ///< 스케줄의 저장 · 재생 결과를 지키려면 바꾸지 않는다

            static float64 readFlag( const GameplayContext& context, const ConditionNameArg& args )
            {
                const GameFlags* pFlags = context.find<GameFlagsSlot>();
                return pFlags != nullptr ? static_cast<float64>( pFlags->getFlag( args._name ) ) : 0.0;
            }

            static bool hasTag( const GameplayContext& context, const ConditionTagArg& args )
            {
                const TagContainer* pSubject = context.find<SubjectTagsSlot>();
                const TagContainer* pWorld   = context.find<WorldTagsSlot>();
                const bool          bSubject = pSubject != nullptr && pSubject->hasTag( args._tag );
                const bool          bWorld   = pWorld != nullptr && pWorld->hasTag( args._tag );
                return bSubject || bWorld;
            }

            static bool rollChance( const GameplayContext& context, const ConditionNumberArg& args )
            {
                return GameHash::toUnitFloat( context.getSeed() ) < args._value; // shortcut: 한 식 안의 chance 는 한 굴림을 나눈다. 독립 굴림이 필요하면 호출 자리 번호를 씨앗에 섞는다
            }

            static bool hasBodyShape( const GameplayContext& context, const ConditionNameListArgs& args )
            {
                const hashed_string* pShape = context.find<BodyShapeSlot>();
                return pShape != nullptr && VectorUtil::contains( args._listName, *pShape );
            }

            static bool hasBodyType( const GameplayContext& context, const ConditionNameListArgs& args )
            {
                const hashed_string* pType = context.find<BodyTypeSlot>();
                return pType != nullptr && VectorUtil::contains( args._listName, *pType );
            }

            static ActionResult runSetFlag( ActionContext& context, const GameFlagSetParams& params )
            {
                GameFlags* pFlags = context._world.find<GameFlagsWriteSlot>();
                if ( pFlags == nullptr )
                    return ActionResult::Failed;
                pFlags->setFlag( params._flag, params._value );
                return ActionResult::Succeeded;
            }

            static ActionResult runAddFlag( ActionContext& context, const GameFlagAddParams& params )
            {
                GameFlags* pFlags = context._world.find<GameFlagsWriteSlot>();
                if ( pFlags == nullptr )
                    return ActionResult::Failed;
                (void)pFlags->addFlag( params._flag, params._delta ); // 새 값은 쓰지 않는다
                return ActionResult::Succeeded;
            }

            static ActionResult runEvent( ActionContext& /*context*/, const GameEventParams& /*params*/ )
            {
                return ActionResult::Succeeded; // 실행기가 호출 글을 알림으로 내보낸다 — 이 동작 자체는 세계를 바꾸지 않는다
            }
        };
    } // namespace

    uint32 GameConditions::makeSeed( int32 first, int32 second )
    {
        return GameHash::hashCoord( first, second, GameConditionsInternal::kChanceSalt );
    }

    SW_CONDITION( flag, ConditionNameArg, &GameConditionsInternal::readFlag );
    SW_CONDITION( tag, ConditionTagArg, &GameConditionsInternal::hasTag );
    SW_CONDITION( chance, ConditionNumberArg, &GameConditionsInternal::rollChance );
    SW_CONDITION( bodyShape, ConditionNameListArgs, &GameConditionsInternal::hasBodyShape );
    SW_CONDITION( bodyType, ConditionNameListArgs, &GameConditionsInternal::hasBodyType );

    SW_GAME_ACTION( FlagSet, "flag.set", GameFlagSetParams, &GameConditionsInternal::runSetFlag );
    SW_GAME_ACTION( FlagAdd, "flag.add", GameFlagAddParams, &GameConditionsInternal::runAddFlag );
    SW_GAME_ACTION( GameEvent, "event", GameEventParams, &GameConditionsInternal::runEvent );
} // namespace sw
