/**
 * @file ControlIntent.h
 * @brief 행동 층 — 조종 대상(폰)이 받는 장치 무관한 한 틱의 명령입니다. 플레이어 · AI · 네트워크 · 기록이 모두 이것을 냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /**
     * @struct ControlIntent
     * @brief 이동 축(조종 요 기준) · 위아래 · 조종 회전(절대) · 아날로그 칸 · 버튼 비트입니다. 버튼 · 아날로그 칸의 이름은 폰의 스키마(`PawnComponent`)가 정합니다.
     * @details **조종 회전은 절대값**입니다(언리얼 ControlRotation) — 플레이어 조종자가 시선 입력을 쌓고, AI 는 바라볼 곳으로 돌리고, 네트워크 · 기록은
     *          그 값을 그대로 싣습니다. 그래서 같은 의도 열은 어느 조종자가 내도 같은 궤적입니다. 선 · 파일 형식은 `write` · `read` 하나이고(양자화),
     *          조종 시스템은 로컬 조종자의 의도도 `quantize` 를 거쳐 폰에 넣습니다 — 로컬 · 원격 · 재생이 같은 값으로 움직인다.
     *          모터마다의 입력 형식(`ArcadeVehicleInput` · `PlatformerInput` …)은 폰의 이동 컴포넌트가 이것에서 만드는 변환으로 남습니다.
     */
    struct SW_GF_API ControlIntent
    {
        static constexpr int32   kButtonCount         = 32;
        static constexpr int32   kAnalogCount         = 4;
        static constexpr int32   kMaxSerializedBytes  = 40;       ///< `write` 가 쓰는 바이트 상한(입력 창이 한 틱에 싣는 칸의 크기)
        static constexpr float32 kAxisSteps           = 127.0f;   ///< 축 1 의 정수 칸 수 — 이동 · 아날로그는 −127..127 정수로 싣는다(0 · ±1 이 정확히 남는다)
        static constexpr float32 kAngleStepsPerRadian = 10000.0f; ///< 한 라디안의 정수 칸 수 — 조종 회전은 0.0001 rad 간격 정수로 싣는다

        float2  _move{};                    ///< x 오른쪽 · y 앞(−1..1, 길이 1 이하) — 조종 요 기준
        float32 _moveUp{ 0.0f };            ///< 위(+) · 아래(−) — 수영 · 비행 · 사다리
        float32 _controlYaw{ 0.0f };        ///< 라디안, +Z 에서 +X 쪽(`FirstPersonLook` 과 같은 약속)
        float32 _controlPitch{ 0.0f };      ///< 라디안, 위가 +
        float32 _arrAnalog[kAnalogCount]{}; ///< 스키마가 이름을 준 아날로그(가속 페달 · 브레이크 · 조준 줌 …), −1..1
        uint32  _buttonDown{ 0 };           ///< 누르고 있음(비트 = 스키마 순서)
        uint32  _buttonTriggered{ 0 };      ///< 이번 틱에 발동(플레이어는 입력 맵 trigger 규칙, AI 는 한 번 누름)

        bool isDown( int32 buttonIndex ) const { return 0 <= buttonIndex && buttonIndex < kButtonCount && ( _buttonDown & ( 1u << buttonIndex ) ) != 0; }
        bool wasTriggered( int32 buttonIndex ) const
        {
            return 0 <= buttonIndex && buttonIndex < kButtonCount && ( _buttonTriggered & ( 1u << buttonIndex ) ) != 0;
        }
        /** @brief 버튼 @p buttonIndex 의 두 비트를 씁니다. 범위 밖이면 할 일이 없습니다. */
        void setButton( int32 buttonIndex, bool bDown, bool bTriggered );
        /** @brief 이동을 월드로 — 조종 요로 XZ 를 돌리고 Y 에 위아래를 넣습니다(길이는 그대로). */
        float3 computeWorldMove() const;
        /** @brief 월드 방향 @p worldDirection(XZ)을 이 의도의 조종 요 기준 이동 축으로 넣습니다(AI 가 경로 방향을 넣을 때). 길이는 1 로 자릅니다. */
        void setWorldMove( const float3& worldDirection );

        /**
         * @brief 양자화해 씁니다(이동 · 아날로그 8 비트, 각 16 비트 남짓, 버튼은 가변 정수). 쓴 뒤 `read` 한 값 = `quantize()` 한 값입니다.
         * @details 값마다 0 을 중심에 둔 정수 칸이라 0 · ±1 이 그대로 남습니다 — 최솟값에서 세는 칸이면 0 이 0 이 아닌 값으로 남아 서 있는 폰이 조금씩 흐른다.
         */
        void write( BitWriter& writer ) const;
        /** @brief `write` 의 바이트를 읽습니다. 넘침이면 false 이고 값은 0 입니다. */
        [[nodiscard]] bool read( BitReader& reader );
        /** @brief 선 · 파일을 거친 것과 같은 값으로 자릅니다(할당 없음). */
        void quantize();

        bool operator==( const ControlIntent& other ) const;
        bool operator!=( const ControlIntent& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw
