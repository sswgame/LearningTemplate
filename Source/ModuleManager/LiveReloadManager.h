/**
 * @file ModuleManager/LiveReloadManager.h
 * @brief 모듈 공유 라이브러리를 섀도 복사해 핫 리로드합니다(의존 모듈까지 연쇄로 교체합니다).
 *
 * @note **이 클래스는 Engine 이 아니라 GameModuleManager 에 있습니다.** 모듈을 로드하고 교체하는 것은 런처(App, Server)의 일이고, Engine 은 모듈이라는 개념
 *       자체를 몰라야 합니다(Engine 레이어 규칙: Engine 은 Editor, GameFramework, Games 를 모릅니다). 쓰는 쪽은 GameModuleManager(GameModuleManager 와
 *       ModuleCompiler)와 App 의 단축키뿐이고, Shipping 빌드에서는 **파일째 빠집니다**(`Source/ModuleManager/CMakeLists.txt` 의 제외 목록).
 *
 *       이 클래스는 리로드 **순서**(prepare → 배치 콜백 → commit, 언로드 구간, 정적 등록 헤드, `RegistrationReloadScope`)를 정하고, 그 밖의 일은
 *       값 멤버나 도우미에 맡깁니다. 연쇄 교체 대상과 순서를 계산하는 `ModuleDependencyGraph`, 섀도 복사본 파일을 쓰고 지우는 `ModuleShadowCopy`,
 *       교체된 옛 이미지를 배치 단위로 남겨 두는 `DeferredImageUnloadQueue` 입니다.
 *       핫 리로드만 쓰는 도우미는 그 밖에 셋이 더 있습니다. 섀도 복사본의 파일 바이트를 읽고 고치는 `ModuleImagePatch`, 새 모듈 코드를
 *       처음 호출하는 지점을 보호하는 `ModuleCallGuard`, 복사본 파일 이름을 만들고 남은 것을 정리하는 `ShadowCopyName` 입니다. 모두 Shipping 에서 함께 빠집니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/stable_unordered_map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/File/FileUtil.h"
#include "Core/Time/GameTimer.h"

#include "Engine/Common/Common.h"
#include "Engine/Module/ModuleTypeRegistry.h"

#include "ModuleManager/DeferredImageUnloadQueue.h"
#include "ModuleManager/ModuleDependencyGraph.h"
#include "ModuleManager/ModuleShadowCopy.h"

namespace sw
{
    class IFileWatcher;
    /**
     * @brief 모듈을 섀도 경로에 복사해 로드하는 핫 리로드 매니저입니다.
     * @note 연쇄 교체는 의존 순서(위상 정렬)대로 합니다. 모든 모듈의 prepare 가 성공한 뒤에만 commit 합니다. 언로드는 위상 역순
     *       (의존하는 쪽 먼저)입니다.
     *       - **적용 전 실패는 옛 이미지를 그대로 두고 계속 돕니다**(그래프를 막지 않습니다). 엔진 ABI 스탬프 불일치, 로드 실패, 이미지 검사
     *         거절(`setOnValidateImage`), 배치 직전 콜백의 거절(`addOnBeforeCommitBatch`)이 여기에 해당합니다. 새 이미지가 상태를 넘겨받기 전이라 잃는 것이 없습니다.
     *       - **적용 뒤 결함은 되돌리지 않습니다.** commit 중 실패, onAfter 의 결함, 그래프 막힘은 남은 commit 을 멈추고 그래프를 막습니다
     *         (재시작 안내). 이미 교체된 DLL 은 되돌릴 수 없습니다. UE Live Coding 과 Unity 의 도메인 리로드도 적용 뒤 결함을 되돌리지 않습니다.
     *       - **콜백 등록은 모두 `Subscription` 을 돌려줍니다.** 등록한 쪽(모듈 호스트 · 서버)이 멤버로 들고, 사라지면 그 등록만 풀립니다 — 다른 쪽이 건 것을
     *         덮어쓰거나 통째로 비우는 길이 없습니다. 모듈마다의 칸은 그 모듈의 주인 하나가 걸고(다시 걸면 앞 구독은 아무것도 풀지 않는다),
     *         배치 직전 콜백은 여럿이 걸 수 있는 목록입니다.
     */
    class LiveReloadManager final : public SubscriptionSource
    {
    public:
        using OnBeforeReloadDelegate = Delegate<void()>;
        using OnAfterReloadDelegate  = Delegate<void( void* pLibraryModule )>;
        /** @brief onAfterReload 안에서 하드웨어 예외가 났을 때 불립니다. 인자는 예외 코드(Windows)나 시그널 번호(리눅스)입니다. */
        using OnReloadFaultDelegate = Delegate<void( uint32 faultCode )>;
        /**
         * @brief 새로 로드한 이미지를 받아들일지 묻습니다. 인자는 새 이미지의 핸들입니다. 옛 이미지가 아직 돌고 있을 때(prepare) 불립니다.
         * @details false 면 새 이미지를 언로드하고 옛 이미지를 그대로 둡니다. 그래프는 막지 않습니다(적용 전 실패).
         *          호스트가 모듈 API 테이블의 ABI 처럼 "옛 것을 언로드한 뒤에야 알 수 있던 거절 사유"를 여기서 미리 확인합니다.
         */
        using OnValidateImageDelegate = Delegate<bool( void* pLibraryModule )>;
        /**
         * @brief 연쇄 교체의 첫 commit 직전에 불립니다. false 면 아무것도 바꾸지 않고 옛 이미지를 모두 유지합니다(그래프는 막지 않습니다).
         * @details false 는 "아무것도 언로드하지 않았다"는 뜻이어야 합니다. 호스트가 상태를 저장하지 못했을 때(게임 상태 직렬화 실패) 씁니다.
         */
        using OnBeforeCommitBatchDelegate = Delegate<bool( const vector<string>& listModuleName )>;
        using DrainWorkersDelegate        = Delegate<void()>;

        /**
         * @brief 모듈을 언로드하기 전에 실행 중인 태스크를 비우는 제한 시간(ms)입니다. 넘으면 리로드 그래프를 깨진 상태로 표시합니다.
         * @details GameModuleManager::drainRenderWorkers(App 경로)와 drainTasksBeforeUnload(헤드리스 폴백)가 함께 쓴다. 백엔드 교체도 같은 비우기를 지난다.
         *          무거운 씬을 막 연 뒤(지형 · 식생 · 텍스처 업로드 태스크)나 다른 GPU 실행과 함께 돌 때는 5 초를 넘길 수 있다 — 넘기면 그래프가 깨져
         *          프로세스를 다시 띄워야 하므로, 정상 작업이 끝날 만큼 넉넉히 둔다(멈춘 태스크를 가려내는 그물일 뿐이다).
         */
        static constexpr uint32 kModuleDrainTimeoutMs = 30000;

        /**
         * @brief 교체된 옛 이미지의 언로드를 몇 번의 연쇄 리로드(배치)만큼 미룰지 정합니다.
         * @details 옛 코드를 가리키는 것(델리게이트, 함수 포인터, vtable, 문자열 리터럴)이 어딘가 남아 있어도, 이미지가 로드돼 있는
         *          동안은 크래시가 나지 않고 옛 동작이 한 번 더 돈다. 섀도 복사본이라 로드해 두어도 원본 파일은 잠기지 않는다.
         *          이보다 오래된 배치는 의존하는 쪽부터 언로드한다.
         */
        static constexpr uint32 kMaxDeferredUnloadBatchCount = 4;

        /** @brief 모듈 맵과 감시자가 빈 상태로 시작합니다. */
        LiveReloadManager();
        /** @brief 로드된 모듈을 언로드합니다. */
        ~LiveReloadManager();

        /** @brief 복사를 금지합니다. */
        LiveReloadManager( const LiveReloadManager& ) = delete;
        /** @brief 대입을 금지합니다. */
        LiveReloadManager& operator=( const LiveReloadManager& ) = delete;

        /** @brief 라이브 리로드 매니저를 종료하고 모든 모듈을 해제합니다. */
        void shutdown();

        /**
         * @brief 모듈을 등록하고 섀도 복사본으로 로드합니다.
         * @details 공용 모듈(`loadSharedModule` 로 올린 것)은 거절합니다 — 키트 · 게임보다 오래 산다는 전제를 지키는 단 한 곳입니다.
         * @param moduleName DLL/SO 기본 이름(확장자 없음)
         * @param listDependsOn 이 모듈이 의존하는 모듈 이름(먼저 로드됩니다)
         */
        [[nodiscard]] bool registerModule( string_view moduleName, const vector<string>& listDependsOn = {} );

        /**
         * @brief 핫 리로드하지 않는 공용 모듈(GameFramework)을 섀도 복사 없이 로드하고, 그 정적 레지스트리를 **제 이름으로** 등록합니다.
         * @details 키트와 SWGame 이 링크하는 공용 모듈을 **처음 부르는 쪽**이 로드하게 두면(Windows 지연 로드, 리눅스 첫 키트의 DT_NEEDED)
         *          그 정적 레지스트리(타입, 컴포넌트 팩토리, 전역 변수)가 그때 등록을 모으는 모듈(SWGame 이나 첫 키트)의 이름으로 들어간다.
         *          그러면 SWGame 의 팩토리 캐시를 덮어써서 그 뒤에 만든 씬에 SWGame 컴포넌트가 없어지고, 첫 SWGame 리로드가 공용 모듈의 컴포넌트와 타입을 모든
         *          씬에서 지운다. 공용 모듈은 다시 로드되지 않으므로 돌아오지도 않는다. 그래서 그 모듈을 링크하는 모듈을 등록하기 **전에** 이 함수를 부른다.
         *          로드하기 전에 엔진 ABI 스탬프를 대조한다. 이미지는 리로드하지 않고, 이 매니저가 `shutdown` 에서 그것을 링크하는 모듈을 모두 내린 뒤 마지막에 내린다(의존 모듈의 import 가 그 이미지를 가리킨다).
         * @return 로드했거나 이 매니저가 이미 로드했으면 true 입니다. 파일이 없으면(그 모듈을 쓰지 않는 구성) 아무것도 하지 않고 true 를 반환합니다.
         */
        [[nodiscard]] bool loadSharedModule( string_view moduleName );

        /**
         * @brief 이미 로드한 모듈의 리로드 직후 콜백(`setOnAfterReload`)을 부릅니다. 리로드 때와 같이 `ModuleCallGuard` 로 보호합니다.
         * @details 기동은 이미지 로드(기동 단계 `ModuleTypes`, 타입 등록)와 인스턴스 생성(RHI 이후)을 나눕니다. 로드할 때는 이 콜백이 아직
         *          등록되지 않았으므로, 인스턴스를 만드는 쪽이 콜백을 등록한 뒤 이 함수를 부릅니다.
         * @return 모듈이 로드돼 있고, 콜백이 결함 없이 돌았고, 그래프가 깨지지 않았으면 true 입니다.
         */
        [[nodiscard]] bool runAfterReload( string_view moduleName );

        /** @brief 해당 모듈(과 그것에 의존하는 모듈)의 리로드를 예약합니다. 게임 스레드(`update` 를 부르는 스레드)에서 부릅니다. */
        void triggerReload( string_view moduleName );

        /**
         * @brief 이 프로세스가 시작한 빌드(`ModuleCompiler`)가 시작됐음을 알립니다. 끝날 때까지 파일 감시가 본 모듈 변경을 적용하지 않습니다. 어느 스레드에서나 부를 수 있습니다.
         * @details 연쇄 빌드는 모듈 DLL 을 하나씩 다시 쓴다. mtime 이 디바운스 시간만큼 멈췄다고 적용하면, 의존하는 모듈의 링크가 아직 끝나지 않은
         *          반쯤 완성된 집합이 로드된다(헤더가 어긋나면 ABI 스탬프가 막지만, 같은 헤더의 반쯤 완성된 집합은 막지 못한다). 언리얼 Live Coding 도 빌드가
         *          끝난 뒤에 패치한다. 바깥에서 돌린 빌드(터미널, IDE)는 빌드 성공 스탬프(`config::kFileModuleBuildStamp`)가 바뀔 때만 올린다 — 빌드가 동적
         *          모듈을 모두 링크한 뒤에만 쓰는 파일이다(`sw_addModuleBuildStamp`). 모듈 DLL 의 쓰기 자체는 리로드를 부르지 않는다.
         */
        void notifyBuildStarted();
        /**
         * @brief `notifyBuildStarted` 로 알린 빌드가 끝났음을 알립니다. 어느 스레드에서나 부를 수 있으며, 다음 `update` 가 게임 스레드에서 처리합니다.
         * @param bSucceeded 성공이면 기다리던 변경을 적용합니다(@p targetName 이 등록된 모듈이면 바뀌지 않았어도 다시 로드합니다). 실패나 취소면
         *        그 빌드가 쓴 변경을 버립니다(반쯤 링크된 집합을 로드하지 않습니다. 다음에 성공한 빌드가 적용합니다).
         */
        void notifyBuildFinished( bool bSucceeded, string_view targetName );

        /**
         * @brief 파일 변경을 확인하고 예약된 리로드를 실행합니다.
         * @details 바깥 빌드는 모듈 폴더의 빌드 성공 스탬프가 바뀐 뒤에만 본다. 그때 원본 mtime 이 로드한 것보다 새 모듈을 모두 연쇄 교체한다.
         *          스탬프가 없는 빌드 트리(구성하기 전)에서는 바깥 빌드를 올리지 않는다(등록할 때 한 번 알린다).
         */
        void update();

        /** @brief 모듈이 리로드되기 직전에 호출할 델리게이트를 겁니다. 구독이 사라지면 풀립니다. */
        [[nodiscard]] Subscription setOnBeforeReload( string_view moduleName, OnBeforeReloadDelegate delegate );

        /** @brief 모듈이 리로드된 직후에 호출할 델리게이트를 겁니다. 구독이 사라지면 풀립니다. */
        [[nodiscard]] Subscription setOnAfterReload( string_view moduleName, OnAfterReloadDelegate delegate );
        /**
         * @brief 새 모듈 코드가 onAfterReload 안에서 결함(접근 위반 등)을 냈을 때 부를 콜백을 겁니다. 구독이 사라지면 풀립니다.
         * @details onAfterReload 는 새 이미지의 코드가 처음 도는 지점(API 바인딩, 인스턴스 생성, 상태 복원)이라 `ModuleCallGuard` 로
         *          보호한다. 결함이 나면 그래프를 막고 이 콜백을 부른다. 부르는 쪽은 그 모듈에서 받은 것(인스턴스, API 테이블)을 **그 모듈을
         *          부르지 않고** 버려야 한다. 반쯤 만들어진 인스턴스를 파괴하는 코드도 같은 모듈에 있기 때문이다.
         */
        [[nodiscard]] Subscription setOnReloadFault( string_view moduleName, OnReloadFaultDelegate delegate );
        /** @brief 새 이미지를 옛 이미지가 언로드되기 전에 검사할 콜백을 겁니다(`OnValidateImageDelegate`). 구독이 사라지면 풀립니다. */
        [[nodiscard]] Subscription setOnValidateImage( string_view moduleName, OnValidateImageDelegate delegate );

        /**
         * @brief 연쇄 교체 대상이 모두 prepare 된 뒤 첫 commit 직전에 부를 콜백을 목록에 더합니다. 구독이 사라지면 그 항목만 빠집니다.
         * @details **나중에 더한 것이 먼저 불린다** — 바깥 층(서버 서비스, 모듈 호스트 뒤에 선다)이 안쪽 층(게임 언로드)보다 먼저 내려간다.
         *          하나라도 false 면 거기서 멈추고 이번 연쇄를 취소해 옛 이미지를 모두 유지한다(앞에서 멈춘 것은 다음 틱에 스스로 다시 선다).
         */
        [[nodiscard]] Subscription addOnBeforeCommitBatch( OnBeforeCommitBatchDelegate delegate );

        /**
         * @brief 모듈을 언로드하기 직전에 불려 워커 스레드를 비울 콜백을 겁니다. 구독이 사라지면 풀립니다.
         * @details TaskManager 만으로는 부족하다. RenderThread 는 App 소유라 Engine 에서 직접 볼 수 없으므로, 모듈 호스트가
         *          renderThread->waitIdle() 와 device.waitIdle() 을 여기에 연결해야 Present 훅이 실행 중인 이미지를 FreeLibrary 하지 않는다.
         */
        [[nodiscard]] Subscription setDrainWorkers( DrainWorkersDelegate delegate );

        /** @brief DLL 그래프가 섞인 상태로 보고 이후 리로드를 막습니다. 프로세스를 다시 시작해야 합니다. */
        void markGraphBroken( string_view reason );
        /** @brief commit 실패, 순환, 바인딩 실패로 그래프가 깨졌으면 true 를 반환합니다. */
        bool isGraphBroken() const { return _bReloadGraphBroken == SW_TRUE; }

        /**
         * @brief 등록된 모듈마다, 의존 모듈이 **지금 이 매니저가 보관한 이미지**에 바인딩됐는지 확인합니다.
         * @return 어긋난 바인딩이 하나도 없으면 true 입니다. 어긋나면 모듈과 의존 이름, 양쪽 주소를 로그로 남기고 false 를 반환합니다.
         * @details 의존 모듈은 이름으로 바인딩되고, prepare 가 복사본마다 세대 이름을 주고 의존하는 복사본이 그 이름을 부르게 바꾼다
         *          (`ModuleShadowCopy::writeShadowCopy`). 바꾸기가 빠지면 Windows 는 원본 DLL 을 한 벌 더 로드하고, 리눅스는 SONAME 이 같은 **먼저 로드된**
         *          이미지에 바인딩된다. 어긋나면 한 프로세스에서 같은 모듈이 두 벌 돌고(정적 상태와 타입 등록이 갈린다), 옛 이미지를 언로드하는 순간
         *          거기로 점프하는 코드가 죽는다. 등록과 연쇄 리로드 끝에 호출하고, 어긋나면 그래프를 막는다. 섞인 채 조용히 도는 것보다 낫기 때문이다.
         *          Windows 는 import 테이블 첫 항목이 가리키는 이미지로, 리눅스는 모듈마다 박힌 스탬프 상수(`sw_moduleEngineABIStamp_<이름>`,
         *          `sw_registerDynamicModule` 이 넣는다)의 주소로 판단한다.
         */
        bool verifyModuleBindings() const;

        /** @brief 로드된 모듈의 핸들을 반환합니다. */
        void* getModuleHandle( string_view moduleName ) const;

        /** @brief 끝까지 성공한 연쇄 리로드 수입니다(교체 · 콜백 · 바인딩 검사를 모두 마친 것). 실패하거나 적용 전에 거절된 리로드는 세지 않습니다. 자동화가 완료를 기다릴 때 읽습니다. */
        uint32 getCompletedReloadCount() const { return _completedReloadCount; }
        /** @brief 교체된 뒤 언로드를 미루고 아직 로드해 둔 옛 이미지 수입니다(`kMaxDeferredUnloadBatchCount` 배치까지). */
        uint32 getDeferredUnloadImageCount() const { return _deferredUnloadQueue.getImageCount(); }

    protected:
        void releaseSubscription( uint64 id ) override;

    private:
        struct ModuleRecord;

        /** @brief 구독 하나가 가리키는 칸의 종류입니다. */
        enum class CallbackSlot : uint8
        {
            BeforeReload,
            AfterReload,
            ReloadFault,
            ValidateImage,
            BeforeCommitBatch,
            DrainWorkers,
        };
        /** @brief 살아 있는 구독 하나 — 어느 모듈(배치 · 비우기는 빈 이름)의 어느 칸인지입니다. */
        struct CallbackRegistration
        {
            uint64       _id;
            string       _moduleName;
            CallbackSlot _slot;
        };
        /** @brief `loadSharedModule` 이 올린 공용 모듈 이미지입니다. */
        struct SharedImage
        {
            string _name;
            void*  _pHandle{ nullptr }; ///< 섀도 복사 없이 원본 경로로 올린 이미지
        };

        /** @brief 배치 직전 콜백 목록의 항목입니다. */
        struct BeforeCommitBatchEntry
        {
            uint64                      _id;
            OnBeforeCommitBatchDelegate _delegate;
        };

        /** @brief @p moduleName 의 @p slot 칸을 새로 걸 구독을 냅니다. 같은 칸의 앞 등록은 표에서 빠집니다(앞 구독은 아무것도 풀지 않게 된다). */
        [[nodiscard]] Subscription issueSlotSubscription( string_view moduleName, CallbackSlot slot );
        /** @brief @p registration 이 가리키는 칸을 비웁니다. */
        void clearSlot( const CallbackRegistration& registration );

        struct PreparedShadow
        {
            void*                        _pHandle{ nullptr };
            string                       _tempPath;
            uint64                       _sourceMtime{ 0 };
            engine::ModuleRegistrarHeads _heads;         ///< 로드한 이미지의 정적 등록자 머리(커밋 때 올린다)
            engine::ModuleRegistrarHeads _previousHeads; ///< 로드 전 전역 머리(중단하면 되돌린다)
        };

        /** @brief 연쇄 리로드에서 준비를 마친 모듈 하나(기록과 로드해 둔 새 이미지)입니다. */
        struct PreparedEntry
        {
            ModuleRecord*  _pRecord{ nullptr };
            PreparedShadow _shadow;
        };

        /** @brief 섀도 복사본을 LoadLibrary 로 로드합니다. */
        [[nodiscard]] bool loadShadowCopyModule( ModuleRecord& context );
        /** @brief 섀도 복사본을 만들고 로드만 합니다(아직 교체하지 않습니다). */
        [[nodiscard]] bool prepareShadowCopy( ModuleRecord& context, PreparedShadow& outShadow );
        /** @brief 섀도 핸들로 교체하고 콜백을 부릅니다. */
        bool commitShadowCopy( ModuleRecord& context, PreparedShadow& prepared );
        /** @brief @p context 의 리로드 직후 콜백을 `ModuleCallGuard` 안에서 부릅니다. 결함이 나면 그래프를 막고 결함 콜백을 부릅니다. */
        void invokeAfterReload( ModuleRecord& context );
        /** @brief prepare 가 실패하면 새 이미지를 버리고, 바꿔 둔 SONAME 을 commit 된 이름으로 되돌립니다. */
        void abortShadowCopy( ModuleRecord& context, PreparedShadow& prepared );
        /** @brief @p context 의 섀도 복사본을 쓰는 데 필요한 입력(의존 모듈과 등록된 모듈의 이미지 이름)을 채웁니다. */
        void populateShadowCopyParams( const ModuleRecord& context, ShadowCopyParams& outParams ) const;
        /** @brief 모듈 핸들을 언로드합니다. */
        void unloadModule( ModuleRecord& context );
        /**
         * @brief `loadSharedModule` 로 올린 공용 모듈을 올린 순서의 거꾸로 내립니다(등록을 제 이름으로 해제하고 이미지를 놓는다).
         * @details 공용 모듈은 그것을 링크하는 모듈보다 오래 살아야 하므로 `shutdown` 이 리로드 대상을 모두 내린 **뒤에** 부른다. 이미지는 프로세스 전역이라 매니저가 놓지 않으면
         *          다음 매니저(테스트 · 재기동)가 같은 모듈을 다시 올리며 정적 등록(데이터 종류 · 감시 값 · 단계 · 개발 명령 · 서비스 팩토리)이 이름 중복으로 거절된다.
         *          엔진 서비스가 이미 내려갔으면 등록을 해제할 곳이 없어 이미지를 그대로 둔다(프로세스 끝까지).
         */
        void unloadSharedModules();
        /**
         * @brief 언로드하기 전에 워커와 태스크를 비웁니다.
         * @return 제한 시간 안에 비웠으면 true. 실패하면 그래프를 깨진 상태로 표시하고 false 를 반환합니다.
         */
        bool drainTasksBeforeUnload();
        /** @brief 부분 그래프를, 모든 prepare 가 성공한 뒤에만 commit 합니다. */
        void reloadCascade( const vector<string>& listSubgraphName );
        /** @brief @p listOrder(의존 순서)의 모듈마다 섀도 복사본을 준비합니다. 하나라도 실패하면 오류를 남기고 false 입니다(준비한 것은 @p outListPrepared 에 남아 있어 부르는 쪽이 버린다). */
        [[nodiscard]] bool prepareCascade( const vector<string>& listOrder, vector<PreparedEntry>& outListPrepared );
        /** @brief @p inoutListPrepared 의 @p firstIndex 번째부터 끝까지 새 이미지를 버립니다. */
        void abortPrepared( vector<PreparedEntry>& inoutListPrepared, size_t firstIndex );
        /**
         * @brief 준비한 모듈을 차례로 교체합니다. 그래프가 이미 깨졌거나 교체가 실패하면 남은 것을 버리고, 일부만 교체됐으면 그래프를 막습니다.
         * @return 모두 교체했으면 true 입니다.
         */
        [[nodiscard]] bool commitCascade( vector<PreparedEntry>& inoutListPrepared );
        /** @brief @p moduleName 의 컨텍스트를 돌려줍니다. 없으면 만들고 의존 그래프에도 이름을 넣습니다(둘의 이름 집합을 같게 둡니다). */
        ModuleRecord& getOrCreateRecord( string_view moduleName );
        /** @brief 모든 모듈 기록과 의존 그래프를 비웁니다. */
        void clearRecords();

        /// @brief 등록된 모듈입니다(경로 · 핸들 · 리로드 예약). 의존 목록은 `_dependencyGraph` 에 있습니다.
        struct ModuleRecord
        {
            OnBeforeReloadDelegate  _onBeforeReload;
            OnAfterReloadDelegate   _onAfterReload;
            OnReloadFaultDelegate   _onReloadFault;
            OnValidateImageDelegate _onValidateImage;
            string                  _moduleName;
            string                  _originalModulePath;
            string                  _tempModulePath;
            ShadowImageNameState    _imageName;
            void*                   _pLibraryModule;
            uint64                  _loadedSourceMtime;
            uint64                  _debounceMtime;
            GameTimer               _debounceTimer;
            atomic<bool>            _bPendingReload;
            atomic<bool>            _bMtimeDebouncing;
            atomic<bool>            _bForceReload;

            /**
             * @brief 원자 플래그를 끈 기본값으로 만듭니다. 복사도 이동도 없다 — 컨텍스트는 `_mapModule`(주소 고정 맵)이 한 번 만든 자리에서 산다.
             *        연쇄 리로드가 `ModuleRecord*` 를 쥔 채 새 모듈 등록이 끼어들어도 그 주소는 그대로다.
             */
            ModuleRecord() noexcept;
        };

        static constexpr int32 kMtimeDebounceMs = 300;

        /** @brief 빌드 쪽(`notifyBuildStarted` · `notifyBuildFinished`, 빌드 스레드)이 쓰고 `update` 가 읽는 상태입니다. `_buildMutex` 가 지킵니다. */
        struct BuildNotice
        {
            string _targetName;                ///< 끝난 빌드의 타깃(비면 전체 빌드)
            bool   _bInProgress{ false };      ///< 빌드가 도는 중이다
            bool   _bFinishedPending{ false }; ///< 끝났는데 `update` 가 아직 처리하지 않았다
            bool   _bSucceeded{ false };       ///< 끝난 빌드가 성공했다
        };

        /** @brief 끝난 빌드를 게임 스레드에서 처리합니다. 빌드가 도는 중이면 true 입니다(그동안 변경을 올리지 않는다). */
        bool consumeBuildNotice();

        stable_unordered_map<string, ModuleRecord> _mapModule;        ///< 이름 집합은 `_dependencyGraph` 와 같다(`getOrCreateRecord` · `clearRecords`). 컨텍스트 주소가 고정이다
        ModuleDependencyGraph                      _dependencyGraph;  ///< 모듈 이름 → 의존 모듈 이름. 연쇄 교체 대상과 순서를 계산한다
        vector<SharedImage>                        _listSharedModule; ///< `loadSharedModule` 로 올린 공용 모듈(올린 순서 — 리로드 대상을 모두 내린 뒤 `shutdown` 이 거꾸로 내린다)
        unique_ptr<IFileWatcher>                   _fileWatcher;      ///< 모듈 폴더(하위 폴더 없이)를 본다. 첫 `registerModule` 이 시작한다
        string                                     _buildStampPath;   ///< 바깥 빌드의 성공 스탬프(`Bin/Modules/` + `config::kFileModuleBuildStamp`). 감시를 시작할 때 정한다
        FileStamp                                  _seenBuildStamp;   ///< 마지막으로 본 스탬프. 이것과 달라지면 바깥 빌드가 끝난 것이다
        mutable mutex                              _buildMutex;
        BuildNotice                                _buildNotice;
        vector<BeforeCommitBatchEntry>             _listBeforeCommitBatch; ///< 배치 직전 콜백(더한 순서 — 부를 때는 거꾸로)
        DrainWorkersDelegate                       _drainWorkers;
        vector<CallbackRegistration>               _listCallbackRegistration; ///< 살아 있는 구독이 가리키는 칸
        uint64                                     _nextCallbackID;           ///< 다음 구독 번호(0 은 빈 구독)
        DeferredImageUnloadQueue                   _deferredUnloadQueue;      ///< 언로드를 미룬 옛 이미지(`kMaxDeferredUnloadBatchCount` 배치까지)
        uint32                                     _completedReloadCount;     ///< 끝까지 성공한 연쇄 리로드 수(`getCompletedReloadCount`)
        uint8                                      _bReloadGraphBroken : 1;
        uint8                                      _bBuildStampPending : 1; ///< 스탬프가 바뀌었는데 이 프로세스의 빌드가 도는 중이라 아직 보지 않았다
        [[maybe_unused]] uint8                     _reserved           : 6;
    };
} // namespace sw
