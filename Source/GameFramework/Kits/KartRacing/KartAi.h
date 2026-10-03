/**
 * @file KartAi.h
 * @brief 카트 AI 운전 — 중심선 앞을 내다보는 레이싱 라인(곡선 안쪽으로), 굽은 길의 드리프트, 아이템 쓰기 판단입니다.
 * @details 러버밴딩(1 등과 거리에 따른 최고 속도 배율)은 순위를 아는 `KartRace` 가 겁니다 — 운전은 속도 상한을 모릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/KartRacing/KartItems.h"
#include "GameFramework/Movement/ArcadeVehicleMotor.h"

namespace sw
{
    class KartTrack;

    /** @brief AI 운전 수치입니다. 각은 라디안입니다. */
    struct KartAiSettings
    {
        float32 _lookAheadBase{ 6.0f };      ///< 겨누는 점까지의 기본 거리(m)
        float32 _lookAheadPerSpeed{ 0.35f }; ///< 속도 1 m/초마다 더 멀리 본다
        float32 _steerGain{ 2.5f };          ///< 방향 오차(라디안) → 조향
        float32 _racingLine{ 0.6f };         ///< 곡선 안쪽으로 붙는 정도(반 폭에 대한 비율, 0 = 중심선)
        float32 _cornerAngle{ 0.8f };        ///< 이만큼 도는 곡선이면 레이싱 라인을 끝까지 쓴다
        float32 _cornerLookAhead{ 25.0f };   ///< 곡선을 읽는 거리(m)
        float32 _driftStartAngle{ 0.9f };    ///< 앞 곡선이 이만큼 돌면 드리프트를 건다
        float32 _driftEndAngle{ 0.35f };     ///< 이만큼 아래로 펴지면 놓는다(미니터보)
        float32 _brakeAngle{ 2.2f };         ///< 이보다 급하면 페달을 늦춘다
        float32 _itemUseDelay{ 0.8f };       ///< 아이템을 받고 이만큼은 들고 있는다
        float32 _itemMaxHold{ 8.0f };        ///< 이보다 오래 들고 있지 않는다
        float32 _shellRange{ 45.0f };        ///< 앞 차가 이 거리(m) 안이면 껍질을 쏜다
        float32 _bananaRange{ 15.0f };       ///< 뒤 차가 이 거리 안이면 바나나를 놓는다
    };

    /** @brief 아이템 판단에 드는 경기 상황입니다. 거리 < 0 은 "없음" 입니다. */
    struct KartAiContext
    {
        float32      _gapAhead{ -1.0f };  ///< 바로 앞 순위 차까지(m)
        float32      _gapBehind{ -1.0f }; ///< 바로 뒤 순위 차까지(m)
        float32      _itemHeldTime{ 0.0f };
        KartItemKind _itemKind{ KartItemKind::Banana };
        uint8        _bHasItem{ SW_FALSE };
        uint8        _bShielded{ SW_FALSE };
    };

    /**
     * @class KartAiDriver
     * @brief 차 하나의 운전수입니다. 드리프트 중인가만 상태로 듭니다.
     */
    class SW_GF_API KartAiDriver
    {
    public:
        KartAiDriver();

        void setSettings( const KartAiSettings& settings ) { _settings = settings; }
        void reset() { _driftSide = 0; }

        /** @brief 이번 걸음의 입력입니다. @p trackDistance 는 차의 중심선 거리입니다. */
        ArcadeVehicleInput computeInput( const KartTrack& track, const ArcadeVehicleMotor& motor, float32 trackDistance );
        /** @brief 지금 아이템을 쓸까입니다. */
        bool shouldUseItem( const KartAiContext& context ) const;

        const KartAiSettings& getSettings() const { return _settings; }
        int32                 getDriftSide() const { return _driftSide; }

    private:
        KartAiSettings _settings;
        int32          _driftSide; ///< −1 · 1 드리프트 중인 쪽, 0 아님
    };
} // namespace sw
