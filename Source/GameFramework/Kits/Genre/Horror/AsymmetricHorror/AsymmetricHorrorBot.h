/**
 * @file AsymmetricHorrorBot.h
 * @brief 비대칭 공포의 봇입니다. 사람이 잡지 않은 생존자의 명령을 정하고(`decideSurvivor`), 살인마를 한 걸음 움직입니다(`stepKiller`).
 * @details 생존자 봇은 플레이어와 같은 명령(`AsymmetricHorrorCommand`)만 낸다. 살인마는 판의 자극(`AsymmetricHorrorMatch::collectKillerStimuli`)을 `AIPerception` 으로 감지해
 *          들기 → 갈고리 → 빈사 줍기 → 해치 닫기 → 추격 · 공격 → 사물함 뒤지기 → 발전기 순찰 · 걷어차기 · 판자 부수기 순으로 고른다. 난수를 쓰지 않아 같은 판이면 같은 판단이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/AIPerception.h"

namespace sw
{
    struct AsymmetricHorrorStage;

    class AsymmetricHorrorMatch;

    /** @brief 생존자 하나가 한 걸음에 내는 명령입니다. 사람의 입력과 봇이 같은 모양을 쓴다. */
    struct AsymmetricHorrorCommand
    {
        float2                 _move;
        uint8                  _bUseTriggered : 1; ///< 이번에 눌렀다 — 곁의 일(스킬 체크 응답, 구출, 문, 치료, 수리, 판자, 창틀, 사물함)을 한다
        uint8                  _bUseHeld      : 1; ///< 누르고 있다 — 들렸거나 걸렸으면 몸부림이다
        [[maybe_unused]] uint8 _reserved      : 6;

        AsymmetricHorrorCommand()
            : _move{ 0.0f, 0.0f }
            , _bUseTriggered{ SW_FALSE }
            , _bUseHeld{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        bool isIdle() const { return _bUseTriggered == SW_FALSE && _bUseHeld == SW_FALSE && _move._x == 0.0f && _move._y == 0.0f; }
    };
} // namespace sw

namespace sw
{
    /** @brief 봇의 수치입니다(씬 데이터). */
    REFLECT()
    struct SW_KIT_API AsymmetricHorrorBotSettings
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "How far the killer sees in front of it", Min = 0.1, Units = m )
        float32 _sightRange{ 14.0f };
        PROPERTY( Tooltip = "Distance to the killer under which a survivor bot runs away", Min = 0.0, Units = m )
        float32 _fleeRange{ 7.0f };
        PROPERTY( Tooltip = "How long the killer remembers a survivor it lost", Min = 0.0, Units = s )
        float32 _memorySeconds{ 5.0f };
        PROPERTY( Tooltip = "Match steps the killer spends on one generator of its patrol", Min = 1 )
        int32 _patrolSteps{ 180 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AsymmetricHorrorBot
     * @brief 봇 하나가 판 하나의 살인마와 사람이 없는 생존자들을 맡습니다. 살인마의 기억은 저장하지 않습니다(불러온 뒤 다시 감지한다).
     */
    class SW_KIT_API AsymmetricHorrorBot
    {
    public:
        AsymmetricHorrorBot();

        /** @brief 수치를 정하고 살인마의 기억을 비웁니다. */
        void initialize( const AsymmetricHorrorBotSettings& settings );
        /** @brief 생존자 @p survivor 의 이번 명령을 정합니다. 움직일 수 없는 생존자는 빈 명령이고, 들렸거나 걸렸으면 몸부림칩니다. */
        AsymmetricHorrorCommand decideSurvivor( const AsymmetricHorrorMatch& match, const AsymmetricHorrorStage& stage, int32 survivor ) const;
        /** @brief 살인마를 한 걸음 움직입니다. */
        void stepKiller( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, float32 deltaTime );

    private:
        /** @brief 빈사 생존자 줍기, 갈고리 걸기, 해치 닫기입니다. 이번 걸음을 썼으면 true 입니다. */
        bool handleDowned( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, float32 deltaTime ) const;
        /** @brief 감지한 생존자를 쫓고 닿으면 공격합니다. 쫓을 것이 없으면 false 입니다. */
        bool chase( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, float32 deltaTime ) const;
        /** @brief 발전기를 순서대로 돌며 걷어차고, 곁의 판자를 부숩니다. */
        void patrol( AsymmetricHorrorMatch& inoutMatch, const AsymmetricHorrorStage& stage, float32 deltaTime ) const;

    private:
        AsymmetricHorrorBotSettings _settings;
        AIPerception                _perception;
        vector<AIStimulus>          _listStimulus; ///< 걸음마다 비워 다시 쓰는 자극 그릇
    };
} // namespace sw
