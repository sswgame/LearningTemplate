/**
 * @file ModuleManager/HostPart.h
 * @brief 호스트(App · Server)가 엔진 **뒤에** 세우고 엔진 **앞에서** 내리는 부품의 공용 집합과 그 의존 선언입니다.
 * @details 두 실행 파일이 종료 순서를 손으로 적어 주석으로 맞추던 것을 데이터로 바꾼다. 부품마다 먼저 서야 하는 부품을 이 파일 한 곳에 적고,
 *          `SubsystemCollection` 이 위상 순서(동점은 이름 순)로 세우고 역순으로 멈춘 뒤 내린다 — 언리얼에서 게임 인스턴스 서브시스템 묶음이 엔진 뒤에
 *          서고 앞에 내려가는 자리다. 부품의 몸통(세우기 · 내리기)은 호스트가 함수 둘(`HostPartStartDelegate` · `HostPartStopDelegate`)로 준다.
 *          - 게임(`Game`)이 축이다. 게임보다 먼저 서는 것(서버 관측 · 서버 문맥)은 게임이 내려간 뒤까지 살고, 게임에 기대는 것(교체기 · 콘솔)은 게임보다 먼저 내려간다.
 *          - 엔진보다 오래 살아야 하는 것(모듈 라이브러리 창구 · 자동화 구동기 · 종료 신호)은 여기가 아니라 호스트 범위(`SubsystemScope`)에 둔다.
 *          - 서버 서비스(`ServerServiceManager`)는 넣지 않는다 — 모듈 리로드마다 멈췄다 다시 서는 수명이라 한 번의 세우기 · 내리기 짝과 맞지 않는다.
 *          헤더만으로 선다(엔진 테스트 · 서버 부팅 테스트가 같은 표로 순서를 단언한다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemCollection.h"

namespace sw
{
    /** @brief 호스트 부품입니다. 어느 호스트가 무엇을 세우는지는 `kArrClientHostPart` · `kArrServerHostPart` 가 정합니다. */
    enum class HostPartID : uint8
    {
        Observability,   ///< 지표 · 상태 레지스트리 · 운영 HTTP · 프로세스 지표(Server)
        CrashReports,    ///< 지난 실행의 크래시를 묶어 지표로 낸다(Server)
        ServiceContext,  ///< 서버 문맥을 호스트 범위에 빌려 건다 — 게임 · 키트가 찾는다(Server)
        Game,            ///< 게임(과 에디터) 인스턴스 — 모듈 호스트
        BackendSwitcher, ///< RHI 백엔드 교체기(App)
        UserSettings,    ///< 사용자 설정의 화면 적용(App)
        RuntimeUIHooks,  ///< 런타임 UI 의 클립보드 · 접근성 · 끝내기 명령(App)
        DevConsole,      ///< 개발 콘솔(App 은 게임 창 콘솔과 표준 입력 채널, Server 는 명령 처리기)
        StandardInput,   ///< 표준 입력 명령 채널(Server)
        DebugChannel,    ///< 원격 개발 명령 채널(Server Dev)
    };

    /** @brief 부품 이름입니다. 의존 칸도 이 상수를 가리킨다 — 철자가 틀리면 컴파일 오류다. */
    namespace HostPartName
    {
        inline constexpr const utf8* kObservability   = "Observability";
        inline constexpr const utf8* kCrashReports    = "CrashReports";
        inline constexpr const utf8* kServiceContext  = "ServiceContext";
        inline constexpr const utf8* kGame            = "Game";
        inline constexpr const utf8* kBackendSwitcher = "BackendSwitcher";
        inline constexpr const utf8* kUserSettings    = "UserSettings";
        inline constexpr const utf8* kRuntimeUIHooks  = "RuntimeUIHooks";
        inline constexpr const utf8* kDevConsole      = "DevConsole";
        inline constexpr const utf8* kStandardInput   = "StandardInput";
        inline constexpr const utf8* kDebugChannel    = "DebugChannel";
    } // namespace HostPartName

    /** @brief 부품 @p id 를 세웁니다. 못 세우면 false 입니다(기동 실패 — 이미 세운 것은 역순으로 내린다). */
    SW_DECLARE_DELEGATE( bool, HostPartStartDelegate, HostPartID );
    /** @brief 부품 @p id 를 내립니다. 세우기에 성공한 부품에만 불립니다. */
    SW_DECLARE_DELEGATE( void, HostPartStopDelegate, HostPartID );

    /** @brief 플레이어 실행 파일(App)이 세우는 부품입니다. */
    inline constexpr HostPartID kArrClientHostPart[] = { HostPartID::Game, HostPartID::BackendSwitcher, HostPartID::UserSettings, HostPartID::RuntimeUIHooks,
                                                         HostPartID::DevConsole };
    /** @brief 전용 서버(Server)가 세우는 부품입니다. */
    inline constexpr HostPartID kArrServerHostPart[] = { HostPartID::Observability, HostPartID::CrashReports, HostPartID::ServiceContext, HostPartID::Game,
                                                         HostPartID::StandardInput, HostPartID::DevConsole, HostPartID::DebugChannel };
} // namespace sw

namespace sw
{
    /**
     * @class HostPart
     * @brief 호스트 부품 하나를 서브시스템으로 세웁니다. 이름과 의존은 아래 표가 정하고, 몸통은 호스트의 함수입니다.
     */
    class HostPart final : public ISubsystem
    {
    public:
        HostPart( HostPartID id, HostPartStartDelegate start, HostPartStopDelegate stop )
            : _id{ id }
            , _start{ std::move( start ) }
            , _stop{ std::move( stop ) }
        {
        }

