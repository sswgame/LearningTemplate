/**
 * @file LiveReloadManager.h
 * @brief 모듈 공유 라이브러리를 섀도 복사해 핫 리로드합니다(의존 모듈까지 연쇄로 교체합니다).
 *
 * @note **여기는 Engine 이 아니라 App 입니다.** 모듈을 로드하고 교체하는 것은 런처(App)의 일이고, Engine 은 모듈이라는 개념
 *       자체를 몰라야 합니다(Engine 레이어 규칙: Engine 은 Editor · GameFramework · Games 를 모릅니다). 예전에는 이 클래스가
 *       `Source/Engine/Module/` 에 있어 EngineLoop 가 소유했는데, 쓰는 쪽은 App(ModuleHost · ModuleCompiler · 단축키)뿐이었고
 *       Shipping 에서는 만들지도 않으면서 864줄이 바이너리에 그대로 실렸습니다. 지금은 Shipping 빌드에서 **파일째 빠집니다**
 *       (`Source/App/CMakeLists.txt` 의 제외 목록).
 *
 *       핫 리로드만 쓰는 도우미 둘도 여기 있습니다 — 섀도 복사본의 파일 바이트를 읽고 고치는 `ModuleImagePatch` 와, 새 모듈 코드를
 *       처음 부르는 자리를 지키는 `ModuleCallGuard`. 쓰는 곳이 이 매니저(와 그 테스트)뿐이고 Shipping 에서 함께 빠지므로 한 단위로 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Time/CpuTimer.h"

#include "Engine/Common/Common.h"
#include "Engine/Module/ModuleHandleProvider.h"

namespace sw
{
    struct ComponentFactoryRegistrar;
    struct EnumRegistrar;
    struct GlobalVariableRegistrar;
    struct TypeRegistrar;
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) ModuleImagePatch — 섀도 복사본을 올리기 전에 파일 바이트를 읽고 고친다(엔진 ABI 도장 · 리눅스 SONAME)
    // ------------------------------------------------------------------------------
    /**
     * @struct ModuleImagePatch
     * @brief ELF64(리틀 엔디언) 공유 라이브러리의 동적 섹션 문자열을 **같은 길이로** 바꿉니다.
     * @details 파일 바이트만 다루므로 어느 플랫폼에서나 돕니다(테스트는 Windows 에서도 합성 ELF 로 돈다). 쓰는 곳은 리눅스의
     *          `LiveReloadManager` 뿐입니다. 문자열 표를 늘리지 않으므로 섹션 · 세그먼트 배치가 그대로입니다.
     * @note 왜 고치는가: 리눅스 동적 링커는 `DT_NEEDED` 를 풀 때 이미 올라온 라이브러리 중 SONAME 이 같은 **먼저 올라온 것**을 씁니다.
     *       섀도 복사본은 파일만 복사하므로 SONAME 이 원본과 같고, 연쇄 리로드는 "전부 prepare(새 이미지 로드) → commit(옛 이미지 내림)"
     *       순서라서 prepare 중인 새 킷이 아직 올라와 있는 **옛** GameFramework 에 묶입니다. 그래서 복사본을 올리기 전에 SONAME 을
     *       세대마다 고유한 이름으로 바꾸고, 의존 모듈 복사본의 NEEDED 를 그 이름으로 바꿉니다 — UE 가 빌드마다 번호 붙은 이름으로 다시
     *       링크하는 것을 복사 시점에 하는 셈입니다. Windows 에서 지연 로드 훅(`DelayLoadNotifyHook.cpp`)이 하는 일의 리눅스 짝입니다.
     */
    struct ModuleImagePatch
    {
        static constexpr int64 kTagNeeded = 1;  ///< DT_NEEDED
        static constexpr int64 kTagSoname = 14; ///< DT_SONAME
        /** @brief 엔진 ABI 도장 문자열의 머리입니다. 뒤에 SHA-1 16진 40 글자가 옵니다(`GenerateEngineAbiStamp.py` 와 같아야 한다). */
        static constexpr const utf8* kEngineAbiStampMarker = "swEngineAbiStamp:";
        static constexpr uint32      kEngineAbiStampDigits = 40;

        /** @brief 동적 섹션의 DT_SONAME 을 읽습니다. ELF64 LE 가 아니거나 SONAME 이 없으면 false 입니다. */
        static bool readSoname( const vector<uint8>& bytes, string& outSoname );

        /**
         * @brief 동적 섹션에서 태그가 @p tag 인 항목 중 문자열이 @p from 인 것을 @p to 로 바꾸고, 바꾼 항목 수를 반환합니다.
         * @details @p to 는 @p from 과 길이가 같아야 합니다(문자열 표 안에서 제자리로 덮습니다). 다르거나 ELF64 LE 가 아니면 0 입니다.
         */
        static uint32 replaceDynamicString( vector<uint8>& inoutBytes, int64 tag, string_view from, string_view to );

        /**
         * @brief 세대 @p generation 을 담은, @p soname 과 **길이가 같은** 이름을 만듭니다(예: `libGameFramework.so` → `libGameFrame0003.so`).
         * @details 확장자(`.so` 부터) 앞의 끝 네 글자를 36진 세대로 바꿉니다. 세대는 프로세스 안에서 모듈과 무관하게 하나씩 오르므로
         *          앞부분이 같은 두 모듈도 같은 이름을 받지 않습니다.
         * @return 만들 수 없으면(`.so` 가 없거나 그 앞이 `lib` + 네 글자보다 짧다) 빈 문자열입니다.
         */
        static string makeGenerationName( string_view soname, uint32 generation );

        /**
         * @brief 모듈 파일 바이트에서 엔진 ABI 도장(`swEngineAbiStamp:<sha1>`)을 찾습니다. 이 함수만은 ELF 에 한정되지 않습니다(DLL · SO 모두).
         * @details 핫 리로드는 **모듈 코드가 한 줄도 돌기 전에**(정적 초기화 전) 돌고 있는 엔진과 같은 헤더로 빌드됐는지 봐야 하므로, 심볼을
         *          찾지 않고 파일에서 표식 문자열을 찾습니다.
         * @return 표식과 그 뒤 16진 40 글자가 온전히 있으면 true 입니다(@p outStamp 는 표식을 포함한 전체).
         */
        static bool findEngineAbiStamp( const vector<uint8>& bytes, string& outStamp );
    };

    // ------------------------------------------------------------------------------
    // 2) ModuleCallGuard — 새 모듈 코드를 처음 부르는 자리를 하드웨어 예외로부터 지킨다
    // ------------------------------------------------------------------------------
    /**
     * @struct ModuleCallGuard
     * @brief 한 호출 안에서 난 하드웨어 예외를 잡아, 프로세스 대신 그 호출만 실패시킵니다.
     * @details 새로 빌드한 게임 · 에디터 코드가 리로드 직후 초기화에서 죽으면, 지키지 않을 때는 에디터 프로세스째 내려가 저장하지 않은
     *          작업을 잃습니다. 지키면 그 모듈만 버리고(무엇을 버릴지는 부르는 쪽이 정한다) 에디터는 살아 저장할 기회가 남습니다.
     *          - Windows 는 SEH(`__try` / `__except`), 리눅스는 결함 시그널(SIGSEGV · SIGBUS · SIGFPE · SIGILL) + `sigsetjmp`.
     *          - **중단점(assert) · 스택 넘침 · 그 밖의 예외는 잡지 않습니다.** assert 는 디버거와 크래시 처리기로 가야 합니다.
     *          - 잡은 뒤의 상태는 온전하지 않습니다. 결함 난 호출 안쪽 프레임의 소멸자는 돌지 않고(쥔 락 · 할당이 남는다), 그 모듈의
     *            자료도 반쯤 만들어진 채입니다. 이것은 **저장하고 재시작할 시간**을 버는 장치이지 계속 돌리는 장치가 아닙니다.
     *          - 모듈 로드(정적 초기화) 자체는 지키지 않습니다. 로더 락을 쥔 채 빠져나오면 다음 로드가 멈춥니다.
     *          - 메인 스레드에서만 부릅니다. 리눅스의 시그널 처리기는 프로세스 전체에 걸리므로 겹쳐 부르면 바깥 것만 설치 · 해제하고, 지키는
     *            호출 밖(다른 스레드)의 결함은 원래 처리기(크래시 처리기)로 돌려보냅니다.
     */
    struct ModuleCallGuard
    {
        /**
         * @brief @p call 을 지키며 부릅니다.
         * @param outFaultCode 결함이 났으면 그 코드(Windows 예외 코드 · 리눅스 시그널 번호), 아니면 0 입니다.
         * @return 결함 없이 끝났으면 true 입니다.
         */
        static bool run( const Delegate<void()>& call, uint32& outFaultCode );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) LiveReloadManager
    // ------------------------------------------------------------------------------
    class IFileWatcher;
    /**
     * @brief 모듈을 섀도 경로에 복사해 로드하는 핫 리로드 매니저입니다.
     * @note 연쇄 교체는 의존 순서(위상 정렬)대로 합니다. 모든 모듈의 prepare 가 성공한 뒤에만 commit 합니다. 언로드는 위상 역순
     *       (의존하는 쪽 먼저)입니다. prepare 가 실패하면 새 이미지를 버리고 기존 핸들을 유지합니다(keep-old). commit 중에 실패하거나
     *       onAfter 가 그래프를 깨진 상태(poison)로 표시하면 남은 commit 을 멈춥니다. 이미 교체된 DLL 은 되돌릴 수 없습니다.
     */
    class LiveReloadManager final : public IModuleHandleProvider
    {
    public:
        using OnBeforeReloadDelegate = Delegate<void()>;
        using OnAfterReloadDelegate  = Delegate<void( void* pLibraryModule )>;
        /** @brief onAfterReload 안에서 하드웨어 예외가 났을 때 불립니다. 인자는 예외 코드(Windows) · 시그널 번호(리눅스)입니다. */
        using OnReloadFaultDelegate       = Delegate<void( uint32 faultCode )>;
        using OnBeforeCommitBatchDelegate = Delegate<void( const vector<string>& listModuleName )>;
        using DrainWorkersDelegate        = Delegate<void()>;

        /**
         * @brief 모듈을 언로드하기 전에 실행 중인 태스크를 비우는 제한 시간(ms)입니다. 넘으면 리로드 그래프를 깨진 상태로 표시합니다.
         * @details ModuleHost::drainRenderWorkers(App 경로)와 drainTasksBeforeUnload(헤드리스 폴백)가 함께 씁니다.
         */
        static constexpr uint32 kModuleDrainTimeoutMs = 5000;

        /**
         * @brief 교체된 옛 이미지를 몇 번의 연쇄 리로드(배치)만큼 올려 둘지입니다.
         * @details 옛 코드를 가리키는 것(델리게이트 · 함수 포인터 · vtable · 문자열 리터럴)이 어딘가 남아 있어도, 이미지가 올라와 있는
         *          동안은 크래시가 아니라 옛 동작이 한 번 더 돕니다. 섀도 복사본이라 올려 두어도 원본 파일은 잠기지 않습니다. 이보다
         *          오래된 배치는 의존하는 쪽부터 내립니다(Windows 지연 로드는 참조 수를 올리지 않으므로 순서를 손으로 지킵니다).
         */
        static constexpr uint32 kMaxRetiredBatchCount = 4;

        /** @brief 모듈 맵과 감시자가 빈 상태로 시작합니다. */
        LiveReloadManager();
        /** @brief 로드된 모듈을 언로드합니다. */
        ~LiveReloadManager() override;

        /** @brief 복사를 금지합니다. */
        LiveReloadManager( const LiveReloadManager& ) = delete;
        /** @brief 대입을 금지합니다. */
        LiveReloadManager& operator=( const LiveReloadManager& ) = delete;

        /** @brief 라이브 리로드 매니저를 종료하고 모든 모듈을 해제합니다. */
        void shutdown();

        /**
         * @brief 모듈을 등록하고 섀도 복사로 로드합니다.
         * @param moduleName DLL/SO 기본 이름(확장자 없음)
         * @param listDependsOn 이 모듈이 의존하는 모듈 이름(먼저 로드됩니다)
         */
        bool registerModule( string_view moduleName, const vector<string>& listDependsOn = {} );

        /** @brief 해당 모듈(과 그것에 의존하는 모듈)의 리로드를 예약합니다. */
        void triggerReload( string_view moduleName );

        /** @brief 파일 변경을 확인하고 예약된 리로드를 실행합니다. */
        void update();

        /** @brief 모듈이 리로드되기 직전에 호출될 델리게이트를 설정합니다. */
        void setOnBeforeReload( string_view moduleName, OnBeforeReloadDelegate delegate );

        /** @brief 모듈이 리로드된 직후에 호출될 델리게이트를 설정합니다. */
        void setOnAfterReload( string_view moduleName, OnAfterReloadDelegate delegate );
        /**
         * @brief 새 모듈 코드가 onAfterReload 안에서 결함(접근 위반 등)을 냈을 때 부를 콜백을 등록합니다.
         * @details onAfterReload 는 새 이미지의 코드가 처음 도는 자리(API 바인딩 · 인스턴스 생성 · 상태 복원)라 `ModuleCallGuard` 로
         *          지킵니다. 결함이 나면 그래프를 막고 이 콜백을 부릅니다 — 부르는 쪽은 그 모듈에서 받은 것(인스턴스 · API 표)을 **그 모듈을
         *          부르지 않고** 버려야 합니다. 반쯤 만들어진 인스턴스를 부수는 코드도 같은 모듈이기 때문입니다.
         */
        void setOnReloadFault( string_view moduleName, OnReloadFaultDelegate delegate );

        /**
         * @brief 연쇄 교체 대상이 모두 prepare 된 뒤, 첫 commit 직전에 한 번 불립니다.
         * @details 키트 DLL 을 언로드하기 전에 SWGame 을 먼저 내릴 때 씁니다.
         */
        void setOnBeforeCommitBatch( OnBeforeCommitBatchDelegate delegate );

        /**
         * @brief 모듈을 언로드하기 직전에 불려 워커 스레드를 비웁니다.
         * @details TaskManager 만으로는 부족합니다. RenderThread 는 App 소유라 Engine 에서 직접 볼 수 없으므로, App 이
         *          renderThread->waitIdle() / device.waitIdle() 을 여기에 연결해야 Present 훅이 실행 중인 이미지를 FreeLibrary 하지 않습니다.
         */
        void setDrainWorkers( DrainWorkersDelegate delegate );

        /**
         * @brief 모듈마다 걸어 둔 리로드 델리게이트를 **모두** 뗍니다.
         * @details 콜백은 보통 `ModuleHost` 의 메서드를 가리킵니다. 그 객체가 이 등록부보다 먼저 사라지므로 사라지기 전에 자기 것을
         *          떼야 하는데, **어떤 모듈에 걸었는지를 거는 쪽이 모두 알지는 못합니다**(게임플레이 키트는 설정 파일에서 옵니다).
         *          이름을 두 곳에 적는 대신, 아는 쪽(등록부)이 한 번에 떼어 줍니다.
         * @note 배치 · 비우기 델리게이트는 각자의 setter 로 떼십시오. 여기서는 건드리지 않습니다.
         */
        void clearReloadCallbacks();

        /** @brief DLL 그래프가 섞인 상태로 보고 이후 리로드를 막습니다. 프로세스를 다시 시작해야 합니다. */
        void markGraphBroken( string_view reason );
        /** @brief commit 실패 · 순환 · 바인딩 실패로 그래프가 깨졌으면 true 입니다. */
        bool isGraphBroken() const { return _bReloadGraphBroken == SW_TRUE; }

        /**
         * @brief 등록된 모듈마다, 의존 모듈이 **지금 이 매니저가 들고 있는 이미지**에 묶였는지 확인합니다.
         * @return 어긋난 결속이 하나도 없으면 true. 어긋나면 모듈 · 의존 이름과 양쪽 주소를 로그로 남기고 false 입니다.
         * @details 섀도 복사본은 원본과 파일 이름이 달라서, 의존 모듈이 어느 이미지에 묶일지는 로더가 정합니다. Windows 는 지연 로드
         *          훅(`DelayLoadNotifyHook.cpp`)이 지금의 복사본을 돌려주고, 리눅스는 SONAME 이 같은 **먼저 올라온** 이미지가 이깁니다.
         *          어긋나면 한 프로세스에서 같은 모듈이 두 벌 돌고(정적 상태 · 타입 등록이 갈린다), 옛 이미지를 내리는 순간 그리로 뛰는
         *          코드가 죽습니다. 등록과 연쇄 리로드 끝에 부르고, 어긋나면 그래프를 막습니다 — 섞인 채 조용히 도는 것보다 낫습니다.
         *          Windows 에서 아직 풀리지 않은 지연 로드는 어긋남이 아닙니다(풀릴 때 훅이 그때의 복사본을 돌려줍니다).
         *          리눅스는 모듈마다 구운 도장 상수(`sw_moduleEngineAbiStamp_<이름>`, `sw_registerDynamicModule` 이 넣는다)의 주소로
         *          가립니다.
         */
        bool verifyModuleBindings() const;

        /** @brief 지금 핫 리로드 배치를 처리 중인지 반환합니다. */
        bool isReloadingBatch() const { return _bReloadingBatch == SW_TRUE; }

        /** @brief 로드된 모듈의 핸들을 반환합니다. */
        void* getModuleHandle( string_view moduleName ) const;

        /** @brief 교체된 뒤 아직 올려 둔 옛 이미지 수입니다(`kMaxRetiredBatchCount` 배치까지). */
        uint32 getRetiredImageCount() const { return static_cast<uint32>( _listRetiredImage.size() ); }

        // --- IModuleHandleProvider: 모듈 DLL 안의 지연 로드 훅이 Engine.dll 을 거쳐 이것만 묻는다 ---
        /** @brief 리로드 그래프가 깨져 있으면 true 입니다. */
        bool isModuleGraphBroken() const override { return isGraphBroken(); }
        /** @brief 이름으로 이미 로드된 모듈 핸들을 찾습니다. */
        void* findLoadedModuleHandle( string_view moduleName ) const override { return getModuleHandle( moduleName ); }

    private:
        struct ModuleContext;

        struct PreparedShadow
        {
            void*                          _pHandle{ nullptr };
            string                         _tempPath;
            uint64                         _sourceMtime{ 0 };
            TypeRegistrar*                 _pTypeHead{ nullptr };
            EnumRegistrar*                 _pEnumHead{ nullptr };
            sw::ComponentFactoryRegistrar* _pFactoryHead{ nullptr };
            GlobalVariableRegistrar*       _pVariableHead{ nullptr };
            TypeRegistrar*                 _pPreviousTypeHead{ nullptr };
            EnumRegistrar*                 _pPreviousEnumHead{ nullptr };
            sw::ComponentFactoryRegistrar* _pPreviousFactoryHead{ nullptr };
            GlobalVariableRegistrar*       _pPreviousVariableHead{ nullptr };
        };

        /** @brief 섀도 복사본을 LoadLibrary 합니다. */
        bool loadShadowCopyModule( ModuleContext& ctx );
        /** @brief 섀도 복사본을 만들고 로드만 합니다(아직 교체하지 않습니다). */
        bool prepareShadowCopy( ModuleContext& ctx, PreparedShadow& out );
        /** @brief 섀도 핸들로 교체하고 콜백을 부릅니다. */
        bool commitShadowCopy( ModuleContext& ctx, PreparedShadow& prepared );
        /** @brief prepare 가 실패하면 새 이미지를 버리고, 바꿔 둔 SONAME 을 commit 된 이름으로 되돌립니다. */
        void abortShadowCopy( ModuleContext& ctx, PreparedShadow& prepared );
        /**
         * @brief (리눅스) 섀도 복사본의 SONAME 을 세대 이름으로, 의존 모듈의 NEEDED 를 그 의존의 **지금** 이름으로 바꿉니다.
         * @details 복사본은 원본의 SONAME 을 그대로 들고 있어서, 동적 링커는 SONAME 이 같은 **먼저 올라온** 이미지에 새 모듈을 묶습니다
         *          (연쇄 리로드의 prepare 에서는 그것이 아직 내려가지 않은 옛 이미지입니다). 이름을 세대마다 고유하게 하면 NEEDED 가
         *          가리키는 이미지가 하나뿐입니다. Windows 에서 지연 로드 훅이 하는 일의 짝입니다(위의 `ModuleImagePatch`).
         */
        void rewriteShadowSonames( ModuleContext& ctx, vector<uint8>& inoutBytes );
        /** @brief 모듈 핸들을 언로드합니다. */
        void unloadModule( ModuleContext& ctx );
        /** @brief 교체된 옛 이미지를 퇴역 목록에 올리고, 배치가 상한을 넘으면 가장 오래된 배치를 내립니다. */
        void retireImage( string_view moduleName, void* pHandle, string_view tempPath );
        /**
         * @brief 가장 오래된 퇴역 배치 하나를 내립니다. 배치 안에서는 나중에 퇴역한 것(의존하는 쪽)부터 내립니다.
         * @details 퇴역한 이미지는 **같은 배치의 퇴역 이미지나 지금 살아 있는 이미지에만** 묶여 있습니다(의존이 바뀌면 의존하는 모듈도 같은
         *          연쇄로 바뀐다). 그래서 오래된 배치부터 내려도 아직 쓰이는 이미지를 먼저 내리는 일이 없습니다.
         */
        void unloadOldestRetiredBatch();
        /**
         * @brief 언로드하기 전에 워커와 태스크를 비웁니다.
         * @return 제한 시간 안에 비웠으면 true. 실패하면 그래프를 깨진 상태로 표시하고 false 를 반환합니다.
         */
        bool drainTasksBeforeUnload();
        /** @brief root 와 그것에 의존하는 모듈 이름을 중복 없이 모읍니다. */
        void collectDependentClosure( string_view root, vector<string>& outListUnique ) const;
        /** @brief 의존 순서대로 위상 정렬합니다. 순환이 있으면 false 입니다. */
        bool topoSortSubgraph( const vector<string>& listName, vector<string>& outListOrdered ) const;
        /** @brief 부분 그래프를, 모든 prepare 가 성공한 뒤에만 commit 합니다. */
        void reloadCascade( const vector<string>& listSubgraphName );

        /**
         * @brief (리눅스) 모듈의 SONAME 세 가지입니다. 다른 플랫폼에서는 비어 있습니다.
         * @details `_original` 은 원본 파일의 이름이라 의존 모듈의 NEEDED 가 이것을 적고 있고, `_current` 는 의존 모듈이 **지금** NEEDED 에
         *          적어야 할 이름(prepare 가 세대 이름으로 바꾼다), `_loaded` 는 commit 된 복사본의 이름입니다. prepare 가 실패하면
         *          `_current` 를 `_loaded` 로 되돌립니다.
         */
        struct SonameState
        {
            string _original;
            string _current;
            string _loaded;
        };

        /** @brief 교체되어 내려갈 차례를 기다리는 옛 이미지입니다. */
        struct RetiredImage
        {
            string _moduleName;
            string _tempPath;
            void*  _pHandle{ nullptr };
            uint32 _batchId{ 0 };
        };

        /// @brief 등록된 모듈입니다(경로 · 핸들 · 의존 · 리로드 예약).
        struct ModuleContext
        {
            OnBeforeReloadDelegate _onBeforeReload;
            OnAfterReloadDelegate  _onAfterReload;
            OnReloadFaultDelegate  _onReloadFault;
            string                 _moduleName;
            string                 _originalModulePath;
            string                 _tempModulePath;
            vector<string>         _listDependsOn;
            SonameState            _soname;
            void*                  _pLibraryModule;
            uint64                 _loadedSourceMtime;
            uint64                 _debounceMtime;
            CpuTimer               _debounceTimer;
            atomic<bool>           _bPendingReload;
            atomic<bool>           _bMtimeDebouncing;
            atomic<bool>           _bForceReload;

            /** @brief 원자 플래그를 끈 기본값으로 만듭니다. */
            ModuleContext() noexcept;
            /** @brief 핸들과 경로를 옮겨 받습니다. */
            ModuleContext( ModuleContext&& other ) noexcept;
            /** @brief 이동 대입입니다. */
            ModuleContext& operator=( ModuleContext&& other ) noexcept;
        };

        static constexpr int32 kMtimeDebounceMs = 300;

        unordered_map<string, ModuleContext> _mapModule;
        unique_ptr<IFileWatcher>             _fileWatcher;
        OnBeforeCommitBatchDelegate          _onBeforeCommitBatch;
        DrainWorkersDelegate                 _drainWorkers;
        vector<RetiredImage>                 _listRetiredImage; ///< 교체된 옛 이미지. 오래된 것부터
        uint32                               _retireBatchId;    ///< 연쇄 리로드마다 오르는 배치 번호
        uint8                                _bReloadGraphBroken : 1;
        uint8                                _bReloadingBatch    : 1;
        [[maybe_unused]] uint8               _reserved           : 6;
    };
} // namespace sw
