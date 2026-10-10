/**
 * @file DebugHUDRegistry.h
 * @brief 디버그 HUD 섹션 등록부와 `SW_DEBUG_HUD_SECTION` 입니다. **Shipping 에서는 통째로 빠집니다**(`SW_DEV_COMMANDS_ENABLED`).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/formatString.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/fixed_string.h"

#include "Engine/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    /** @brief HUD 섹션 한 줄입니다(이름 · 값). */
    struct DebugHUDLine
    {
        string _label;
        string _value;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DebugHUDSectionWriter
     * @brief 섹션 본문이 줄과 그래프를 적는 곳입니다. HUD 가 갱신할 때마다(켜진 섹션만) 비우고 본문에 넘깁니다.
     * @details 줄 칸은 지우지 않고 다시 써서, 줄 수가 그대로면 문자열 용량을 다시 쓴다. 게임 스레드에서만 씁니다.
     */
    class SW_API DebugHUDSectionWriter
    {
    public:
        /** @brief 프레임 시간 기록의 길이(프레임)입니다. */
        static constexpr uint32 kFrameHistoryCount = 120;

        DebugHUDSectionWriter();

        /** @brief 줄 · 그래프를 비웁니다(칸은 남긴다). */
        void reset();
        /** @brief 줄 하나를 더합니다. */
        void addLine( string_view label, string_view value );
        /** @brief 값을 printf 형식(`formatstring` — `%.1f` · `%#`)으로 적은 줄 하나를 더합니다. 128 바이트에서 자릅니다. */
        template <typename... Args>
        void addLineFormat( string_view label, string_view format, Args&&... args )
        {
            fixed_string<constant::kMaxBuffer128> text;
            formatstring( text.data(), text.capacity(), format, std::forward<Args>( args )... );
            addLine( label, text.c_str() );
        }
        /** @brief 막대 그래프를 겁니다(섹션마다 하나 — 다시 부르면 바꾼다). 값은 복사합니다. @p maxValue 가 막대 높이의 끝입니다. */
        void addGraph( const float32* pValue, uint32 count, float32 maxValue );

        /** @brief 지난 프레임들의 실제 프레임 시간(초, 오래된 것부터)입니다. HUD 가 프레임마다 모은다. */
        const float32* getFrameHistory() const { return _arrFrameSecond; }
        uint32         getFrameHistoryCount() const { return _frameHistoryCount; }
        /** @brief 프레임 시간 하나를 기록합니다(HUD 가 부른다 — 꽉 차면 가장 오래된 것을 민다). */
        void recordFrameSeconds( float32 seconds );

        /** @brief 이번 갱신에 적힌 줄입니다(`getLineCount` 개만 유효). */
        const DebugHUDLine&    getLineAt( uint32 index ) const { return _listLine[index]; }
        uint32                 getLineCount() const { return _lineCount; }
        bool                   hasGraph() const { return _listGraph.empty() == false; }
        const vector<float32>& getGraph() const { return _listGraph; }
        float32                getGraphMax() const { return _graphMax; }

    private:
        vector<DebugHUDLine> _listLine;
        vector<float32>      _listGraph;
        float32              _arrFrameSecond[kFrameHistoryCount];
        float32              _graphMax;
        uint32               _lineCount;
        uint32               _frameHistoryCount;
    };
} // namespace sw

namespace sw
{
    /** @brief 섹션 본문입니다 — 이미 있는 서비스에서 읽기만 하고 @p writer 에 적습니다(새 수집을 프레임에 더하지 않는다). 게임 스레드에서 부릅니다. */
    using DebugHUDSectionFunc = void ( * )( DebugHUDSectionWriter& writer );

    /** @brief 섹션 등록 줄의 플래그입니다. */
    struct DebugHUDSectionFlag
    {
        static constexpr uint8 kNone              = 0;
        static constexpr uint8 kDefaultOn         = 1u << 0; ///< 사용자 설정이 없으면 켜진 섹션
        static constexpr uint8 kUsesFrameProfiler = 1u << 1; ///< `FrameProfiler` 구간 · 카운터를 읽는다 — 보이는 동안 HUD 가 프로파일러를 켠다
    };
} // namespace sw

namespace sw
{
    /** @brief 섹션 하나의 등록 줄입니다. 문자열 · 함수는 등록한 이미지(모듈)의 정적 저장소에 있습니다. */
    struct DebugHUDSectionRegistration
    {
        const utf8*         _pName;  ///< 명령 · 설정에 쓰는 이름(`fps` · `physics` — 공백 · 쉼표 · `-` 로 시작 금지, 대소문자 무시)
        const utf8*         _pTitle; ///< HUD 에 보이는 제목(개발 도구라 영어 고정)
        DebugHUDSectionFunc _pFunc;
        int32               _order; ///< 작을수록 위(같으면 이름순)
        uint8               _flags; ///< `DebugHUDSectionFlag`
    };
} // namespace sw