        /** @brief 부품의 이름입니다(의존 칸 · 로그 · 테스트가 부르는 식별자). */
        static const utf8* getName( HostPartID id )
        {
            switch ( id )
            {
                case HostPartID::Observability:
                    return HostPartName::kObservability;
                case HostPartID::CrashReports:
                    return HostPartName::kCrashReports;
                case HostPartID::ServiceContext:
                    return HostPartName::kServiceContext;
                case HostPartID::Game:
                    return HostPartName::kGame;
                case HostPartID::BackendSwitcher:
                    return HostPartName::kBackendSwitcher;
                case HostPartID::UserSettings:
                    return HostPartName::kUserSettings;
                case HostPartID::RuntimeUIHooks:
                    return HostPartName::kRuntimeUIHooks;
                case HostPartID::DevConsole:
                    return HostPartName::kDevConsole;
                case HostPartID::StandardInput:
                    return HostPartName::kStandardInput;
                case HostPartID::DebugChannel:
                    return HostPartName::kDebugChannel;
            }
            return "Unknown";
        }

        SubsystemInitResult initializeSubsystem() override
        {
            return _start.isBound() && _start( _id ) ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        void shutdownSubsystem() override
        {
            if ( _stop.isBound() )
                _stop( _id );
        }
        const utf8* getSubsystemName() const override { return getName( _id ); }

        /** @brief 먼저 서야 하는 부품입니다 — 이 집합에 반드시 있어야 한다(없으면 정렬 오류). */
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrNeedsObservability[] = { HostPartName::kObservability };
            static constexpr const utf8* kArrNeedsGame[]          = { HostPartName::kGame };
            static constexpr const utf8* kArrNeedsDevConsole[]    = { HostPartName::kDevConsole };
            switch ( _id )
            {
                case HostPartID::CrashReports:   // 남은 묶음 수를 지표로 낸다
                case HostPartID::ServiceContext: // 문맥이 지표 · 상태 레지스트리를 빌려준다
                    return kArrNeedsObservability;
                case HostPartID::BackendSwitcher: // 교체 앞뒤로 게임 · 에디터 인스턴스를 내리고 다시 세운다
                case HostPartID::DevConsole:      // 명령이 게임이 올린 개발 명령 · 변수를 부른다 — 게임보다 먼저 내려간다
                    return kArrNeedsGame;
                case HostPartID::DebugChannel: // 원격 줄을 개발 콘솔로 돌린다
                    return kArrNeedsDevConsole;
                case HostPartID::Observability:
                case HostPartID::Game:
                case HostPartID::UserSettings:
                case HostPartID::RuntimeUIHooks:
                case HostPartID::StandardInput:
                    break;
            }
            return {};
        }

        /** @brief 이것 **뒤에** 서야 하는 부품입니다 — 그 호스트에 없으면 건너뛴다(App 의 게임은 서버 문맥 없이 선다). */
        span<const utf8* const> getSubsystemDependents() const override
        {
            static constexpr const utf8* kArrBeforeGame[] = { HostPartName::kGame };
            // 게임 · 키트가 기동하며 서버 문맥(지표 레지스트리 · 서버 설정 · 게임 포트)을 찾는다.
            if ( _id == HostPartID::ServiceContext )
                return kArrBeforeGame;
            return {};
        }

    private:
        HostPartID            _id;
        HostPartStartDelegate _start;
        HostPartStopDelegate  _stop;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief @p listPart 를 @p outParts 에 더하고 의존 순서로 세웁니다. 엔진 기동이 끝난 뒤 한 번 부릅니다.
     * @return 모두 섰으면 true 입니다. 아니면 `outParts.getError()` 에 어느 부품인지 남고, 세운 것은 `outParts.destroyAll()` 이 역순으로 내립니다.
     */
    [[nodiscard]] inline bool startHostParts( SubsystemCollection& outParts, vector_reference<const HostPartID> listPart, const HostPartStartDelegate& start, const HostPartStopDelegate& stop )
    {
        SW_MEMORY_SCOPE( EngineMisc );
        for ( const HostPartID id : listPart )
        {
            outParts.add( make_unique<HostPart>( id, start, stop ) );
        }
        return outParts.initializeAll( SubsystemHostMask::All );
    }

    /** @brief 지금 서 있는 부품을 선 순서대로 `A > B > C` 로 적습니다(기동 로그 — 서버 부팅 테스트가 읽는다). */
    inline string describeHostPartOrder( const SubsystemCollection& parts )
    {
        string text;
        for ( const uint32 index : parts.getInitializedIndices() )
        {
            if ( text.empty() == false )
                text += " > ";
            text += parts.getSubsystem( index ).getSubsystemName();
        }
        return text;
    }

    /** @brief @p listPart 가 설 순서를 `A > B > C` 로 적습니다(몸통 없이 표만으로 — 테스트가 실행 파일의 기동 로그와 맞춰 본다). 정렬 오류면 그 글입니다. */
    inline string describeDeclaredHostPartOrder( vector_reference<const HostPartID> listPart )
    {
        SubsystemCollection parts;
        const bool          bStarted = startHostParts( parts, listPart, SW_DELEGATE_LAMBDA( HostPartStartDelegate, []( HostPartID )
                 { return true; } ),
                                                       HostPartStopDelegate{} );
        return bStarted ? describeHostPartOrder( parts ) : parts.getError();
    }
} // namespace sw
