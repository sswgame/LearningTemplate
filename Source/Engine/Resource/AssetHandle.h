/**
 * @file AssetHandle.h
 * @brief 로드 상태(`Loading` · `Ready` · `Failed`)를 드러내는 에셋 핸들과, 핸들 여럿을 모아 준비를 묻는 준비 집합(`AssetReadySet`)입니다.
 * @details 언리얼 `FStreamableHandle` · 유니티 Addressables `AsyncOperationHandle` 과 같은 모양입니다. 종류별 캐시(`MeshCache` · `TextureCache` · `AudioClipCache`)가
 *          `request( path )` 로 핸들을 내고, 동기 API(`acquire` · `loadPrefab` · `getOrCompile`)는 같은 진행 중 표에서 블록을 받아 `wait()` 합니다 — 비동기와 동기가
 *          **같은 로드 블록**을 탑니다. 다른 점은 새 로드를 어디서 시작하는가(`AssetLoadStart`) 하나입니다.
 *
 *          한 로드는 두 단계입니다. **로드**(디스크 읽기 · 디코드, 어느 스레드든)와, 필요한 종류만 **완료**(GPU 업로드처럼 정해진 스레드에서만 되는 일)입니다.
 *          각 단계는 먼저 집은(`claim`) 스레드 하나가 실행합니다. 워커 태스크가 아직 집지 않은 로드를 기다리는 쪽(`wait`)이 집으면 그 자리에서 실행하므로,
 *          워커가 모두 바쁜 때 워커 안에서 `wait` 해도 자기 일을 기다리며 멈추지 않습니다(태스크 풀 교착 없음).
 *
 *          요청 없는 핸들은 상태가 없습니다(`isValid() == false`). 상태 열거형에 "요청한 적 없음(Missing)" 을 두지 않는 이유는, 그것이 "핸들 없음" 을
 *          상태로 흉내 낸 것이라 `Missing` 과 `Failed` 를 한데 묶어 처리하는 호출처를 낳기 때문입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @brief 에셋 로드 하나의 상태입니다. 요청한 핸들만 상태를 가집니다(`AssetHandle::isValid`). */
    enum class AssetLoadState : uint8
    {
        Loading = 0, ///< 읽거나 디코드하거나 완료(GPU 업로드)를 기다리는 중이다
        Ready,       ///< 쓸 수 있다
        Failed,      ///< 읽기 · 디코드 · 완료가 실패했다. 이유는 `getError` 에 있다
    };

    /**
     * @brief 완료 단계 한 걸음의 결과입니다(`AssetLoadBlock::beginCompletion` · `pollCompletion`).
     * @details GPU 업로드처럼 제출과 끝남 확인이 갈리는 종류만 `Submitted` · `Busy` 를 쓴다. 한 번에 끝나는 종류는 `Done` · `Failed` 만 돌려준다.
     */
    enum class AssetCompletionStep : uint8
    {
        Done = 0,  ///< 끝났다. 블록이 `Ready` 가 된다
        Failed,    ///< 실패했다. 이유를 @p outError 에 적었다
        Submitted, ///< 일을 냈고 끝났는지는 `pollCompletion` 이 본다(업로드 중 단계 — 블록은 `Loading` 으로 남는다)
        Busy,      ///< 지금은 낼 수 없다. 완료를 다시 기다리는 단계로 돌아가 다음 펌프에서 다시 낸다
    };

    /**
     * @brief 새 로드의 로드 단계를 어디서 시작하는가입니다(`AssetLoadBlock::startLoad`).
     * @details 동기 API(`acquire` · `loadPrefab`)는 `OnCallingThread` 로 블록을 만들어 그 자리에서 읽는다 — 워커에 맡겼다가 곧바로 기다리면 태스크가 하나
     *          헛돌고, 읽기의 로그(오류)가 호출한 스레드가 아니라 워커에서 나와 테스트의 방어 범위(`SW_TEST_DEFENSIVE_SCOPE`) 밖으로 샌다.
     *          블록과 진행 중 표는 같으므로 그 사이 같은 경로를 비동기로 요청한 쪽은 같은 로드를 받는다.
     */
    enum class AssetLoadStart : uint8
    {
        Async,          ///< 낮은 우선순위 워커 태스크로 시작한다(엔진 태스크가 없거나 쿠킹 중이면 이 자리에서)
        OnCallingThread ///< 이 자리에서 읽는다(동기 API)
    };
} // namespace sw

namespace sw
{
    /**
     * @class AssetLoadBlock
     * @brief 로드 하나의 공유 상태입니다. 핸들 · 캐시 · 워커 태스크가 `shared_ptr` 로 나눠 가집니다. 종류별 캐시가 이것을 상속해 단계 본문을 채웁니다.
     * @details 단계 상태는 원자 워드 하나(`_phase`)이고, 기다리는 쪽은 그 워드에서 잠듭니다(`Futex`). 상태와 오류 문자열은 단계를 `Done` 으로 바꾸기 **전에** 쓰므로
     *          `getState()` 가 `Ready` · `Failed` 를 본 스레드는 결과와 오류를 읽을 수 있습니다(release · acquire).
     */
    class SW_API AssetLoadBlock
    {
    public:
        /** @brief @p pKindName 은 로그에 쓰는 종류 이름(정적 문자열), @p path 는 요청 경로입니다. */
        AssetLoadBlock( const utf8* pKindName, string_view path );
        virtual ~AssetLoadBlock();

        AssetLoadBlock( const AssetLoadBlock& )            = delete;
        AssetLoadBlock& operator=( const AssetLoadBlock& ) = delete;

        /** @brief 지금 상태입니다. */
        AssetLoadState getState() const;
        /** @brief 요청 경로입니다. */
        const string& getPath() const { return _path; }
        /** @brief 종류 이름입니다(`"Mesh"`, `"Texture"` …). */
        const utf8* getKindName() const { return _pKindName; }
        /** @brief 실패 이유입니다. `Failed` 가 아니면 빈 문자열입니다. */
        const string& getError() const;

        /**
         * @brief `Ready` 나 `Failed` 가 될 때까지 기다립니다. 아직 아무도 집지 않은 단계는 이 스레드가 집어 실행합니다.
         * @warning 완료 단계가 있는 종류(텍스처 업로드)는 그 단계를 이 스레드에서 실행하므로, 그 종류의 정해진 스레드(디바이스를 쓰는 스레드)에서만 부릅니다.
         *          다른 스레드는 `getState()` 를 프레임마다 묻습니다(`AssetReadySet`).
         */
        void wait();

        /** @brief 로드 단계를 아직 아무도 집지 않았으면 이 스레드가 실행합니다(워커 태스크 본문). */
        void runLoadIfUnclaimed();
        /** @brief 로드가 끝나 완료를 기다리면 이 스레드가 완료 단계를 실행합니다(정해진 스레드의 펌프). 실행했으면 true 입니다. */
        bool runCompletionIfPending();
        /**
         * @brief 완료 단계를 내 둔(업로드 중) 로드의 끝남 확인을 아무도 집지 않았으면 이 스레드가 한 걸음 실행합니다(펌프, 막지 않는다). 실행했으면 true 입니다.
         * @details 끝나지 않았으면 단계는 업로드 중으로 남고 `onUploadSubmitted` 가 다시 불립니다. 다른 스레드의 `wait()` 가 같은 블록의 완료를 집지 않도록 **집은 스레드 하나만** 실행합니다.
         */
        bool runUploadPollIfPending();

        /**
         * @brief 로드 단계를 시작합니다. `Async` 는 엔진 태스크가 있으면 낮은 우선순위 워커 태스크로, 없으면(테스트 · 도구) 이 자리에서 실행합니다.
         *        `OnCallingThread` 는 늘 이 자리에서 실행합니다. 쿠킹 중에도 `Async` 를 이 자리에서 실행합니다(`canLoadOnWorker`).
         * @details 태스크는 이 블록을 `shared_ptr` 로 쥐므로 핸들이 먼저 사라져도 해제된 블록에 쓰지 않습니다.
         */
        static void startLoad( const shared_ptr<AssetLoadBlock>& pBlock, AssetLoadStart start );
        /**
         * @brief 에셋 읽기를 워커 태스크로 넘길 수 있으면 true 입니다. 엔진 태스크가 없거나(테스트 · 도구) 쿠킹 중(`CookJob::requiresCallingThreadRead`)이면 false 입니다.
         * @details 쿠킹 작업 범위는 스레드 지역 값이라 워커의 읽기는 작업의 입력에서 빠진다. 읽기를 워커로 보낼지 정하는 곳(`startLoad`, 오디오 디코드,
         *          스트리밍 큐)은 모두 이 값 하나로 정한다 — 엔진 서비스만 보고 정하면 쿠킹 중에 씬을 세울 때 그 읽기가 실패한다.
         */
        static bool canLoadOnWorker();

    protected:
        /**
         * @brief 로드 단계 본문입니다(디스크 읽기 · 디코드). 어느 스레드에서든 불립니다.
         * @return 성공하면 true. 실패하면 @p outError 에 이유를 적고 false 를 반환합니다.
         */
        [[nodiscard]] virtual bool runLoad( string& outError ) = 0;
        /** @brief 완료 단계가 있는 종류면 true 입니다. 기본은 없음입니다. */
        virtual bool hasCompletion() const { return false; }
        /** @brief 완료 단계 본문입니다(정해진 스레드). 완료가 있는 종류만 덮어씁니다. */
        [[nodiscard]] virtual bool runCompletion( string& outError );
        /**
         * @brief 완료 단계의 첫 걸음입니다. 기본은 `runCompletion` 을 한 번에 실행해 `Done` · `Failed` 로 끝냅니다. 제출과 확인이 갈리는 종류가 덮어씁니다.
         * @param bBlocking `wait()` 가 부른 경우 true 입니다 — 끝날 때까지 기다리고 `Done` · `Failed` 만 돌려줍니다(`Submitted` · `Busy` 를 돌려주지 않는다). 펌프는 false 입니다.
         */
        [[nodiscard]] virtual AssetCompletionStep beginCompletion( bool bBlocking, string& outError );
        /** @brief `Submitted` 뒤의 끝남 확인입니다(업로드 중 단계에서 집은 스레드가 부른다). `bBlocking` 이면 끝날 때까지 기다립니다. 기본은 늘 `Done` 입니다. */
        [[nodiscard]] virtual AssetCompletionStep pollCompletion( bool bBlocking, string& outError );
        /** @brief 완료 단계가 `Submitted` 로 끝나 업로드 중이 된 순간 불립니다(펌프 목록에 넣을 때). 잠금 안에서 `isUploading()` 을 확인하고 넣습니다. */
        virtual void onUploadSubmitted() {}
        /** @brief 업로드 중(완료를 냈고 끝남 확인을 아무도 집지 않은) 단계면 true 입니다. */
        bool isUploading() const { return _phase.load( std::memory_order_acquire ) == kPhaseUploading; }
        /**
         * @brief 로드 단계가 성공해 완료를 기다리게 된 순간 불립니다(로드한 스레드). 완료 단계를 펌프에 맡기는 종류가 덮어씁니다.
         * @warning 단계를 `AwaitingCompletion` 으로 알린 **뒤에** 불리므로, 기다리던 다른 스레드가 이미 완료를 집었을 수 있습니다. 펌프 목록에 넣는 쪽은
         *          완료 본문이 목록에서 빼는 잠금 안에서 `isAwaitingCompletion()` 을 보고 넣어야 합니다 — 아니면 끝난 블록이 목록에 남아 참조를 쥡니다.
         */
        virtual void onAwaitingCompletion() {}
        /** @brief 로드가 끝나 완료를 아무도 집지 않은 단계면 true 입니다(`onAwaitingCompletion` 이 펌프 목록에 넣을지 볼 때). */
        bool isAwaitingCompletion() const { return _phase.load( std::memory_order_acquire ) == kPhaseAwaitingCompletion; }

        /** @brief 이미 준비된 값으로 만든 블록을 `Ready` 로 표시합니다(캐시 적중). 단계를 실행하지 않습니다. */
        void markReadyWithoutLoad();
        /** @brief 요청 자체가 틀린 블록을 `Failed` 로 표시합니다(빈 경로 등). 단계를 실행하지 않습니다. */
        void markFailedWithoutLoad( string error );

    private:
        /** @brief 단계 워드의 값입니다. */
        enum Phase : uint32
        {
            kPhaseQueued = 0,         ///< 로드를 아무도 집지 않았다
            kPhaseLoading,            ///< 한 스레드가 로드 중이다
            kPhaseAwaitingCompletion, ///< 로드는 끝났고 완료를 아무도 집지 않았다
            kPhaseCompleting,         ///< 한 스레드가 완료 중이다
            kPhaseDone,               ///< `_state` 가 `Ready` 나 `Failed` 다
            kPhaseUploading,          ///< 완료 단계가 일을 냈고(`Submitted`) 끝남 확인을 아무도 집지 않았다 — `wait()` 와 펌프가 집어 확인한다(집은 쪽은 `kPhaseCompleting`)
        };

        /** @brief 집은 로드 단계를 실행하고 다음 단계로 넘깁니다. */
        void executeLoad();
        /** @brief 집은 완료 단계를 실행하고 결과에 따라 끝내거나 업로드 중 · 완료 대기 단계로 넘깁니다. */
        void executeCompletion( bool bBlocking );
        /** @brief 집은 업로드 중 단계의 끝남 확인을 한 걸음 실행하고 결과를 반영합니다. */
        void executeUploadPoll( bool bBlocking );
        /** @brief 완료 단계 한 걸음의 결과를 단계 워드에 반영합니다. */
        void settleCompletionStep( AssetCompletionStep step, string error );
        /** @brief 결과를 적고 `Done` 으로 넘긴 뒤 기다리는 쪽을 깨웁니다. */
        void finish( bool bSucceeded, string error );

    private:
        string                 _path;
        string                 _error;
        const utf8*            _pKindName;
        atomic<uint32>         _phase;
        atomic<AssetLoadState> _state;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AssetHandle
     * @brief 에셋 로드 하나를 가리키는 핸들입니다. 복사하면 같은 로드를 나눠 가집니다. 핸들이 살아 있는 동안 준비된 에셋도 살아 있습니다.
     * @tparam TBlock 종류별 로드 블록입니다. `getAsset()` 이 에셋을 `shared_ptr` 로 돌려줘야 합니다.
     * @details 기본 생성한 핸들은 요청이 없습니다(`isValid() == false`). 상태를 묻기 전에 요청을 가졌는지 확인합니다 — 요청 없는 핸들의 상태 질문은 assert 입니다.
     */
    template <typename TBlock>
    class AssetHandle
    {
    public:
        using AssetType = typename TBlock::AssetType;

        AssetHandle() = default;
        explicit AssetHandle( shared_ptr<TBlock> pBlock )
            : _pBlock{ std::move( pBlock ) }
        {
        }

        /** @brief 요청을 가졌으면 true 입니다. */
        bool isValid() const { return _pBlock != nullptr; }
        /** @brief 상태입니다. 요청이 있어야 합니다. */
        AssetLoadState getState() const
        {
            if ( _pBlock == nullptr )
                ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
            return _pBlock->getState();
        }
        /** @brief 쓸 수 있으면 true 입니다(요청이 없으면 false). */
        bool isReady() const { return _pBlock != nullptr && _pBlock->getState() == AssetLoadState::Ready; }
        /** @brief 아직 읽는 중이면 true 입니다(요청이 없으면 false). */
        bool isLoading() const { return _pBlock != nullptr && _pBlock->getState() == AssetLoadState::Loading; }
        /** @brief 실패했으면 true 입니다(요청이 없으면 false). */
        bool hasFailed() const { return _pBlock != nullptr && _pBlock->getState() == AssetLoadState::Failed; }

        /** @brief `Ready` 일 때만 에셋을, 아니면 nullptr 를 돌려줍니다. 기다리지 않습니다. */
        [[nodiscard]] shared_ptr<AssetType> get() const { return isReady() ? _pBlock->getAsset() : shared_ptr<AssetType>{}; }
        /** @brief 실패 이유입니다. 요청이 있어야 합니다. */
        const string& getError() const
        {
            if ( _pBlock == nullptr )
                ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
            return _pBlock->getError();
        }
        /** @brief 요청 경로입니다. 요청이 있어야 합니다. */
        const string& getPath() const
        {
            if ( _pBlock == nullptr )
                ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
            return _pBlock->getPath();
        }

        /**
         * @brief `Ready` 나 `Failed` 가 될 때까지 기다리고 에셋을 돌려줍니다(실패하면 nullptr). 요청이 있어야 합니다.
         * @details 동기 API 는 이것 하나로 비동기와 같은 길을 탑니다. 완료 단계가 있는 종류의 제약은 `AssetLoadBlock::wait` 를 보십시오.
         */
        [[nodiscard]] shared_ptr<AssetType> wait() const
        {
            if ( _pBlock == nullptr )
                ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
            _pBlock->wait();
            return get();
        }

        /** @brief 준비 집합이 상태를 묻는 블록입니다. */
        const shared_ptr<TBlock>& getBlock() const { return _pBlock; }

        /** @brief 요청을 놓습니다. */
        void reset() { _pBlock.reset(); }

    private:
        shared_ptr<TBlock> _pBlock{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class AssetReadySet
     * @brief 게임이 시작 전에 끝낼 에셋들의 준비 집합입니다. 로딩 화면은 이 집합만 묻습니다(언리얼 `FStreamableManager::RequestAsyncLoad` 의 묶음 핸들).
     * @details 집합은 핸들을 쥐므로, 캐시가 약한 참조인 종류(메시)도 집합이 살아 있는 동안 남습니다. 막고 기다리는 `wait` 는 두지 않습니다 —
     *          완료 단계가 있는 종류(텍스처)는 디바이스 스레드에서만 끝나므로, 게임 스레드가 막고 기다리면 그 단계가 영영 오지 않을 수 있습니다.
     *          로딩 화면은 프레임마다 `isReady()` 를 묻습니다.
     */
    class SW_API AssetReadySet
    {
    public:
        AssetReadySet() = default;

        /** @brief 핸들을 집합에 넣습니다. 요청 없는 핸들은 넣을 수 없습니다(assert). */
        template <typename TBlock>
        void add( const AssetHandle<TBlock>& handle )
        {
            SW_ASSERT( handle.isValid() );
            _listBlock.push_back( handle.getBlock() );
        }

        /** @brief 모두 `Ready` 나 `Failed` 이면 true 입니다(빈 집합도 true). */
        [[nodiscard]] bool isComplete() const;
        /** @brief 모두 `Ready` 이면 true 입니다(빈 집합도 true). */
        [[nodiscard]] bool isReady() const;
        /** @brief 하나라도 `Failed` 이면 true 입니다. */
        [[nodiscard]] bool hasFailed() const;
        /** @brief 아직 `Loading` 인 수입니다(로딩 화면의 진행). */
        [[nodiscard]] size_t countLoading() const;
        /** @brief `Ready` 인 수입니다. */
        [[nodiscard]] size_t countReady() const;
        /** @brief 넣은 핸들 수입니다. */
        size_t getCount() const { return _listBlock.size(); }
        /** @brief 실패한 로드의 "종류 경로: 이유" 를 @p outListError 에 덧붙입니다. */
        void appendFailures( vector<string>& outListError ) const;
        /** @brief 집합을 비웁니다. 쥐고 있던 에셋을 놓습니다(진행 중인 로드는 그대로 끝나고 버려집니다). */
        void clear() { _listBlock.clear(); }

    private:
        vector<shared_ptr<AssetLoadBlock>> _listBlock;
    };
} // namespace sw
