/**
 * @file CrashHandler.h
 * @brief 처리되지 않은 예외와 치명적인 시그널을 잡아 심볼 변환한 콜 스택 · 미니덤프 · 컨텍스트를 남깁니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    struct CrashHandlerProcessHook;

    /**
     * @brief 크래시 문맥 파일(`crash_<세션>.txt`) 끝에 붙일 글을 쓰는 함수입니다. **할당 · 잠금 없이** @p pOutText 에 쓰고 쓴 길이를 돌려줍니다.
     * @details 크래시 시점에 읽어야 하는 소유자 쪽 상태(서버의 최근 틱 이력 링 버퍼 같은 것)를 미리 정해 둔 키-값 대신 그대로 싣는 자리입니다.
     *          죽어 가는 프로세스에서 새 파일을 쓰지 않고 이미 쓰는 문맥 파일에 얹으므로, 보고 묶음(`CrashReportService`)이 따로 챙길 것이 없습니다.
     */
    using CrashExtraTextWriter = uint32 ( * )( void* pUser, utf8* pOutText, uint32 capacity );

    /**
     * @class CrashHandler
     * @brief 크래시 처리기 하나 — 보고 폴더 · 시한 · 문맥 값 · 빵부스러기 · 덧붙일 글을 들고, 설치되면 이 프로세스의 크래시를 보고합니다.
     * @details 설치한 뒤 접근 위반이나 시그널이 발생하면 예외 코드 · 주소와 폴트 스레드의 콜 스택을 남기고, 원래 동작(프로세스 종료)으로 넘깁니다.
     *          배포본에서는 텍스트 콜 스택만으로는 부족하다(최적화된 빌드는 인라인과 꼬리 호출로 프레임이 합쳐진다) — Windows 는 **미니덤프**를 함께 쓴다.
     *          언리얼이 CrashReportClient 로 덤프 · 로그 · 컨텍스트를 묶어 올리는 것과 같은 역할이다(여기서는 같은 프로세스에서 쓰고, 재진입 가드와 할당 없는 경로로 버틴다).
     *
     *          **상태는 이 객체가 든다.** 소유자(엔진은 `CrashHandlerSubsystem`, 테스트는 케이스)가 만들고 설치한다. 전역에 남는 것은 OS 가 프로세스에 하나만 두는 것뿐이다 —
     *          예외 필터 · 시그널 처리기는 사용자 포인터를 받지 않으므로 그 진입점이 "지금 설치된 처리기" 하나를 원자 포인터로 찾는다(Breakpad 의 처리기 스택과 같다).
     *          설치는 쌓인다: 나중에 설치한 처리기가 보고하고, 떼면 앞의 처리기가 다시 보고한다(테스트가 앱의 처리기를 건드리지 않고 자기 폴더 · 시한으로 죽어 볼 수 있다).
     */
    class SW_API CrashHandler
    {
    public:
        CrashHandler();
        /** @brief 설치돼 있으면 뗍니다. */
        ~CrashHandler();

        CrashHandler( const CrashHandler& )            = delete;
        CrashHandler& operator=( const CrashHandler& ) = delete;

        /**
         * @brief 이 처리기를 이 프로세스의 크래시 보고자로 설치합니다(가장 안쪽 — 앞서 설치된 처리기 위에 쌓인다). 부른 스레드는 `ThreadCrashStack::initializeCurrentThread` 도 거칩니다.
         * @details 처음 설치되는 처리기가 OS 훅을 겁니다. Windows 는 처리되지 않은 SEH 예외 필터와 함께 SIGABRT · 순수 가상 호출 · 잘못된 CRT 인자 훅을 겁니다. 셋은 CRT 가
         *          `__fastfail` 로 **예외 필터를 건너뛰고** 프로세스를 끝내는 길이라, 훅이 없으면 abort 로 끝날 때 덤프도 스택도 남지 않습니다. 이미 설치돼 있으면 아무것도 하지 않습니다.
         */
        void install();
        /** @brief 설치를 풉니다. 마지막 처리기가 풀리면 OS 훅을 떼고 이전 처리기를 되돌립니다. 설치돼 있지 않으면 아무것도 하지 않습니다. */
        void uninstall();

        /**
         * @brief 크래시 리포트에 함께 적을 키-값을 등록합니다(같은 키는 덮어쓴다).
         * @details 엔진 · 게임이 아는 정보를 크래시 시점에 알 수 있도록 미리 등록해 둡니다. 백엔드, GPU 어댑터와 드라이버 버전, 빌드 식별자, 활성 씬 같은 것들입니다.
         *          **크래시 시점에는 할당을 하지 않습니다**(힙이 이미 깨져 있을 수 있습니다). 그래서 여기서 미리 고정 버퍼에 복사해 둡니다.
         *          이 저장소는 RHI 백엔드가 넷이라 "어느 백엔드에서 났는가" 가 특히 중요합니다(범위를 좁히는 첫 질문입니다).
         */
        void setContextValue( string_view key, string_view value );
        /**
         * @brief 크래시 보고에 붙일 최근 사건 한 줄(빵부스러기)을 넣습니다. 최근 32 줄만 남고, 크래시 때 `crash_<세션>.breadcrumbs.txt` 로 씁니다.
         * @details 텔레메트리가 사건마다 넣습니다(동의와 상관없이 — 기계 밖으로 나가는 것은 크래시 보고 동의가 정한다). 아무 스레드에서 불러도 됩니다.
         */
        void addBreadcrumb( string_view text );
        /**
         * @brief 문맥 파일 끝에 글을 붙일 함수를 걸고 그 구독을 돌려줍니다(칸은 하나 — 다시 걸면 바꾸고 옛 구독은 아무것도 풀지 않는다).
         * @details 소유자는 @p pUser 와 같은 수명으로 구독을 든다 — 구독이 사라지면 풀리므로 지운 버퍼를 크래시 경로가 읽을 수 없다.
         */
        [[nodiscard]] Subscription bindExtraTextWriter( CrashExtraTextWriter pWriter, void* pUser );

        /**
         * @brief 멈춘 프로세스를 보고합니다 — 컨텍스트 · 빵부스러기 파일과, Windows 는 프로세스 스냅샷 미니덤프(모든 스레드의 스택)를 씁니다. 프로세스는 끝내지 않습니다.
         * @details 멈춤을 잡은 감시 스레드(`AutomationHangWatchdog`)가 부른다 — 그 스레드는 멀쩡하므로 크래시 경로와 달리 보고 스레드에 맡기지 않고 그 자리에서 쓴다.
         *          할당하지 않는다(멈춘 스레드가 힙 락을 쥐었을 수 있다). POSIX 는 살아 있는 프로세스를 할당 없이 덤프할 길이 없어 컨텍스트만 쓴다.
         */
        void reportHang( const utf8* pReason );

        // ------------------------------------------------------------------------------
        // 크래시 경로 — 설치된 처리기의 OS 진입점(플랫폼 파일)과 행 보고가 부른다. 테스트도 죽지 않고 파일 모양을 볼 때 부른다.
        // ------------------------------------------------------------------------------

        /**
         * @brief 리포트 파일 경로 `<폴더>/crash_<세션ID>.<확장자>` 를 만듭니다.
         * @details **할당하지 않습니다.** 크래시 시점에 힙을 건드리지 않도록 호출하는 쪽의 버퍼에 씁니다.
         */
        void makeReportPath( utf8* pOutPath, uint32 outSize, const utf8* pExtension ) const;
        /**
         * @brief 크래시 컨텍스트(백엔드 · GPU · 빌드 등 미리 등록해 둔 값)와 빵부스러기를 파일로 씁니다. **할당하지 않습니다.**
         * @details 언리얼이 CrashContext 로 올리는 것과 같은 역할이다. 덤프나 코어 파일만으로는 "어느 백엔드에서 났는가" · "어떤 드라이버였는가" 를 알 수 없는데,
         *          그것이 범위를 좁히는 첫 질문이다. 세 플랫폼이 같은 형식을 쓰도록 여기에 한 번만 둔다.
         */
        void writeContextFile( const utf8* pReason, const void* pFaultAddress, uint64 processID, uint64 threadID );
        /**
         * @brief 크래시 리포트 본문을 만들어 스택 파일 · stderr · 로그에 남깁니다(세 플랫폼이 **같은 리포트**를 낸다).
         * @param pReason          폴트 이름(예외 코드 이름 · 시그널 이름)
         * @param pFaultAddress    폴트 주소. 없으면 nullptr
         * @param pPlatformContext 스택을 따라가기 시작할 지점(Windows 는 `CONTEXT*`). nullptr 이면 현재 스택
         * @param bMiniDumpWritten 미니덤프를 **실제로** 남겼는지 여부. 남긴 경우에만 그 경로를 목록에 넣습니다
         * @param pPlatformThread  폴트 스레드(Windows `HANDLE`). nullptr 이면 지금 스레드 — 보고 스레드가 대신 쓸 때 넘깁니다
         * @note 여기서부터는 **할당이 생깁니다.** `symbolize` 가 `sw::string` 을 값으로 반환하고, `StringBuilder` 도 8KB 를 넘기면
         *       힙으로 늘어납니다. 할당이 없는 것(미니덤프 · 컨텍스트 파일)은 호출하는 쪽이 이 함수보다 **먼저** 써 두어야 합니다.
         */
        void writeReport( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, bool bMiniDumpWritten, void* pPlatformThread = nullptr );
        /**
         * @brief 보고가 한 걸음 나아갔다고 적습니다(덤프 항목 · 파일 하나 · 심볼 프레임 하나). **할당 · 잠금이 없고 시그널 안에서 불러도 됩니다.**
         * @details 시한은 보고 전체의 길이가 아니라 **진척이 멈춘 시간**을 잰다. 바쁜 기계에서 덤프 · 심볼 변환이 길어진 보고는 끝까지 쓰고,
         *          락에서 영영 선 보고(막힌 stdio · 힙 락)만 끊는다. 전체 길이를 재면 부하가 걸린 크래시에서 덤프 · 스택 파일이 잘린다.
         */
        void recordProgress() { _reportProgress.fetch_add( 1 ); }

        /** @brief 덤프 · 리포트를 쓸 폴더를 정합니다. 엔진은 로그 폴더를 준다 — 사용자가 폴더 하나만 보내면 되도록. */
        void setReportFolder( string_view folderPath ) { _reportFolder = folderPath; }
        /** @brief 덤프 · 리포트를 쓸 폴더입니다(정하지 않았으면 빈 글 — 작업 폴더에 쓴다). */
        const utf8* getReportFolder() const { return _reportFolder.c_str(); }
        /**
         * @brief 크래시 보고가 **진척 없이** 버틸 수 있는 시간(초)입니다. 그동안 한 걸음도 나아가지 않으면 보고를 버리고 곧장 끝냅니다. 0 이면 기본값(20 초).
         * @details 보고는 죽어 가는 프로세스 안에서 돈다. 크래시가 남긴 락(힙 손상으로 abort 한 malloc 의 락 · 막힌 stdio · 로더)을
         *          보고가 기다리면 영영 끝나지 않고, 크래시 난 게임이 창을 띄운 채 서 있다. 시한은 전체 길이가 아니라 진척이 멈춘 시간이라
         *          바쁜 기계에서 길어진 덤프 · 심볼 변환은 끝까지 쓰고 멈춘 보고만 끊는다(`recordProgress`).
         *          Windows 는 폴트 스레드가 시한마다 진척을 보고, POSIX 는 `alarm` 시그널이 진척을 보고 다시 건다.
         */
        void setReportDeadline( uint32 seconds );
        /** @brief 진척 없이 버틸 시간(초)입니다(`setReportDeadline`). */
        uint32 getReportDeadlineSeconds() const { return _reportDeadlineSeconds.load(); }
        /** @brief 지금까지의 진척 수입니다. 기다리는 쪽(폴트 스레드 · 시한 시그널)이 시한마다 앞의 값과 비교합니다. */
        uint32 getProgress() const { return _reportProgress.load(); }
        /** @brief 문맥 값입니다. */
        const CrashContextStore& getContextStore() const { return _contextStore; }
        /** @brief 빵부스러기입니다. */
        const CrashBreadcrumbStore& getBreadcrumbStore() const { return _breadcrumbStore; }
        /** @brief 설치돼 있으면 true 입니다. */
        bool isInstalled() const { return _bInstalled == SW_TRUE; }

    private:
        /** @brief 빵부스러기를 `crash_<세션>.breadcrumbs.txt` 로 씁니다. 비어 있으면 쓰지 않습니다. **할당하지 않습니다.** */
        void writeBreadcrumbFile() const;
        /** @brief 심볼 변환한 콜 스택을 파일로 남깁니다(배포 환경에서는 아무도 stderr 를 보지 않는다 — 파일이어야 보내 줄 수 있다). */
        void writeStackFile( const utf8* pStackText ) const;
        /** @brief 문맥 글 함수 구독이 풀렸습니다. 지금 걸린 것이 그 구독이면 칸을 비웁니다(`_extraTextSubscriptions` 의 해제 함수). */
        void releaseExtraTextWriter( uint64 id );

        /** @brief 설치 · 해제가 처리기 사슬(`_pOuter`)을 바꿀 때 잡는 잠금입니다. 크래시 경로는 잡지 않는다(원자 포인터만 읽는다). */
        static mutex& getChainMutex();

        /** @brief OS 훅(플랫폼 파일)은 가장 안쪽 처리기(`_s_pInstalled`)를 읽는다. */
        friend struct CrashHandlerProcessHook;

    private:
        /// @brief 진척 없이 버틸 기본 시간(초)입니다(`setReportDeadline`).
        static constexpr uint32 kDefaultReportDeadlineSeconds = 20;

        /**
         * @brief OS 진입점이 보고를 맡길 가장 안쪽(나중에 설치한) 처리기입니다. 없으면 nullptr.
         * @details 이 프로세스에 하나뿐인 전역 상태다 — 예외 필터 · 시그널 처리기는 사용자 포인터를 받지 않아 그 진입점이 처리기를 찾을 길이 이것뿐이다.
         *          바꾸는 것은 `install` · `uninstall` 뿐이고(잠금 안에서), 크래시 경로는 원자로 읽기만 한다.
         */
        static atomic<CrashHandler*> _s_pInstalled;

        fixed_string<constant::buffer::kMax1024> _reportFolder;
        CrashContextStore                        _contextStore;
        CrashBreadcrumbStore                     _breadcrumbStore;
        atomic<uint32>                           _reportDeadlineSeconds;
        atomic<uint32>                           _reportProgress;
        atomic<CrashExtraTextWriter>             _pExtraTextWriter;
        atomic<void*>                            _pExtraTextUser;
        atomic<uint64>                           _extraTextWriterID; ///< 지금 걸린 함수의 구독 번호. 풀린 구독이 이 번호일 때만 칸을 비운다
        atomic<uint64>                           _nextExtraTextWriterID;
        SubscriptionList                         _extraTextSubscriptions;
        CrashHandler*                            _pOuter; ///< 설치했을 때 앞서 설치돼 있던 처리기(떼면 그것이 다시 보고한다)
        uint8                                    _bInstalled : 1;

        SW_SERVICE_SLOT( CrashHandler, Required );
    };
} // namespace sw
