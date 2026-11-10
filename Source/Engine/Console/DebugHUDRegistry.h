/**
 * @file DebugHUDRegistry.h
 * @brief 디버그 HUD 섹션 레지스트리와 `SW_DEBUG_HUD_SECTION` 입니다. Shipping 에서는 등록 매크로가 꺼져 표가 비어 있습니다(`SW_SHIPPING`).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Module/ModuleEntryRegistrar.h"

namespace sw
{
    /** @brief HUD 줄 값의 색 갈래입니다. 상태만 색을 갖는다 — 보통 값은 중립이고 실패 · 경고만 경고색이다(앱 화면 규칙: 강조색은 데이터 · 상태에만). */
    enum class DebugHUDLineStyle : uint8
    {
        Neutral,
        Warning
    };

    /** @brief HUD 섹션 한 줄입니다(이름 · 값 · 값의 색 갈래). */
    struct DebugHUDLine
    {
        string            _label;
        string            _value;
        DebugHUDLineStyle _style{ DebugHUDLineStyle::Neutral };
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
        /** @brief 줄 하나를 더합니다. @p style 이 `Warning` 이면 HUD 가 값을 경고색으로 그린다(실패 · 경고 상태에만). */
        void addLine( string_view label, string_view value, DebugHUDLineStyle style = DebugHUDLineStyle::Neutral );
        /**
         * @brief 값을 printf 형식(`formatstring` — `%.1f` · `%#`)으로 적은 줄 하나를 더합니다. 128 바이트에서 자릅니다.
         * @details 정의는 `Engine/Console/DebugHUDSectionFormat.h` 에 있다(이 헤더가 서식 헤더를 끌어오지 않게) — 부르는 .cpp 가 그것을 include 한다.
         */
        template <typename... Args>
        void addLineFormat( string_view label, string_view format, Args&&... args );
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
        /**
         * 자동화 실행 상태 섹션 — 창이 보이는 시나리오 실행 동안 HUD 가 사용자 켜짐(`debug.hud` · `debug.hudSections`)과 무관하게 그리고, 그 밖에는 그리지 않는다.
         * 사용자 설정 파일에 아무것도 쓰지 않는다(실행 중에만 켜진다). 이 섹션만 보이는 동안 HUD 는 다른 섹션을 그리지 않는다(프로파일러를 켜지 않는다).
         */
        static constexpr uint8 kAutomationRun = 1u << 2;
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

        /** @brief `DebugHUDSectionFlag` 의 @p flag 가 켜져 있으면 true 입니다. */
        bool hasFlag( uint8 flag ) const { return ( _flags & flag ) != 0; }
        /** @brief 사용자가 켜고 끄는 섹션이면 true 입니다(자동화 실행 상태 섹션은 실행이 켠다 — 명령 · 설정 창 · 씬 뷰 HUD 의 목록에 넣지 않는다). */
        bool isUserSection() const { return hasFlag( DebugHUDSectionFlag::kAutomationRun ) == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class DebugHUDRegistry
     * @brief 디버그 HUD 섹션의 레지스트리입니다(Core 이미지에 하나 — 엔진 · 키트 · 게임이 등록하고, 모듈을 내리면 그 섹션이 빠집니다).
     * @details 섹션은 자기 .cpp 에 `SW_DEBUG_HUD_SECTION` 한 줄로 등록합니다(`SW_DEV_COMMAND` 와 같은 모양). 등록 · 해제마다 판 번호가 올라
     *          HUD 와 설정 창이 섹션 목록을 다시 짓습니다. 같은 이름 · 예약어(`on` · `off` · `list` · `corner` · `opacity` · `window`)는 경고와 함께 거절합니다.
     *          등록 · 해제(이미지 로더의 정적 초기화)와 조회(게임 스레드)가 잠금 하나로 등록 목록을 지킨다. 섹션 본문은 잠금 밖에서 부른다 —
     *          `getSections` 는 복사본을 돌려준다(HUD 가 보일 때만 부르는 Dev 전용 경로라 프레임 핫 경로가 아니다).
     */
    class SW_API DebugHUDRegistry final : public ISubsystem
    {
    public:
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::EngineMisc; ///< 할당 용도(`ISubsystem::getSubsystemMemoryTag`)
        static constexpr const utf8* kSubsystemName      = "DebugHUDRegistry";

        DebugHUDRegistry();

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 섹션을 올립니다. 이름이 비었거나 · 예약어거나 · 이미 있거나 · 본문이 없으면 false 입니다. */
        [[nodiscard]] bool registerEntry( const DebugHUDSectionRegistration* pRegistration );
        /** @brief 섹션을 뺍니다(없으면 아무 일도 없습니다). */
        void unregisterEntry( const DebugHUDSectionRegistration* pRegistration );
        /** @brief 이름(대소문자 무시)으로 섹션을 찾습니다. */
        const DebugHUDSectionRegistration* findSection( string_view name ) const;
        /** @brief 섹션 전부(순서 값 → 이름순)의 복사본입니다. 포인터는 이번 호출 안에서만 씁니다(모듈이 내려가면 빠진다). */
        vector<const DebugHUDSectionRegistration*> getSections() const;
        /** @brief 등록된 섹션 수입니다. */
        uint32 getCount() const;
        /** @brief 등록 · 해제마다 오르는 번호입니다(목록을 다시 지을 때를 안다). */
        uint64 getRevision() const;
        /** @brief @p name 이 명령의 예약어라 섹션 이름으로 쓸 수 없으면 true 입니다. */
        static bool isReservedName( string_view name );

        /**
         * @brief 대상을 보는 섹션(npc · movement · sequence · animrewind …)이 볼 오브젝트 이름을 정합니다(빈 값은 지운다). 개발 명령 `debug.target` 과 에디터 Gameplay Debugger 창(선택을 따른다)이 부른다.
         * @details 디버그 소스 표와 그것들이 함께 보는 대상을 한 곳에 둔다 — 언리얼 Gameplay Debugger 가 카테고리 목록과 선택 액터를 한 컨트롤러에 두는 자리다.
         *          shortcut: 대상은 이름이다(같은 이름이 여럿이면 첫 것), 이름이 겹치는 씬에서 고르기가 틀리면 오브젝트 id 로 바꾼다.
         */
        void setTargetName( string_view name );
        /** @brief 지금 대상 이름입니다(비면 정하지 않았다 — 섹션마다 자기 컴포넌트를 가진 첫 오브젝트를 본다). */
        string getTargetName() const;

    private:
        mutable mutex                                                     _mutex;                  ///< 등록 목록 · 판 번호를 지킨다(본문 호출은 잠금 밖)
        RegistrationList<const DebugHUDSectionRegistration> _registration SW_GUARDED_BY( _mutex ); ///< (순서, 이름) 순 · 이름은 대소문자 무시
        uint64 _revision                                                  SW_GUARDED_BY( _mutex );
        string _targetName                                                SW_GUARDED_BY( _mutex ); ///< `setTargetName` 이 정한 대상(비면 없음)
    };

    SW_SERVICE_SLOT( DebugHUDRegistry, Required );
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
    /** @brief `SW_DEBUG_HUD_SECTION` 이 두는 정적 등록자입니다. */
    using DebugHUDSectionRegistrar = ModuleEntryRegistrar<DebugHUDRegistry, const DebugHUDSectionRegistration*>;
} // namespace sw

#if !defined( SW_SHIPPING )

/**
 * @brief 디버그 HUD 섹션을 그 섹션의 .cpp 에서 등록합니다. Shipping 에서는 아무것도 남기지 않습니다 — 본문 함수도 `#if !defined( SW_SHIPPING )`
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
        static ::sw::DebugHUDSectionRegistrar          sw_debugHUDSectionRegistrar_##varName                                                     \
        {                                                                                                                                        \
            &sw_debugHUDSection_##varName                                                                                                        \
        }

#else

    #define SW_DEBUG_HUD_SECTION( varName, pName, pTitle, order, flags, pFunc ) static_assert( true, "SW_DEBUG_HUD_SECTION is compiled out of Shipping" )

#endif
