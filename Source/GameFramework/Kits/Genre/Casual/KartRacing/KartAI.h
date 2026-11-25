/**
 * @file KartAI.h
 * @brief 카트 AI 운전을 구현합니다. 중심선 앞을 내다보며 곡선 안쪽으로 붙는 레이싱 라인, 굽은 길의 드리프트, 아이템을 쓸 시점의 판단을 담당합니다.
 * @details 러버밴딩(1등과의 거리에 따른 최고 속도 배율)은 순위를 아는 `KartRace` 가 건다. 운전은 속도 상한을 모른다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/KartItems.h"
#include "GameFramework/Runtime/Character/Movement/KinematicVehicleMotor.h"

namespace sw
{
    class KartTrack;

    /**
     * @struct KartAISettings
     * @brief AI 운전 수치입니다. 각의 단위는 라디안입니다. 값은 데이터(디렉터의 PROPERTY)에 있고 코드에는 기본값이 없다.
     * @details 적지 않은 칸은 0 이라 경기를 열 때 거절한다(`isSet`). 레이싱 라인 · 늦춘 페달 · 아이템 지연은 0 도 뜻이 있다.
     */
    REFLECT()
    struct SW_KIT_API KartAISettings
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.0, Units = m )
        float32 _lookAheadBase{ 0.0f }; ///< 겨누는 점까지의 기본 거리(m)
        PROPERTY( Min = 0.0 )
        float32 _lookAheadPerSpeed{ 0.0f }; ///< 속도 1 m/초마다 더 멀리 본다
        PROPERTY( Min = 0.0 )
        float32 _steerGain{ 0.0f }; ///< 방향 오차(라디안)를 조향으로 바꾸는 배율이다
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _racingLine{ 0.0f }; ///< 곡선 안쪽으로 붙는 정도이다(반 폭에 대한 비율이며 0 이면 중심선)
        PROPERTY( Min = 0.0 )
        float32 _cornerAngle{ 0.0f }; ///< 이만큼(라디안) 도는 곡선이면 레이싱 라인을 끝까지 쓴다
        PROPERTY( Min = 0.0, Units = m )
        float32 _cornerLookAhead{ 0.0f }; ///< 곡선을 읽는 거리(m)
        PROPERTY( Min = 0.0 )
        float32 _driftStartAngle{ 0.0f }; ///< 앞 곡선이 이만큼(라디안) 돌면 드리프트를 건다
        PROPERTY( Min = 0.0 )
        float32 _driftEndAngle{ 0.0f }; ///< 곡선이 이 값(라디안) 아래로 펴지면 드리프트를 놓는다(미니터보)
        PROPERTY( Min = 0.0 )
        float32 _brakeAngle{ 0.0f }; ///< 이보다(라디안) 급한 곡선에서는 페달을 늦춘다
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _brakeThrottle{ 0.0f }; ///< 급한 곡선에서 밟는 페달(0..1)
        PROPERTY( Min = 0.0, Units = s )
        float32 _itemUseDelay{ 0.0f }; ///< 아이템을 받고 이 시간(초) 동안은 들고 있는다
        PROPERTY( Min = 0.0, Units = s )
        float32 _itemMaxHold{ 0.0f }; ///< 이 시간(초)보다 오래 들고 있지 않는다
        PROPERTY( Min = 0.0, Units = m )
        float32 _shellRange{ 0.0f }; ///< 앞 차가 이 거리(m) 안에 있으면 껍질을 쏜다
        PROPERTY( Min = 0.0, Units = m )
        float32 _bananaRange{ 0.0f }; ///< 뒤 차가 이 거리(m) 안에 있으면 바나나를 놓는다

        /** @brief 0 이 뜻이 없는 칸이 모두 적혔으면 true 입니다. */
        bool isSet() const
        {
            const bool bLook  = _lookAheadBase > 0.0f && _lookAheadPerSpeed > 0.0f && _steerGain > 0.0f && _cornerAngle > 0.0f && _cornerLookAhead > 0.0f;
            const bool bDrift = _driftStartAngle > 0.0f && _driftEndAngle > 0.0f && _brakeAngle > 0.0f;
            return bLook && bDrift && _itemMaxHold > 0.0f && _shellRange > 0.0f && _bananaRange > 0.0f;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템 판단에 쓰는 경기 상황입니다. 거리가 0 보다 작으면 "없음" 입니다. */
    struct KartAIContext
    {
        float32      _gapAhead{ -1.0f };  ///< 바로 앞 순위 차까지의 거리(m)
        float32      _gapBehind{ -1.0f }; ///< 바로 뒤 순위 차까지의 거리(m)
        float32      _itemHeldTime{ 0.0f };
        KartItemKind _itemKind{ KartItemKind::Banana };
        uint8        _bHasItem{ SW_FALSE };
        uint8        _bShielded{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class KartAIDriver
     * @brief 차 하나의 운전수입니다. 상태로는 드리프트 중인 쪽만 가집니다.
     */
    class SW_KIT_API KartAIDriver
    {
    public:
        KartAIDriver();

        void setSettings( const KartAISettings& settings ) { _settings = settings; }
        void reset() { _driftSide = 0; }
        /** @brief 드리프트 중인 쪽을 복원합니다(경기 상태 읽기에 씁니다). 값은 -1, 0, 1 입니다. */
        void setDriftSide( int32 driftSide ) { _driftSide = driftSide; }

        /** @brief 이번 걸음의 입력을 계산합니다. @p trackDistance 는 차의 중심선 거리입니다. */
        KinematicVehicleInput computeInput( const KartTrack& track, const KinematicVehicleMotor& motor, float32 trackDistance );
        /** @brief 지금 아이템을 써야 하는지 반환합니다. */
        bool shouldUseItem( const KartAIContext& context ) const;

        const KartAISettings& getSettings() const { return _settings; }
        int32                 getDriftSide() const { return _driftSide; }

    private:
        KartAISettings _settings;
        int32          _driftSide; ///< 드리프트 중인 쪽이다(-1 또는 1, 드리프트 중이 아니면 0)
    };
} // namespace sw