namespace sw
{
    /**
     * @class DebugHUDRegistry
     * @brief 디버그 HUD 섹션의 등록부입니다(Engine 하나 — 엔진 · 키트 · 게임이 등록하고, 모듈을 내리면 그 섹션이 빠집니다).
     * @details 섹션은 자기 .cpp 에 `SW_DEBUG_HUD_SECTION` 한 줄로 등록합니다(`SW_DEV_COMMAND` 와 같은 모양). 등록 · 해제마다 판 번호가 올라
     *          HUD 와 설정 창이 섹션 목록을 다시 짓습니다. 같은 이름 · 예약어(`on` · `off` · `list` · `corner` · `opacity` · `window`)는 경고와 함께 거절합니다.
     *          게임 스레드 · 정적 초기화에서만 씁니다.
     */
    class SW_API DebugHUDRegistry
    {
    public:
        /** @brief 프로세스에 하나인 등록부입니다(Engine 이미지의 함수 정적). */
        static DebugHUDRegistry& get();

        DebugHUDRegistry();

        /** @brief 섹션을 올립니다. 이름이 비었거나 · 예약어거나 · 이미 있거나 · 본문이 없으면 false 입니다. */
        bool registerSection( const DebugHUDSectionRegistration* pRegistration );
        /** @brief 섹션을 뺍니다(없으면 아무 일도 없습니다). */
        void unregisterSection( const DebugHUDSectionRegistration* pRegistration );
        /** @brief 이름(대소문자 무시)으로 섹션을 찾습니다. */
        const DebugHUDSectionRegistration* findSection( string_view name ) const;
        /** @brief 섹션 전부(순서 값 → 이름순)입니다. 포인터는 이번 호출 안에서만 씁니다(모듈이 내려가면 빠진다). */
        const vector<const DebugHUDSectionRegistration*>& getSections() const { return _registration.getItems(); }
        uint32                                            getCount() const { return _registration.getCount(); }
        /** @brief 등록 · 해제마다 오르는 번호입니다(목록을 다시 지을 때를 안다). */
        uint64 getRevision() const { return _revision; }
        /** @brief @p name 이 명령의 예약어라 섹션 이름으로 쓸 수 없으면 true 입니다. */
        static bool isReservedName( string_view name );

    private:
        RegistrationList<const DebugHUDSectionRegistration> _registration; ///< (순서, 이름) 순 · 이름은 대소문자 무시
        uint64                                              _revision;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct DebugHUDSectionState
     * @brief 섹션 켜짐을 적은 설정 글(`debug.hudSections` · `gv_debugHUDSections`)을 읽고 씁니다.
     * @details 글은 공백으로 나눈 낱말 목록이고, `이름` 은 켬 · `-이름` 은 끔입니다. 적히지 않은 섹션은 등록의 기본(`kDefaultOn`)을 따릅니다 —
     *          그래서 새 모듈의 섹션은 저장된 글이 있어도 자기 기본으로 뜨고, 내린 모듈의 낱말은 지우지 않고 남깁니다.
     */
    struct SW_API DebugHUDSectionState
    {
        /** @brief @p stateText 에서 @p registration 이 켜져 있으면 true 입니다(같은 이름이 둘이면 뒤 것). */
        static bool isSectionShown( string_view stateText, const DebugHUDSectionRegistration& registration );
        /** @brief @p stateText 에서 @p name 의 낱말을 지우고, @p bShown 이 기본과 다르면 그 낱말을 끝에 붙인 글을 만듭니다. */
        static string makeStateText( string_view stateText, string_view name, bool bShown, bool bDefaultShown );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_DEBUG_HUD_SECTION` 이 두는 정적 등록자입니다 — 만들 때 올리고 없앨 때(모듈을 내릴 때) 뺍니다. */
    struct SW_API DebugHUDSectionRegistrar
    {
        explicit DebugHUDSectionRegistrar( const DebugHUDSectionRegistration* pRegistration );
        ~DebugHUDSectionRegistrar();
        DebugHUDSectionRegistrar( const DebugHUDSectionRegistrar& )            = delete;
        DebugHUDSectionRegistrar& operator=( const DebugHUDSectionRegistrar& ) = delete;

        const DebugHUDSectionRegistration* _pRegistration;
        bool                               _bRegistered;
    };
} // namespace sw

/**
 * @brief 디버그 HUD 섹션을 그 섹션의 .cpp 에서 등록합니다. Shipping 에서는 아무것도 남기지 않습니다 — 본문 함수도 `#if SW_DEV_COMMANDS_ENABLED`
 *        안에 두십시오.
 * @param varName 파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pName   명령 · 설정에 쓰는 이름(리터럴, `hud <이름> on`)
 * @param pTitle  HUD 에 보이는 제목
 * @param order   작을수록 위
 * @param flags   `DebugHUDSectionFlag` 의 조합
 * @param pFunc   `void( DebugHUDSectionWriter& )` 본문
 */
    #define SW_DEBUG_HUD_SECTION( varName, pName, pTitle, order, flags, pFunc )                                                                  \
        static const ::sw::DebugHUDSectionRegistration sw_debugHUDSection_##varName{ pName, pTitle, pFunc, order, static_cast<uint8>( flags ) }; \
        static const ::sw::DebugHUDSectionRegistrar    sw_debugHUDSectionRegistrar_##varName { &sw_debugHUDSection_##varName }

#else

    #define SW_DEBUG_HUD_SECTION( varName, pName, pTitle, order, flags, pFunc ) static_assert( true, "SW_DEBUG_HUD_SECTION is compiled out of Shipping" )

#endif
