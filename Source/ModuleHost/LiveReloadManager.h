/**
 * @file LiveReloadManager.h
 * @brief 모듈 공유 라이브러리를 섀도 복사해 핫 리로드합니다(의존 모듈까지 연쇄로 교체합니다).
 *
 * @note **여기는 Engine 이 아니라 ModuleHost 입니다.** 모듈을 로드하고 교체하는 것은 런처(App · Server)의 일이고, Engine 은 모듈이라는 개념
 *       자체를 몰라야 합니다(Engine 레이어 규칙: Engine 은 Editor · GameFramework · Games 를 모릅니다). 쓰는 쪽은 ModuleHost(ModuleHost ·
 *       ModuleCompiler)와 App 의 단축키뿐이고, Shipping 빌드에서는 **파일째 빠집니다**(`Source/ModuleHost/CMakeLists.txt` 의 제외 목록).
 *
 *       핫 리로드만 쓰는 도우미 셋은 각자 파일에 있습니다 — 섀도 복사본의 파일 바이트를 읽고 고치는 `ModuleImagePatch`, 새 모듈 코드를
 *       처음 부르는 자리를 지키는 `ModuleCallGuard`, 복사본 파일 이름을 짓고 남은 것을 치우는 `ShadowCopyName`. Shipping 에서 함께 빠집니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Time/GameTimer.h"

#include "Engine/Common/Common.h"
#include "Engine/Module/ModuleHandleProvider.h"

namespace sw
{
    struct EnumRegistrar;
    struct GlobalVariableRegistrar;
    struct TypeRegistrar;
} // namespace sw

namespace sw
{
    class IFileWatcher;
    /**
     * @brief 모듈을 섀도 경로에 복사해 로드하는 핫 리로드 매니저입니다.
     * @note 연쇄 교체는 의존 순서(위상 정렬)대로 합니다. 모든 모듈의 prepare 가 성공한 뒤에만 commit 합니다. 언로드는 위상 역순
     *       (의존하는 쪽 먼저)입니다.
     *       - **적용 전 실패는 옛 이미지를 그대로 두고 계속 돕니다**(그래프를 막지 않는다): 엔진 ABI 도장 불일치 · 로드 실패 · 이미지 검사
     *         거절(`setOnValidateImage`) · 배치 직전 콜백의 거절(`setOnBeforeCommitBatch`). 새 이미지가 상태를 넘겨받기 전이라 잃는 것이 없습니다.
     *       - **적용 뒤 결함은 되돌리지 않습니다**: commit 중 실패 · onAfter 의 결함이나 그래프 막음은 남은 commit 을 멈추고 그래프를 막습니다
     *         (재시작 안내). 이미 교체된 DLL 은 되돌릴 수 없습니다 — UE Live Coding · Unity 의 도메인 리로드도 적용 뒤 결함을 되돌리지 않습니다.
     */
    class LiveReloadManager final : public IModuleHandleProvider
    {
    public:
        using OnBeforeReloadDelegate = Delegate<void()>;
        using OnAfterReloadDelegate  = Delegate<void( void* pLibraryModule )>;
        /** @brief onAfterReload 안에서 하드웨어 예외가 났을 때 불립니다. 인자는 예외 코드(Windows) · 시그널 번호(리눅스)입니다. */
        using OnReloadFaultDelegate = Delegate<void( uint32 faultCode )>;
        /**
         * @brief 새로 올린 이미지를 받아들일지 묻습니다. 인자는 새 이미지의 핸들입니다. 옛 이미지가 아직 돌고 있을 때(prepare) 불립니다.
         * @details false 면 새 이미지를 내리고 옛 이미지를 그대로 둡니다 — 그래프는 막지 않습니다(적용 전 실패). 호스트가 모듈 API 표의 ABI 처럼
         *          "옛 것을 내린 뒤에야 알던 거절 사유" 를 여기서 미리 봅니다.
         */
        using OnValidateImageDelegate = Delegate<bool( void* pLibraryModule )>;
        /**
         * @brief 연쇄 교체의 첫 commit 직전에 불립니다. false 면 아무것도 바꾸지 않고 옛 이미지를 모두 둡니다(그래프는 막지 않는다).
         * @details false 는 "아무것도 내리지 않았다" 는 뜻이어야 합니다 — 호스트가 상태를 찍지 못했을 때(게임 상태 직렬화 실패) 씁니다.
         */
        using OnBeforeCommitBatchDelegate = Delegate<bool( const vector<string>& listModuleName )>;
        using DrainWorkersDelegate        = Delegate<void()>;

        /**
         * @brief 모듈을 언로드하기 전에 실행 중인 태스크를 비우는 제한 시간(ms)입니다. 넘으면 리로드 그래프를 깨진 상태로 표시합니다.
         * @details ModuleHost::drainRenderWorkers(App 경로)와 drainTasksBeforeUnload(헤드리스 폴백)가 함께 씁니다.
         */
        static constexpr uint32 kModuleDrainTimeoutMs = 5000;

        /**
         * @brief 교체된 옛 이미지의 언로드를 몇 번의 연쇄 리로드(배치)만큼 미룰지입니다.
         * @details 옛 코드를 가리키는 것(델리게이트 · 함수 포인터 · vtable · 문자열 리터럴)이 어딘가 남아 있어도, 이미지가 올라와 있는
         *          동안은 크래시가 아니라 옛 동작이 한 번 더 돕니다. 섀도 복사본이라 올려 두어도 원본 파일은 잠기지 않습니다. 이보다
         *          오래된 배치는 의존하는 쪽부터 내립니다(Windows 지연 로드는 참조 수를 올리지 않으므로 순서를 손으로 지킵니다).
         */
        static constexpr uint32 kMaxDeferredUnloadBatchCount = 4;

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

        /**
         * @brief 핫 리로드하지 않는 공용 모듈(GameFramework)을 섀도 복사 없이 올리고, 그 정적 등록기를 **제 이름으로** 등록합니다.
         * @details 키트 · SWGame 이 링크하는 공용 모듈을 **처음 부르는 쪽**이 올리게 두면(Windows 지연 로드 · 리눅스 첫 키트의 DT_NEEDED)
         *          그 정적 등록기(타입 · 컴포넌트 팩토리 · 전역 변수)가 그때 등록을 모으는 모듈(SWGame · 첫 키트)의 이름으로 들어가,
         *          SWGame 의 팩토리 캐시를 덮고(그 뒤에 만든 씬에 SWGame 컴포넌트가 없다) 첫 SWGame 리로드가 공용 모듈의 컴포넌트와 타입을 모든
         *          씬에서 지운다 — 공용 모듈은 다시 올라오지 않으므로 돌아오지 않는다. 그래서 그 모듈을 링크하는 모듈을 등록하기 **전에** 부릅니다.
         *          올리기 전에 엔진 ABI 도장을 대조합니다. 이미지는 프로세스가 끝날 때까지 둡니다(의존 모듈의 import 가 그 이미지를 가리킨다).
         * @return 올렸거나 이 매니저가 이미 올렸으면 true. 파일이 없으면(그 모듈을 쓰지 않는 구성) 아무것도 하지 않고 true 입니다.
         */
        [[nodiscard]] bool loadSharedModule( string_view moduleName );

        /**
         * @brief 이미 올린 모듈의 리로드 직후 콜백(`setOnAfterReload`)을 부릅니다. 리로드 때와 같이 `ModuleCallGuard` 로 지킵니다.
         * @details 기동은 이미지 올리기(기동 단계 `ModuleTypes` — 타입 등록)와 인스턴스 만들기(RHI 뒤)를 나눕니다. 올릴 때는 이 콜백이 아직
         *          걸려 있지 않으므로, 인스턴스를 만드는 쪽이 콜백을 건 뒤 이것을 부릅니다.
         * @return 모듈이 올라 있고, 콜백이 결함 없이 돌았고, 그래프가 깨지지 않았으면 true 입니다.
         */
        [[nodiscard]] bool runAfterReload( string_view moduleName );

        /** @brief 해당 모듈(과 그것에 의존하는 모듈)의 리로드를 예약합니다. 게임 스레드(`update` 를 부르는 스레드)에서 부릅니다. */
        void triggerReload( string_view moduleName );

        /**
         * @brief 이 프로세스가 시킨 빌드(`ModuleCompiler`)가 시작됐습니다. 끝날 때까지 파일 감시가 본 모듈 변경을 올리지 않습니다. 아무 스레드에서나 부릅니다.
         * @details 연쇄 빌드는 모듈 DLL 을 하나씩 다시 씁니다. mtime 이 디바운스 시간만큼 멈췄다고 올리면, 의존하는 모듈의 링크가 아직 끝나지 않은
         *          반쯤 된 집합이 올라갑니다(헤더가 어긋나면 ABI 도장이 막지만, 같은 헤더의 반쯤 된 집합은 막지 못한다). 언리얼 Live Coding 도 빌드가
         *          끝난 뒤에 패치합니다. 바깥에서 돌린 빌드(터미널 · IDE)는 알 길이 없어 지금처럼 mtime 디바운스만 봅니다.
         */
        void notifyBuildStarted();
        /**
         * @brief `notifyBuildStarted` 의 빌드가 끝났습니다. 아무 스레드에서나 부릅니다 — 다음 `update` 가 게임 스레드에서 처리합니다.
         * @param bSucceeded 성공이면 기다리던 변경을 올리고(@p targetName 이 등록된 모듈이면 그것은 바뀌지 않았어도 다시 올린다), 실패 · 취소면
         *        그 빌드가 쓴 변경을 버립니다(반쯤 링크된 집합을 올리지 않는다 — 다음 성공한 빌드가 올린다).
         */
        void notifyBuildFinished( bool bSucceeded, string_view targetName );

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
        /** @brief 새 이미지를 옛 이미지가 내려가기 전에 검사할 콜백을 등록합니다(`OnValidateImageDelegate`). */
        void setOnValidateImage( string_view moduleName, OnValidateImageDelegate delegate );

        /**
         * @brief 연쇄 교체 대상이 모두 prepare 된 뒤, 첫 commit 직전에 한 번 불립니다.
         * @details 키트 DLL 을 언로드하기 전에 SWGame 을 먼저 내릴 때 씁니다. false 를 돌려주면 이번 연쇄를 거두고 옛 이미지를 모두 둡니다.
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
         *          리눅스는 모듈마다 박힌 도장 상수(`sw_moduleEngineAbiStamp_<이름>`, `sw_registerDynamicModule` 이 넣는다)의 주소로
         *          가립니다.
         */
        bool verifyModuleBindings() const;

        /** @brief 로드된 모듈의 핸들을 반환합니다. */
        void* getModuleHandle( string_view moduleName ) const;

        /** @brief 교체된 뒤 언로드를 미루고 아직 올려 둔 옛 이미지 수입니다(`kMaxDeferredUnloadBatchCount` 배치까지). */
        uint32 getDeferredUnloadImageCount() const { return static_cast<uint32>( _listDeferredUnloadImage.size() ); }

        // --- IModuleHandleProvider: 모듈 DLL 안의 지연 로드 훅이 Engine.dll 을 거쳐 이것만 묻는다 ---
        /** @brief 리로드 그래프가 깨져 있으면 true 입니다. */
        bool isModuleGraphBroken() const override { return isGraphBroken(); }
        /** @brief 이름으로 이미 로드된 모듈 핸들을 찾습니다. */
        void* findLoadedModuleHandle( string_view moduleName ) const override { return getModuleHandle( moduleName ); }

    private:
        struct ModuleContext;

        struct PreparedShadow
        {
            void*                    _pHandle{ nullptr };
            string                   _tempPath;
            uint64                   _sourceMtime{ 0 };
            TypeRegistrar*           _pTypeHead{ nullptr };
            EnumRegistrar*           _pEnumHead{ nullptr };
            GlobalVariableRegistrar* _pVariableHead{ nullptr };
            TypeRegistrar*           _pPreviousTypeHead{ nullptr };
            EnumRegistrar*           _pPreviousEnumHead{ nullptr };
            GlobalVariableRegistrar* _pPreviousVariableHead{ nullptr };
        };

        /** @brief 섀도 복사본을 LoadLibrary 합니다. */
        [[nodiscard]] bool loadShadowCopyModule( ModuleContext& ctx );
        /** @brief 섀도 복사본을 만들고 로드만 합니다(아직 교체하지 않습니다). */
        bool prepareShadowCopy( ModuleContext& ctx, PreparedShadow& out );
        /** @brief 섀도 핸들로 교체하고 콜백을 부릅니다. */
        bool commitShadowCopy( ModuleContext& ctx, PreparedShadow& prepared );
        /** @brief @p ctx 의 리로드 직후 콜백을 `ModuleCallGuard` 안에서 부릅니다. 결함이 나면 그래프를 막고 결함 콜백을 부릅니다. */
        void invokeAfterReload( ModuleContext& ctx );
        /** @brief prepare 가 실패하면 새 이미지를 버리고, 바꿔 둔 SONAME 을 commit 된 이름으로 되돌립니다. */
        void abortShadowCopy( ModuleContext& ctx, PreparedShadow& prepared );
        /**
         * @brief (리눅스) 섀도 복사본의 SONAME 을 세대 이름으로, 의존 모듈의 NEEDED 를 그 의존의 **지금** 이름으로 바꿉니다.
         * @details 복사본은 원본의 SONAME 을 그대로 들고 있어서, 동적 링커는 SONAME 이 같은 **먼저 올라온** 이미지에 새 모듈을 묶습니다
         *          (연쇄 리로드의 prepare 에서는 그것이 아직 내려가지 않은 옛 이미지입니다). 이름을 세대마다 고유하게 하면 NEEDED 가
         *          가리키는 이미지가 하나뿐입니다. Windows 에서 지연 로드 훅이 하는 일의 짝입니다(`ModuleImagePatch`).
         */
        void rewriteShadowSonames( ModuleContext& ctx, vector<uint8>& inoutBytes );
        /** @brief 모듈 핸들을 언로드합니다. */
        void unloadModule( ModuleContext& ctx );
        /** @brief 교체된 옛 이미지를 지연 언로드 목록에 올리고, 배치가 상한을 넘으면 가장 오래된 배치를 내립니다. */
        void deferImageUnload( string_view moduleName, void* pHandle, string_view tempPath, bool bKeepMapped );
        /**
         * @brief 언로드를 미뤄 둔 가장 오래된 배치 하나를 내립니다. 배치 안에서는 나중에 목록에 오른 것(의존하는 쪽)부터 내립니다.
         * @details 언로드를 미룬 이미지는 **같은 배치의 미룬 이미지나 지금 살아 있는 이미지에만** 묶여 있습니다(의존이 바뀌면 의존하는
         *          모듈도 같은 연쇄로 바뀐다). 그래서 오래된 배치부터 내려도 아직 쓰이는 이미지를 먼저 내리는 일이 없습니다.
         */
        void unloadOldestDeferredBatch();
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
        struct DeferredUnloadImage
        {
            string _moduleName;
            string _tempPath;
            void*  _pHandle{ nullptr };
            uint32 _batchID{ 0 };
            bool   _bKeepMapped{ false }; ///< 다른 코드가 아직 구독하는 이벤트 채널을 만든 이미지 — 내리지 않는다(`ModuleImageUtil::releaseModuleCode`)
        };

        /// @brief 등록된 모듈입니다(경로 · 핸들 · 의존 · 리로드 예약).
        struct ModuleContext
        {
            OnBeforeReloadDelegate  _onBeforeReload;
            OnAfterReloadDelegate   _onAfterReload;
            OnReloadFaultDelegate   _onReloadFault;
            OnValidateImageDelegate _onValidateImage;
            string                  _moduleName;
            string                  _originalModulePath;
            string                  _tempModulePath;
            vector<string>          _listDependsOn;
            SonameState             _soname;
            void*                   _pLibraryModule;
            uint64                  _loadedSourceMtime;
            uint64                  _debounceMtime;
            GameTimer               _debounceTimer;
            atomic<bool>            _bPendingReload;
            atomic<bool>            _bMtimeDebouncing;
            atomic<bool>            _bForceReload;

            /** @brief 원자 플래그를 끈 기본값으로 만듭니다. */
            ModuleContext() noexcept;
            /** @brief 핸들과 경로를 옮겨 받습니다. */
            ModuleContext( ModuleContext&& other ) noexcept;
            /** @brief 이동 대입입니다. */
            ModuleContext& operator=( ModuleContext&& other ) noexcept;
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

        unordered_map<string, ModuleContext> _mapModule;
        vector<string>                       _listSharedModule; ///< `loadSharedModule` 로 올린 공용 모듈 이름(내리지 않는다)
        unique_ptr<IFileWatcher>             _fileWatcher;
        mutable mutex                        _buildMutex;
        BuildNotice                          _buildNotice;
        OnBeforeCommitBatchDelegate          _onBeforeCommitBatch;
        DrainWorkersDelegate                 _drainWorkers;
        vector<DeferredUnloadImage>          _listDeferredUnloadImage; ///< 언로드를 미룬 옛 이미지. 오래된 것부터
        uint32                               _reloadBatchID;           ///< 연쇄 리로드마다 오르는 배치 번호
        uint8                                _bReloadGraphBroken : 1;
        [[maybe_unused]] uint8               _reserved           : 7;
    };
} // namespace sw
