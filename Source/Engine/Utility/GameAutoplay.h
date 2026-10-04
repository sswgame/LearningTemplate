/**
 * @file GameAutoplay.h
 * @brief 게임의 자동 플레이(입력 없이 AI 가 조종 — 확인 · 녹화 · 벤치) 계약입니다. 게임이 한 줄로 등록하고, 에디터 툴바 · 콘솔 · 명령줄이 같은 스위치를 켭니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief 게임 하나의 자동 플레이 등록 줄입니다. 문자열 · 값은 등록한 이미지(게임 모듈)의 정적 저장소에 있습니다.
     * @details 켜짐 상태는 그 게임의 전역 변수(`-gv_<게임>AutoPlay`) **하나**에 있습니다 — 명령줄 · 전역 변수 패널 · 콘솔 · 툴바가 모두 같은 값을
     *          바꾸므로 어긋날 자리가 없습니다.
     */
    struct GameAutoplayRegistration
    {
        const utf8* _pGameName;     ///< 게임 이름(툴팁)
        const utf8* _pDescription;  ///< 무엇을 자동으로 하는가(툴팁)
        const utf8* _pVariableName; ///< 켜짐을 든 전역 변수 이름(`gv_shooterAutoPlay`)
        int32*      _pValue;        ///< 그 전역 변수(0 = 꺼짐)
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameAutoplay
     * @brief 지금 올라온 게임의 자동 플레이 스위치입니다. 게임 코드는 `isOn()` 하나만 묻습니다.
     * @details 등록은 게임 .cpp 의 `SW_GAME_AUTOPLAY` 한 줄이고, 모듈을 내리면(핫 리로드) 등록도 빠집니다. 등록부는 Engine 에 있습니다. 둘 이상이
     *          등록돼 있으면 마지막 것을 씁니다(시험 게임은 한 번에 하나다). 게임 스레드에서만 씁니다.
     */
    class SW_API GameAutoplay
    {
    public:
        /** @brief 등록합니다(정적 등록자가 부릅니다). */
        static void registerAutoplay( const GameAutoplayRegistration* pRegistration );
        /** @brief 등록을 뺍니다. */
        static void unregisterAutoplay( const GameAutoplayRegistration* pRegistration );
        /** @brief 지금 쓰는 등록입니다. 자동 플레이가 없는 게임이면 nullptr 입니다. */
        static const GameAutoplayRegistration* findActive();
        /** @brief 자동 플레이가 켜져 있으면 true 입니다(등록이 없으면 false). */
        static bool isOn();
        /**
         * @brief 켜거나 끕니다. 전역 변수 표를 거쳐 값을 써 변경 콜백 · 패널이 같이 따릅니다(표가 없으면 값에 바로 씁니다).
         * @return 등록이 없으면 false
         */
        static bool setOn( bool bOn );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_GAME_AUTOPLAY` 가 두는 정적 등록자입니다. */
    struct SW_API GameAutoplayRegistrar
    {
        explicit GameAutoplayRegistrar( const GameAutoplayRegistration* pRegistration );
        ~GameAutoplayRegistrar();
        GameAutoplayRegistrar( const GameAutoplayRegistrar& )            = delete;
        GameAutoplayRegistrar& operator=( const GameAutoplayRegistrar& ) = delete;

        const GameAutoplayRegistration* _pRegistration;
    };
} // namespace sw

/**
 * @brief 게임의 자동 플레이를 그 전역 변수 바로 아래에서 등록합니다(`SW_TEST_GLOBAL_VARIABLE_INT( gv_x, 0, …, SW_KEEP_IN_SHIPPING )` 다음 줄).
 * @param gvName       켜짐을 든 int32 전역 변수
 * @param pGameName    게임 이름(리터럴)
 * @param pDescription 무엇을 자동으로 하는가(리터럴)
 */
#define SW_GAME_AUTOPLAY( gvName, pGameName, pDescription )                                                            \
    static const ::sw::GameAutoplayRegistration sw_gameAutoplay_##gvName{ pGameName, pDescription, #gvName, &gvName }; \
    static const ::sw::GameAutoplayRegistrar    sw_gameAutoplayRegistrar_##gvName { &sw_gameAutoplay_##gvName }
