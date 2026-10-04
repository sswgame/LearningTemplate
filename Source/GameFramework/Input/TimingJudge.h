/**
 * @file TimingJudge.h
 * @brief 타이밍 판정 — 목표 시각과 누른 시각의 차이를 판정 창(가장 좁은 것부터)에 대어 등급을 냅니다.
 * @details 리듬 게임의 Cool/Good/Bad/Miss, 턴제의 타이밍 공격(씨 오브 스타즈 · 마리오 RPG), 비대칭 공포의 스킬 체크, 격투의 저스트 프레임이 같은 계산입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 판정 창 하나입니다. */
    struct TimingWindow
    {
        hashed_string _grade{};
        float32       _earlyWidth{ 0.05f }; ///< 목표보다 이만큼 이르게까지(초)
        float32       _lateWidth{ 0.05f };  ///< 늦게까지
        int32         _score{ 0 };
        uint8         _bBreaksCombo{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 판정 결과입니다. */
    struct TimingResult
    {
        const TimingWindow* _pWindow{ nullptr }; ///< 없으면 창 밖(누름을 무시하거나 놓침으로)
        float32             _offset{ 0.0f };     ///< 누른 시각 − 목표 시각(음수 = 이르다)

        bool isHit() const { return _pWindow != nullptr; }
        bool isEarly() const { return _offset < 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class TimingJudge
     * @brief 창은 좁은 것부터 봅니다. 이른 쪽 · 늦은 쪽 폭을 따로 둘 수 있습니다(늦게 누르는 쪽을 너그럽게 — 입력 지연 보정).
     *        XML: `<TimingWindows><Window grade="Cool" early="0.025" late="0.03" score="300"/>...</TimingWindows>`
     */
    class SW_GF_API TimingJudge
    {
    public:
        void               setWindows( const vector<TimingWindow>& listWindow );
        void               loadFromNode( const XmlNode& node );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        /** @brief 모든 창을 곱해 넓히거나 좁힙니다(난이도 · 접근성). */
        void setScale( float32 scale ) { _scale = scale > 0.0f ? scale : 1.0f; }
        /** @brief 입력 지연 보정(초) — 판정 전에 누른 시각에서 뺍니다. */
        void setOffset( float32 offset ) { _offset = offset; }

        TimingResult judge( float32 targetTime, float32 pressTime ) const;
        /** @brief 목표가 지나 가장 넓은 창도 닫혔는가입니다(놓침 처리). */
        bool hasExpired( float32 targetTime, float32 now ) const;
        /** @brief 가장 넓은 창의 이른 쪽 폭 — 이보다 이르면 누름을 이 목표에 쓰지 않습니다. */
        float32                     getEarliestWidth() const;
        const vector<TimingWindow>& getWindows() const { return _listWindow; }

    private:
        vector<TimingWindow> _listWindow{}; ///< 좁은 것부터
        float32              _scale{ 1.0f };
        float32              _offset{ 0.0f };
    };
} // namespace sw
