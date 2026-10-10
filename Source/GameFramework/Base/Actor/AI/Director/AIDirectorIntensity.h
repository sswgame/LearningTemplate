/**
 * @file AIDirectorIntensity.h
 * @brief 플레이어 긴장도(스트레스) 모델 — 감독이 읽는 인터페이스와, 데이터의 신호로 도는 기본 구현입니다.
 * @details 레프트 4 데드의 "생존자 긴장도" 와 같은 자리입니다: 입은 피해 · 가까이서 쓰러뜨림이 긴장도를 올리고, 싸움이 끝나고 잠시 뒤부터 식습니다.
 *          게임은 자기 모델을 `IAIDirectorIntensityModel` 로 끼울 수 있고, 끼우지 않으면 프로필의 `<Intensity>` 로 도는 기본 모델을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AIDirectorIntensityDef;

    class Archive;

    /**
     * @class IAIDirectorIntensityModel
     * @brief 감독이 읽는 긴장도 모델입니다. 감독은 `update` 를 프레임마다 한 번 부르고 값을 읽기만 합니다(신호는 게임이 모델에 직접 넣는다).
     */
    class SW_GF_API IAIDirectorIntensityModel
    {
    public:
        IAIDirectorIntensityModel()                                              = default;
        IAIDirectorIntensityModel( const IAIDirectorIntensityModel& )            = default;
        IAIDirectorIntensityModel& operator=( const IAIDirectorIntensityModel& ) = default;
        virtual ~IAIDirectorIntensityModel()                                     = default;

        /** @brief 시간을 흘립니다(식기 · 초당 신호). */
        virtual void update( float32 deltaTime ) = 0;
        /** @brief 지금 긴장도입니다(0 이상). */
        virtual float32 getIntensity() const = 0;
        /** @brief 마지막 싸움 신호 뒤 지난 시간(s)입니다. */
        virtual float32 getCalmSeconds() const = 0;
        /** @brief 신호 값입니다(보상 풀의 "필요" 를 읽는다). 모르는 신호면 0 입니다. */
        virtual float32 getSignal( const hashed_string& signalID ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AIDirectorIntensityModel
     * @brief `<Intensity>` 의 신호로 도는 기본 모델입니다.
     * @details 긴장도 = max( 쌓인 스트레스, Σ Level 신호 바닥 ), 상한 `max`. 스트레스는 Impulse(한 번) · Rate(초마다)로 쌓이고, 마지막 싸움 신호 뒤
     *          `decayDelay` 초가 지나면 `decayPerSecond` 로 식습니다. 정의는 빌려 씁니다.
     */
    class SW_GF_API AIDirectorIntensityModel final : public IAIDirectorIntensityModel
    {
    public:
        AIDirectorIntensityModel();

        /** @brief 정의(빌림)를 정하고 처음으로 돌립니다. */
        void initialize( const AIDirectorIntensityDef* pDef );
        /** @brief 스트레스 · 신호 · 싸움 시계를 처음으로 돌립니다(판을 다시 시작). */
        void reset();
        /** @brief Impulse 신호에 @p amount 를 넣습니다. 모르는 신호 · Impulse 가 아니면 false 입니다. */
        bool addSignal( const hashed_string& signalID, float32 amount );
        /** @brief Rate · Level 신호의 지금 값을 정합니다. 모르는 신호 · Impulse 면 false 입니다. */
        bool setSignal( const hashed_string& signalID, float32 value );

        void    update( float32 deltaTime ) override;
        float32 getIntensity() const override;
        float32 getCalmSeconds() const override { return _calmSeconds; }
        float32 getSignal( const hashed_string& signalID ) const override;
        float32 getStress() const { return _stress; }
        /** @brief 스트레스 · 싸움 뒤 시간 · 신호 값을 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 신호 수가 정의와 다르면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        int32   findSignalIndex( const hashed_string& signalID ) const;
        void    noteCombat();
        float32 computeLevelFloor() const;

        vector<float32>               _listSignalValue; ///< 신호마다(Impulse 는 마지막에 넣은 양)
        const AIDirectorIntensityDef* _pDef;
        float32                       _stress;
        float32                       _calmSeconds;
    };
} // namespace sw
