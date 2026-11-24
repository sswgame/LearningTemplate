/**
 * @file ControlIntent.h
 * @brief 행동 레이어입니다. 조종 대상(폰)이 받는, 장치와 무관한 한 틱의 명령입니다. 플레이어, AI, 네트워크, 기록이 모두 이것을 냅니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /**
     * @class ControlViewTick
     * @brief 의도를 낸 사람이 그때 보던 월드의 시각(서버 틱, 소수)입니다. 랙 보정이 이 시각으로 되감습니다.
     * @details 값 없이 만들 수 없다 — 조종 시스템이 프레임마다 하나를 정해 조종자에게 준다(`ControlFrameContext::_viewTick`). 서버 월드의 지금을 그대로 보는 쪽(혼자 하는 게임 · 서버의 AI ·
     *          리슨 서버 호스트)은 `makePresent` 이고, 복제된 월드를 보간해 보는 클라이언트는 그린 서버 틱(`makeSeen`)이다. Valve usercmd 의 `tick_count − lerp` 자리.
     *          64 비트라 서버를 오래 켜 둬도 감기지 않는다(32 비트 1/256 틱은 30 Hz 에서 엿새 남짓에 감겼다).
     */
    class SW_GF_API ControlViewTick
    {
    public:
        static constexpr uint64 kStampsPerTick = 256; ///< 선의 값은 서버 틱 × 이 값의 정수다(1/256 틱 단위)

        /** @brief 서버 월드의 지금을 보고 있습니다(되감지 않는다). */
        static ControlViewTick makePresent() { return ControlViewTick{ kPresentStamp }; }
        /** @brief 서버 틱 @p serverTick 을 그린 화면을 보고 있습니다. 음수(아직 그리지 않음)는 0 입니다. */
        static ControlViewTick makeSeen( float64 serverTick );

        bool isPresent() const { return _stamp == kPresentStamp; }
        /** @brief 보던 서버 틱입니다. 지금을 보고 있으면 @p presentTick 입니다. */
        float64 getServerTick( float64 presentTick ) const { return isPresent() ? presentTick : static_cast<float64>( _stamp ) / static_cast<float64>( kStampsPerTick ); }

        void write( BitWriter& writer ) const;
        /** @brief 넘침이면 읽는 쪽이 `BitReader::hasOverflowed` 로 압니다. */
        static ControlViewTick read( BitReader& reader );

        bool operator==( const ControlViewTick& other ) const { return _stamp == other._stamp; }
        bool operator!=( const ControlViewTick& other ) const { return _stamp != other._stamp; }

    private:
        explicit ControlViewTick( uint64 stamp )
            : _stamp{ stamp }
        {
        }

        static constexpr uint64 kPresentStamp = ~0ull;

    private:
        uint64 _stamp;
    };

    /**
     * @class ControlTrigger
     * @brief 버튼 하나가 발동한 사건입니다 — 버튼, 누른 순간의 시선, 그때 보던 시각. 셋 없이는 만들 수 없습니다.
     * @details 발동을 한 프레임 비트로 두면 여러 입력을 한 의도로 합칠 때 앞 발동이 뒤 입력의 시선 · 시각으로 판정된다. 사건이 자기 값을 들고 다녀 합쳐도 섞이지 않는다.
     */
    class SW_GF_API ControlTrigger
    {
    public:
        ControlTrigger( int32 buttonIndex, float32 yaw, float32 pitch, ControlViewTick viewTick );

        int32                  getButton() const { return _buttonIndex; }
        float32                getYaw() const { return _yaw; }
        float32                getPitch() const { return _pitch; }
        const ControlViewTick& getViewTick() const { return _viewTick; }
        /** @brief 보던 시각을 서버 틱 [@p lowestTick, @p highestTick] 으로 자릅니다(서버가 받은 값을 쓰기 전). */
        void clampViewTick( float64 lowestTick, float64 highestTick );
        void setViewTick( ControlViewTick viewTick ) { _viewTick = viewTick; }

        bool operator==( const ControlTrigger& other ) const
        {
            return _buttonIndex == other._buttonIndex && _yaw == other._yaw && _pitch == other._pitch && _viewTick == other._viewTick;
        }
        bool operator!=( const ControlTrigger& other ) const { return ( *this == other ) == false; }

    private:
        ControlViewTick _viewTick;
        int32           _buttonIndex;
        float32         _yaw;
        float32         _pitch;
    };

    /**
     * @struct ControlIntent
     * @brief 이동 축(조종 요 기준), 위아래, 조종 회전(절대), 아날로그 값, 버튼 비트입니다. 버튼과 아날로그 값의 이름은 폰의 스키마(`PawnComponent`)가 정합니다.
     * @details **조종 회전은 절대값**입니다(언리얼 ControlRotation). 플레이어 조종자가 시선 입력을 쌓고, AI 는 바라볼 곳으로 돌리고, 네트워크와 기록은
     *          그 값을 그대로 싣습니다. 그래서 같은 의도 열은 어느 조종자가 내도 같은 궤적입니다. 네트워크와 파일 형식은 `write` 와 `read` 하나이고(양자화),
     *          조종 시스템은 로컬 조종자의 의도도 `quantize` 를 거쳐 폰에 넣습니다. 그래서 로컬, 원격, 재생이 같은 값으로 움직입니다.
     *          **발동은 사건 목록**(`ControlTrigger`)이다. 사건마다 누른 순간의 시선(CS2 식 서브틱 입력 — 프레임 끝의 조종 회전이 아니라 클릭한 순간의 회전)과 그때 보던 시각을 든다.
     *          여러 틱의 의도를 한 프레임에 모아도(`appendTriggers`) 사건은 자기 시선 · 시각 그대로다.
     *          모터마다의 입력 형식(`KinematicVehicleInput`, `PlatformerInput` …)은 폰의 이동 컴포넌트가 이것에서 만드는 변환으로 남습니다.
     */
    struct SW_GF_API ControlIntent
    {
        static constexpr int32   kButtonCount         = 32;
        static constexpr int32   kAnalogCount         = 4;
        static constexpr int32   kMaxSerializedBytes  = 112;      ///< `write` 가 쓰는 바이트 상한(입력 창이 한 틱에 싣는 슬롯의 크기)
        static constexpr int32   kMaxTriggers         = 6;        ///< 의도 하나가 선 · 파일에 싣는 발동 상한. 넘는 발동은 보내는 쪽이 다음 틱에 싣는다(받는 쪽은 깨진 것으로 본다)
        static constexpr float32 kAxisSteps           = 127.0f;   ///< 축 1 의 정수 단계 수. 이동과 아날로그는 -127..127 정수로 싣는다(0 과 ±1 이 정확히 남는다)
        static constexpr float32 kAngleStepsPerRadian = 10000.0f; ///< 한 라디안의 정수 단계 수. 조종 회전은 0.0001 rad 간격 정수로 싣는다

        float2                 _move{};                                     ///< x 는 오른쪽, y 는 앞(-1..1, 길이 1 이하). 조종 요 기준이다
        float32                _moveUp{ 0.0f };                             ///< 위(+)와 아래(-). 수영, 비행, 사다리에 쓴다
        float32                _controlYaw{ 0.0f };                         ///< 라디안, +Z 에서 +X 쪽(`FirstPersonLook` 과 같은 약속)
        float32                _controlPitch{ 0.0f };                       ///< 라디안, 위가 +
        float32                _arrAnalog[kAnalogCount]{};                  ///< 스키마가 이름을 준 아날로그(가속 페달, 브레이크, 조준 줌 …), -1..1
        uint32                 _buttonDown{ 0 };                            ///< 누르고 있음(비트 = 스키마 순서)
        ControlViewTick        _viewTick{ ControlViewTick::makePresent() }; ///< 이 의도(누르고 있는 버튼)를 낼 때 보던 시각. 발동은 사건마다 자기 값이 있다
        vector<ControlTrigger> _listTrigger{};                              ///< 이번 틱에 발동한 사건들(누른 순서)

        /** @brief 버튼을 누르고 있는지입니다. 폰에 없는 버튼(비어 있음)이나 범위 밖이면 false 입니다. */
        bool isDown( optional<int32> buttonIndex ) const { return isInRange( buttonIndex ) && ( _buttonDown & ( 1u << *buttonIndex ) ) != 0; }
        /** @brief 버튼이 이번 틱에 발동했는지입니다. 폰에 없는 버튼(비어 있음)이나 범위 밖이면 false 입니다. */
        bool wasTriggered( optional<int32> buttonIndex ) const;
        /** @brief 버튼 @p buttonIndex 의 누름을 씁니다. 비었거나 범위 밖이면 할 일이 없습니다. */
        void setButton( optional<int32> buttonIndex, bool bDown );
        /** @brief 버튼 @p buttonIndex 의 발동 사건을 더합니다(누른 순간의 시선과 보던 시각). 비었거나 범위 밖이면 할 일이 없습니다. */
        void addTrigger( optional<int32> buttonIndex, float32 yaw, float32 pitch, ControlViewTick viewTick );
        /** @brief @p other 의 발동 사건을 뒤에 붙입니다(여러 틱을 한 프레임에 모을 때). 사건의 시선 · 시각은 그대로입니다. */
        void appendTriggers( const ControlIntent& other );
        /** @brief 의도와 사건의 보던 시각을 모두 @p viewTick 으로 바꿉니다 — 기록을 다시 낼 때 기록 때의 시각은 이 월드의 것이 아니다. */
        void setViewTick( ControlViewTick viewTick );
        /**
         * @brief 받은 보던 시각을 서버가 인정하는 범위로 자릅니다. 의도의 시각은 [@p lowestTick, @p presentTick], 사건의 시각은 [@p lowestTick, 의도의 시각] 이 된다.
         * @details 지어낸 시각으로 범위 밖을 되감지 못하게 한다. @p lowestTick 은 되감기 상한과 이 연결이 앞서 본 시각 중 늦은 쪽이다 — 화면 시각은 되돌아가지 않는다.
         */
        void clampViewTicks( float64 lowestTick, float64 presentTick );
        /** @brief 버튼 번호가 있고 비트 범위 안인지입니다. */
        static bool isInRange( optional<int32> buttonIndex ) { return buttonIndex.has_value() && 0 <= *buttonIndex && *buttonIndex < kButtonCount; }
        /** @brief 이동을 월드 방향으로 바꿉니다. 조종 요로 XZ 를 돌리고 Y 에 위아래를 넣으며, 길이는 그대로입니다. */
        float3 computeWorldMove() const;
        /**
         * @brief 월드 이동을 앞 방향 @p worldForward(XZ) 기준 축으로 바꿉니다 — x 는 오른쪽(앞을 시계 방향으로 90°), y 는 앞 성분이고 각각 -1..1 로 자릅니다.
         * @details 탈것이 의도("가고 싶은 월드 방향")를 페달 · 조향(물리 차) · 추진 · 키(배)로 읽는 한 길이다. 앞이 0 길이면 +Z 를 앞으로 본다.
         */
        float2 computeLocalMove( const float3& worldForward ) const;
        /** @brief 월드 방향 @p worldDirection(XZ)을 이 의도의 조종 요 기준 이동 축으로 넣습니다(AI 가 경로 방향을 넣을 때). 길이는 1 로 자릅니다. */
        void setWorldMove( const float3& worldDirection );
        /**
         * @brief 양자화해 씁니다(이동과 아날로그는 8 비트, 각은 16 비트 남짓, 버튼은 가변 정수). 쓴 뒤 `read` 한 값은 `quantize()` 한 값과 같습니다.
         * @details 값마다 0 을 중심에 둔 정수 단계라 0 과 ±1 이 그대로 남습니다. 최솟값부터 세는 단계라면 0 이 0 이 아닌 값으로 남아 서 있는 폰이 조금씩 흐릅니다.
         * @return 단계가 범위를 넘어 쓰지 못하면 false 입니다. 단계는 쓰기 전에 범위로 잘리므로 정상 값에서는 늘 true 입니다.
         */
        [[nodiscard]] bool write( BitWriter& writer ) const;
        /** @brief `write` 의 바이트를 읽습니다. 넘침이거나 단계가 범위를 넘거나 `_move` 길이가 1 을 넘으면 false 이고 값은 0 입니다. */
        [[nodiscard]] bool read( BitReader& reader );
        /** @brief 선 · 파일을 거친 것과 같은 값으로 자릅니다(할당 없음). */
        void quantize();

        bool operator==( const ControlIntent& other ) const;
        bool operator!=( const ControlIntent& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw
