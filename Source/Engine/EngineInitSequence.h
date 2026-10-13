/**
 * @file EngineInitSequence.h
 * @brief 엔진 기동 단계(표 `EngineInitStepList.xxx` 의 줄과 `ISubsystem`)를 의존으로 위상 정렬해 초기화하고, 그 역순으로 종료 · 해제합니다.
 * @details 언리얼 `FSubsystemCollectionBase::InitializeDependency` 와 같은 자리입니다 — 각 단계가 먼저 서야 하는 단계를 적고, 초기화는 그 그래프의
 *          위상 순서(동점은 이름 순), 종료(`Deinitialize`)와 객체 해제는 역순입니다. 순서 계산 · 실행은 `SubsystemCollection` 하나가 합니다.
 *          표의 줄은 그 목록에 줄마다 하나씩 든 어댑터이고(본문은 호스트의 `initialize<단계>` · `shutdown<단계>` · `destroy<단계>` 정적 함수,
 *          `EngineInitNoStepBody`), `ISubsystem` 으로 옮긴 단계는 `addSubsystem` 으로 같은 목록에 들어가 표의 줄과 한 순서로 섞여 돕니다.
 *          표는 옮기는 동안의 자리이고, 옮긴 단계는 표에서 지웁니다(`docs/plans/SubsystemMigration.md`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/MemoryTag.h"

#include "Engine/Common/FunctionSubsystem.h"
#include "Engine/Common/ISubsystem.h"
#include "Engine/Common/SubsystemCollection.h"

namespace sw
{
    /**
     * @brief 자기 클래스가 없는 엔진 기동 단계의 선언(이름 · 의존 · 대상 · 메모리 태그)입니다. 본문은 호스트가 `FunctionSubsystem` 의 델리게이트로 줍니다.
     * @details 호스트가 엮는 단계(모듈 훅 · 타입 공급자 로더 · 씬의 디바이스)와 Core 서비스(태스크 · 비동기 IO · 코덱 레지스트리 — Core 는 `ISubsystem` 을
     *          모른다)를 세우는 단계입니다. 본문은 호스트마다 다를 수 있지만(엔진 루프 · 테스트 하네스) 선언은 여기 하나라 두 호스트가 같은 순서로 섭니다.
     *          클래스가 있는 단계(`ShaderCache` · `PhysicsSystem` …)는 그 클래스가 스스로 선언합니다.
     */
    struct EngineFunctionStep
    {
        /** @brief 코덱 레지스트리 + Core 슬롯 + (엔진 루프만) 외부 코덱. 맨 먼저 서므로 맨 나중에 소멸한다 — 팩을 내리는 Resource 해제까지 코덱이 살아 있다. */
        static constexpr FunctionSubsystemDesc kCompression{ "Compression", "", EngineInitTarget::All, MemoryTag::EngineMisc };
        /** @brief 이 프로세스에 올라온 타입 등록기를 모으고 명령줄의 enum 이름을 적용한다. 설정 · 에셋 · 씬 객체가 이 뒤에 서서 먼저 소멸한다. */
        static constexpr FunctionSubsystemDesc kReflection{ "Reflection", "", EngineInitTarget::All, MemoryTag::Reflection };
        /** @brief 엔진 · 게임 설정(EngineConfig · GameConfig)과 설정 표. 설정 역직렬화가 TypeInfo 를 쓴다. */
        static constexpr FunctionSubsystemDesc kConfig{ "Config", "Reflection", EngineInitTarget::All, MemoryTag::EngineMisc };
        /**
         * @brief 에셋 매니저 + 콘텐츠 마운트. 팩 루트는 GameConfig, 팩 스트림은 코덱이다.
         * @details 해제(`AssetManager::shutdown`)가 에셋 캐시를 비운다 — 에셋을 드는 단계가 모두 이 뒤에 서므로, 역순 해제에서 그들의 소멸자가 에셋을 놓은 뒤다.
         */
        static constexpr FunctionSubsystemDesc kResource{ "Resource", "Compression Config", EngineInitTarget::All, MemoryTag::Asset };
        /** @brief 엔진 데이터(EngineDefaultAssets)와 엔진 문화권 표 · 문자열. 하네스는 읽지 않고 소유만 한다. */
        static constexpr FunctionSubsystemDesc kEngineDefaultAssets{ "EngineDefaultAssets", "Reflection Resource", EngineInitTarget::All, MemoryTag::EngineMisc };
        /** @brief 오디오 서비스. 음성 스트리밍이 태스크를 쓰고, 모듈 콜백을 든다 — 서버는 장치 없는 NullAudioBackend. */
        static constexpr FunctionSubsystemDesc kAudio{ "Audio", "ModuleImages Task", EngineInitTarget::All, MemoryTag::Audio };
        /**
         * @brief 씬 매니저 · 에셋 스트리밍 큐 · 되돌리기 기록. 컴포넌트가 입력 · 오디오 · 모듈 코드 · 에셋 · 물리 씬을 든다(스트리밍은 비동기 IO).
         * @details 스트리밍 큐의 소멸자는 태스크를 기다린다 — Task 에 의존하는 이 단계가 Task 보다 먼저 놓는다.
         */
        static constexpr FunctionSubsystemDesc kScene{ "Scene", "Audio FileIO Input ModuleImages Physics Resource Task", EngineInitTarget::All, MemoryTag::Scene };
        /** @brief 태스크 매니저. 소멸자가 태스크를 기다리는 객체(에셋 스트리밍 큐)는 이것에 의존하는 단계(Scene)가 먼저 놓는다. */
        static constexpr FunctionSubsystemDesc kTask{ "Task", "", EngineInitTarget::All, MemoryTag::Task };
        /** @brief 종료 쪽 자리 — 씬은 사라지고 서비스는 살아 있는 구간에서 모듈 DLL 을 내리는 호스트 훅. 타입 · 코덱 · 캐시를 떼고 태스크를 기다린다. */
        static constexpr FunctionSubsystemDesc kModuleImages{ "ModuleImages", "Compression Reflection Resource Task", EngineInitTarget::All, MemoryTag::EngineMisc };
        /** @brief 비동기 파일 IO. 완료 콜백을 태스크로 싣고, 모듈을 내리기 전에 걸린 읽기를 다 끝낸다. */
        static constexpr FunctionSubsystemDesc kFileIO{ "FileIO", "ModuleImages Task", EngineInitTarget::All, MemoryTag::Asset };
        /**
         * @brief 입력 매니저. 모듈이 등록한 장치 · 콜백을 든다 — 서버는 이벤트가 오지 않는 장치 목록이다.
         * @note `InputManager` 는 정렬이 큰 타입(락프리 큐)이라 `ISubsystem*` 으로 지울 수 없어 이 선언으로 세운다(`FunctionSubsystem::takeOwnership` 은 제 타입으로 지운다).
         */
        static constexpr FunctionSubsystemDesc kInput{ "Input", "ModuleImages", EngineInitTarget::All, MemoryTag::EngineMisc };
        /** @brief 호스트가 타입 공급자(GF · 킷 · 게임 모듈)를 올려 등록을 끝낸다 — 씬을 읽는 일은 이 뒤다. */
        static constexpr FunctionSubsystemDesc kModuleTypes{ "ModuleTypes", "Config ModuleImages Reflection", EngineInitTarget::All, MemoryTag::Reflection };
        /**
         * @brief 헤드리스 작업 — 셰이더 쿠킹 · 씬 쿠킹(모든 컴포넌트 타입이 있어야 쿠킹한다) · 임포트 준비. 하면 뒤 단계(창 · RHI · 렌더러)를 건너뛴다.
         *        서버는 씬 쿠킹만 하고 셰이더 쿠킹 · 임포트는 거절한다.
         */
        static constexpr FunctionSubsystemDesc kHeadless{ "Headless", "EngineDefaultAssets ModuleTypes Scene ShaderCache", EngineInitTarget::All, MemoryTag::EngineMisc };
        /** @brief 플레이어 옵션 — 스키마 · 사용자 파일을 읽어 대상(오디오 · 입력 · 전역 변수)에 넣는다. 화면 값은 창을 만드는 RHI 가 읽는다. */
        static constexpr FunctionSubsystemDesc kUserSettings{ "UserSettings", "Audio Headless Input Resource", EngineInitTarget::Client, MemoryTag::EngineMisc };
        /** @brief 렌더러와 GT 쪽 패킷 빌더(씬 스냅샷 · 업로드 큐). 백엔드 교체로 다시 선다. */
        static constexpr FunctionSubsystemDesc kFrameRenderer{ "FrameRenderer", "RHI Task", EngineInitTarget::Client, MemoryTag::RenderCPU };
        /** @brief 렌더 스레드. 다시 설 때 같은 객체에 붙는다(훅 · 포인터 유지). */
        static constexpr FunctionSubsystemDesc kRenderThread{ "RenderThread", "FrameRenderer", EngineInitTarget::Client, MemoryTag::RenderCPU };
        /** @brief 셰이더 라이브 리로드(Debug). 재컴파일이 렌더 스레드를 세우고 렌더러에 알린다. */
        static constexpr FunctionSubsystemDesc kLiveShader{ "LiveShader", "FrameRenderer RenderThread", EngineInitTarget::Client, MemoryTag::Shader };
        /** @brief 씬에 디바이스를 건다 — 렌더러 쪽 단계 가운데 종료가 가장 먼저다. */
        static constexpr FunctionSubsystemDesc kSceneRHI{ "SceneRHI", "RHI Scene", EngineInitTarget::Client, MemoryTag::RenderCPU };

        /** @brief 위 선언 모두입니다(테스트가 엔진 목록을 본문 없이 짓는다). */
        static constexpr const FunctionSubsystemDesc* kArrAll[] = { &kCompression, &kReflection, &kConfig, &kResource, &kEngineDefaultAssets, &kAudio, &kScene, &kTask, &kModuleImages, &kFileIO, &kInput, &kModuleTypes,
                                                                    &kHeadless, &kUserSettings, &kFrameRenderer, &kRenderThread, &kLiveShader, &kSceneRHI };
    };
} // namespace sw

namespace sw
{
    /** @brief 표의 기동 단계입니다. 값은 표(`EngineInitStepList.xxx`)의 줄 자리일 뿐이고 순서가 아닙니다(순서는 `getInitializeOrder`). */
    enum class EngineInitStep : uint8
    {
#define SW_ENGINE_STARTUP_STEP( Name, ... ) Name,
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        Count
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 단계 본문이 없는 자리입니다. 호스트의 본문 모음이 이것을 상속하고, 필요한 함수만 같은 이름으로 정의합니다(이름 가림).
     * @details 표의 줄마다 `initialize<단계>` · `shutdown<단계>` · `destroy<단계>` 세 이름을 null 로 둡니다. 본문 모음이
     *          `static EngineInitResult initializeScene( THost& )` 를 정의하면 그것이 걸리고, 정의하지 않은 자리는 아무것도 하지 않습니다
     *          (초기화는 성공으로 칩니다). 세 본문의 뜻은 `ISubsystem` 의 초기화 · 종료 · 소멸자와 같습니다.
     *          - `initialize<단계>( THost& )` → `EngineInitResult`: 단계를 세운다. 위상 순서로 불린다.
     *          - `shutdown<단계>( THost& )`: 세운 단계를 내린다. **초기화한 단계만** 역순으로 불리고, 이때 모든 단계의 객체는 아직 살아 있다.
     *          - `destroy<단계>( THost& )`: 단계가 소유한 객체를 해제한다. 모든 종료 뒤에 **모든 단계**에 역순으로 불린다(기동이 어디서 멈췄든).
     *            단계의 객체는 부트스트랩이 미리 만들었거나 실패한 초기화가 반쯤 만들었을 수 있어서, 본문은 null 안전해야 한다.
     * @note 이름이 틀린 본문(`initializeScnee`)은 어느 자리에도 걸리지 않습니다. 시그니처가 틀린 본문은 컴파일 오류입니다.
     */
    struct EngineInitNoStepBody
    {
#define SW_ENGINE_STARTUP_STEP( Name, ... )                          \
    static constexpr decltype( nullptr ) initialize##Name = nullptr; \
    static constexpr decltype( nullptr ) shutdown##Name   = nullptr; \
    static constexpr decltype( nullptr ) destroy##Name    = nullptr;
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
    };
} // namespace sw

namespace sw
{
    /** @brief 단계 하나의 본문 셋입니다. 호스트 타입을 지운 포인터이고, null 이면 그 본문이 없습니다(`EngineInitStepTable` 이 채웁니다). */
    struct EngineInitStepEntry
    {
        using InitializeFunction = EngineInitResult ( * )( void* pHost );
        using StepFunction       = void ( * )( void* pHost );

