/**
 * @file GuardDemoComponent.h
 * @brief AI 감각(`AIPerception`)과 록온(`LockOnSelector`)의 시연입니다 — 경비 하나가 자극 원천을 보고 듣고 기억하며, 보이는 것 가운데 하나를 쥡니다.
 * @details 자극은 씬의 공간 색인(`AIStimulusSourceComponent`)에서 직접 받고, 시야는 격자(`NavGrid`)의 벽이 가린다. 록온 후보는 지금 기억하는 대상이다 — 보이지 않게 되면 유예 뒤에,
 *          잊거나 멀어지면 바로 풀린다. 시나리오: `game/playground/automation/perception.scenario.xml` · `lockon.scenario.xml`(무대 maps/guarddemo).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/AI/AIPerception.h"
#include "GameFramework/Runtime/Character/Combat/LockOnSelector.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    /**
     * @class GuardDemoComponent
     * @brief 프레임마다 감각을 돌려 보인 · 들린 대상의 이름을 상태 칸에 적고, 록온을 유지합니다. 록온은 개발 명령 `guard.lock` · `guard.cycle` · `guard.release` 가 겁니다.
     */
    REFLECT( Category = "FeatureDemo", DisplayName = "Guard Demo", Tooltip = "Senses stimulus sources (sight, hearing, memory) and locks on to one of them" )
    class GuardDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        GuardDemoComponent();
        virtual ~GuardDemoComponent() override;

        /** @brief 보이는 대상 가운데 가장 좋은 것을 쥡니다. 없으면 풀리고 false 입니다. */
        bool lockBest();
        /** @brief 지금 대상에서 오른쪽(@p direction 1)이나 왼쪽(-1)의 다음 대상으로 넘깁니다. */
        void cycleLock( int32 direction );
        /** @brief 록온을 놓습니다. */
        void releaseLock();

        const hashed_string& getLockedTarget() const { return _lockedTarget; }

    protected:
        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;

    private:
        /** @brief 이 오브젝트의 눈 자리와 앞 방향(로컬 +Z)입니다. 씬 컴포넌트가 없으면 false 입니다. */
        [[nodiscard]] bool readEye( float3& outEye, float3& outForward ) const;
        /** @brief 기억하는 대상을 록온 후보로 옮깁니다(지금 보이는 것만 `_bVisible`). */
        void gatherLockCandidates( vector<LockOnCandidate>& outListCandidate ) const;
        /** @brief 오브젝트 id 의 이름입니다. 0 이거나 사라졌으면 빈 이름입니다. */
        [[nodiscard]] hashed_string findName( uint64 objectID ) const;
        /** @brief 록온 대상 이름을 상태 칸에 적습니다. */
        void refreshLockedTarget() { _lockedTarget = findName( _lockOn.getTarget() ); }

    private:
        PROPERTY( Category = "Perception", DisplayName = "Team", Min = 0, Max = 3, Tooltip = "Stimulus sources of this team are ignored" )
        int32 _team;
        PROPERTY( Category = "Perception", DisplayName = "Sight Range", Min = 0.0, Units = m )
        float32 _sightRange;
        PROPERTY( Category = "Perception", DisplayName = "Lose Sight Range", Min = 0.0, Units = m, Tooltip = "A target already seen stays seen up to this distance" )
        float32 _loseSightRange;
        PROPERTY( Category = "Perception", DisplayName = "Sight Half Angle" )
        Radians _sightHalfAngle;
        PROPERTY( Category = "Perception", DisplayName = "Memory Duration", Min = 0.0, Units = s )
        float32 _memoryDuration;
        PROPERTY( Category = "Perception", DisplayName = "Wall Name", Tooltip = "Objects with this base name block sight on their grid cell" )
        hashed_string _wallName;
        PROPERTY( Category = "Perception", DisplayName = "Grid Origin", Units = m )
        float3 _gridOrigin;
        PROPERTY( Category = "Perception", DisplayName = "Grid Size", Min = 1, Tooltip = "Cells along each side of the sight grid" )
        int32 _gridSize;
        PROPERTY( Category = "Perception", DisplayName = "Grid Cell Size", Min = 0.01, Units = m )
        float32 _gridCellSize;
        PROPERTY( Category = "Lock On", DisplayName = "Max Distance", Min = 0.01, Units = m )
        float32 _lockMaxDistance;
        PROPERTY( Category = "Lock On", DisplayName = "Break Distance", Min = 0.01, Units = m )
        float32 _lockBreakDistance;
        PROPERTY( Category = "Lock On", DisplayName = "Lost Sight Grace", Min = 0.0, Units = s )
        float32 _lockLostSightGrace;

        PROPERTY( RuntimeState )
        hashed_string _seenTarget; ///< 지금 보이는 대상 가운데 가장 가까운 것(없으면 빈 이름)
        PROPERTY( RuntimeState )
        hashed_string _heardTarget; ///< 이번에 들린 대상(없으면 빈 이름)
        PROPERTY( RuntimeState )
        hashed_string _lockedTarget; ///< 쥔 대상(없으면 빈 이름)
        PROPERTY( RuntimeState, Min = 0 )
        int32 _rememberedCount; ///< 기억하는 대상 수(보이지 않아도 기억 시간 안이면 센다)

        SW_TRANSIENT AIPerception   _perception;
        SW_TRANSIENT LockOnSelector _lockOn;
        SW_TRANSIENT NavGrid        _sightGrid;
    };
} // namespace sw