        InitializeFunction _pInitialize; ///< `initialize<단계>`
        StepFunction       _pShutdown;   ///< `shutdown<단계>`
        StepFunction       _pDestroy;    ///< `destroy<단계>`
    };
} // namespace sw

namespace sw
{
    /** @brief 본문 모음의 정적 함수(또는 `EngineInitNoStepBody` 의 null)를 `EngineInitStepEntry` 의 칸으로 바꿉니다. */
    template <class THost>
    struct EngineInitStepBinder
    {
        template <auto pInitialize>
        static EngineInitResult initialize( void* pHost )
        {
            return pInitialize( *static_cast<THost*>( pHost ) );
        }
        template <auto pFunction>
        static void call( void* pHost )
        {
            pFunction( *static_cast<THost*>( pHost ) );
        }

        template <auto pInitialize>
        static constexpr EngineInitStepEntry::InitializeFunction bindInitialize()
        {
            if constexpr ( std::is_null_pointer_v<decltype( pInitialize )> )
                return nullptr;
            else
                return &initialize<pInitialize>;
        }
        template <auto pFunction>
        static constexpr EngineInitStepEntry::StepFunction bindCall()
        {
            if constexpr ( std::is_null_pointer_v<decltype( pFunction )> )
                return nullptr;
            else
                return &call<pFunction>;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 호스트의 본문 모음(`TBody::initialize<단계>` …)으로 만든 본문 표입니다. 자리는 `EngineInitStep` 값입니다.
     * @details 단계마다 타입을 두지 않습니다 — 표 한 줄에 본문 이름 셋이 따라오고, 본문 모음이 그중 필요한 것만 정의합니다.
     *          `EngineInitNoStepBody` 를 상속하지 않은 본문 모음은 모든 자리를 정의해야 합니다(빠지면 컴파일 오류).
     */
    template <class THost, class TBody>
    struct EngineInitStepTable
    {
        using Binder = EngineInitStepBinder<THost>;

        static constexpr EngineInitStepEntry kArrEntry[] = {
#define SW_ENGINE_STARTUP_STEP( Name, ... ) \
    { Binder::template bindInitialize<TBody::initialize##Name>(), Binder::template bindCall<TBody::shutdown##Name>(), Binder::template bindCall<TBody::destroy##Name>() },
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        };
        static_assert( sizeof( kArrEntry ) / sizeof( kArrEntry[0] ) == static_cast<size_t>( EngineInitStep::Count ),
                       "Startup step body table must have one row per EngineInitStep" );
    };
} // namespace sw

namespace sw
{
    /**
     * @class EngineInitSequence
     * @brief 기동 단계 표의 줄과 `ISubsystem` 을 한 목록(`SubsystemCollection`)에 들고, 호스트의 본문 모음으로 초기화 · 종료 · 해제를 돌립니다.
     * @details 목록의 자리 `0 … EngineInitStep::Count-1` 은 표의 줄(어댑터)이고, 그 뒤가 `addSubsystem` 으로 넣은 서브시스템입니다.
     */
    class SW_API EngineInitSequence
    {
    public:
        /** @brief 표의 줄마다 어댑터를 목록에 넣고 정렬합니다. 모르는 이름 · 순환이면 `initializeAll` 이 오류를 알리고 실패합니다. */
        EngineInitSequence();
        /** @brief 남은 서브시스템을 소멸합니다. 표의 해제 본문(`destroy<단계>`)은 `destroyAll` 로만 불립니다. */
        ~EngineInitSequence();

        EngineInitSequence( const EngineInitSequence& )            = delete;
        EngineInitSequence& operator=( const EngineInitSequence& ) = delete;

        /**
         * @brief `ISubsystem` 단계를 넘겨받아 표의 줄과 한 순서로 정렬합니다. `initializeAll` **전에** 넣습니다. 넘긴 객체를 돌려줍니다(소유는 이 순서).
         * @details 의존 칸은 표의 단계 이름도, 다른 서브시스템 이름도 부를 수 있습니다(표의 줄도 서브시스템 이름을 부른다). 모르는 이름 ·
         *          순환 · 대상 어긋남은 `initializeAll` 이 정렬 오류로 알리고 실패합니다.
         */
        template <class TSubsystem>
        TSubsystem& addSubsystem( unique_ptr<TSubsystem> subsystem )
        {
            TSubsystem& added = _subsystems.add( std::move( subsystem ) );
            refreshOrder();
            return added;
        }

        /**
         * @brief 위상 순서로 초기화합니다 — 표의 줄은 `TBody::initialize<단계>`(정의하지 않은 단계는 성공으로 칩니다), 서브시스템은 `initializeSubsystem`.
         * @details `Failed` 면 거기서 멈추고 false 입니다. `SkipDependents` 면 그 단계에 의존하는 단계를 건너뜁니다.
         *          @p target 과 겹치지 않는 대상의 단계는 부르지 않고 건너뜁니다(전용 서버의 RHI · 렌더러 · 플레이어 설정).
         *          @p host 를 기억해 두었다가 `shutdownAll` · `destroyAll` 이 같은 호스트로 부릅니다. 호스트는 그때까지 살아 있어야 합니다.
         * @tparam TBody 본문 모음입니다(`EngineInitNoStepBody` 를 상속하고 필요한 `initialize<단계>` · `shutdown<단계>` · `destroy<단계>` 만 정의).
         * @param target 이 호스트의 대상. App 은 `Client`, 전용 서버는 `Server` 입니다.
         * @return 순서가 유효하고 실패한 단계가 없으면 true 입니다.
         */
        template <class TBody, class THost>
        [[nodiscard]] bool initializeAll( THost& host, EngineInitTarget target = EngineInitTarget::Client )
        {
            // 순서가 틀렸어도 호스트는 기억한다 — 부트스트랩이 이미 만든 단계 객체를 `destroyAll` 이 해제해야 한다.
            _pArrEntry = EngineInitStepTable<THost, TBody>::kArrEntry;
            _pHost     = &host;
            return _subsystems.initializeAll( target );
        }
        /** @brief 초기화한 단계를 역순으로 종료합니다. 두 번 불러도 됩니다. */
        void shutdownAll() { _subsystems.shutdownAll(); }
        /**
         * @brief 모든 단계를 초기화 순서의 역순으로 해제합니다(표의 줄은 `destroy<단계>`, 서브시스템은 소멸). 아직 종료하지 않은 단계가 있으면 먼저 내립니다.
         * @details 초기화한 단계만이 아니라 **모든 단계**입니다 — 실패한 단계, 건너뛴 단계(`SkipDependents`), 닿지 못한 단계도 해제합니다.
         *          `initializeAll` 을 부른 적이 없으면 표의 해제 본문은 부르지 않습니다.
         */
        void destroyAll();

        /**
         * @brief @p step 에 (간접으로라도) 의존하는 **초기화된** 단계를 역순으로 종료합니다. @p step 자신은 그대로 둡니다.
         * @details 기동 중에 한 단계를 갈아 끼울 때 쓴다(백엔드 교체: RHI 의 디바이스를 다시 만든다). 내린 단계는
         *          `restartStoppedSteps` 가 같은 본문으로 다시 세운다 — 교체 경로가 기동 본문을 손으로 베끼지 않는다.
         */
        void shutdownDependentsOf( EngineInitStep step );
        /**
         * @brief `shutdownDependentsOf` 가 내린 단계를 위상 순서로 다시 초기화합니다. 본문은 이미 있는 객체를 다시 쓴다.
         * @return 모두 다시 섰으면 true 입니다. 실패한 단계에서 멈추고, 그 단계와 나머지는 내린 채로 남습니다(종료 대상에서도 빠진다).
         */
        [[nodiscard]] bool restartStoppedSteps() { return _subsystems.restartStopped(); }
        /** @brief `shutdownDependentsOf` 가 내리고 아직 다시 세우지 않은 표의 단계입니다(초기화 순서). */
        vector<EngineInitStep> getStoppedSteps() const { return makeTableSteps( _subsystems.getStoppedIndices() ); }

        /** @brief 의존에서 계산한 표의 단계 초기화 순서입니다(본문을 돌리지 않아도 나옵니다). 종료 · 해제는 이 역순입니다. */
        const vector<EngineInitStep>& getInitializeOrder() const { return _listOrder; }
        /** @brief 지금 초기화돼 있는 표의 단계입니다(초기화한 순서). */
        vector<EngineInitStep> getInitializedSteps() const { return makeTableSteps( _subsystems.getInitializedIndices() ); }
        /** @brief 표의 줄과 서브시스템을 함께 든 목록입니다(이름 · 대상 · 전체 순서). `destroyAll` 뒤에는 빕니다. */
        const SubsystemCollection& getSubsystems() const { return _subsystems; }
        /** @brief 순서 오류(같은 이름 · 모르는 이름 · 순환 · 대상 어긋남)입니다. 없으면 빈 글입니다. */
        const string& getError() const { return _subsystems.getError(); }

        /** @brief 단계 이름(표의 철자)입니다. */
        static const utf8* getStepName( EngineInitStep step );
        /** @brief 단계 초기화가 할당하는 메모리의 용도(표의 둘째 칸)입니다. 초기화 · 재시작을 부르는 동안 이 태그가 걸립니다. */
        static MemoryTag getStepMemoryTag( EngineInitStep step );
        /** @brief 단계가 도는 호스트(표의 셋째 칸)입니다. */
        static EngineInitTarget getStepTarget( EngineInitStep step );
        /** @brief 표 그대로의 노드 목록입니다(줄 순서). */
        static vector<EngineInitNode> makeStepNodes();

    private:
        /** @brief 표의 줄 하나를 서브시스템으로 보이는 어댑터입니다(`EngineInitSequence.cpp`). 본문은 이 순서가 기억한 호스트의 본문 표입니다. */
        class StepSubsystem;

        /** @brief 정렬을 다시 하고 표의 단계 순서(`_listOrder`)를 다시 뽑습니다. */
        void refreshOrder();
        /** @brief 목록의 자리 중 표의 줄만 단계로 바꿉니다. */
        static vector<EngineInitStep> makeTableSteps( const vector<uint32>& listIndex );

    private:
        SubsystemCollection        _subsystems; ///< 표의 줄(자리 0 … Count-1) + 서브시스템
        vector<EngineInitStep>     _listOrder;  ///< 표의 단계 초기화 순서(서브시스템을 넣을 때마다 다시 뽑는다 — `destroyAll` 뒤에도 남는다)
        const EngineInitStepEntry* _pArrEntry;  ///< `initializeAll` 이 받은 호스트의 본문 표
        void*                      _pHost;      ///< 본문에 넘길 호스트
    };
} // namespace sw
