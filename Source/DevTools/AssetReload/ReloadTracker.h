/**
 * @file ReloadTracker.h
 * @brief 설정 · 에셋 · 셰이더 · 모듈 리로드의 결과와 걸린 시간을 한 곳에 모읍니다(읽기 전용 집계 — 리로드 주인과 오류 처리는 그대로).
 * @details 세 리로드는 감시 · 갱신 길이 다르고(설정 `ConfigReloadSubsystem`, 셰이더 `ShaderRecompiler`, 에셋 에디터 `AssetHotReload`), 실패하면 이전 값을 지킨 채
 *          로그 한 줄만 남겼다 — "이 파일은 지금 낡은 값으로 돈다" 를 어디서도 물을 수 없었다. 각 주인이 결과를 여기에 적고, 개발 명령 `reload.status` 가 보여 준다.
 *          언리얼은 Live Coding · 설정 리로드 · 셰이더 재컴파일이 각자 로그를 쓰고 모으는 뷰가 없다 — Godot 의 "Reimport" 독도 에셋만이다. 그래서 모으는 쪽을 둔다.
 *          고정 용량: 경로마다 마지막 상태 한 줄(`kMaxPathCount`, 넘으면 가장 오래된 경로를 밀어낸다). 길마다 적은 수 · 실패 수 · 마지막 리로드 시간(ms)은 밀어내지 않는 칸이다 —
 *          플레이 중 리로드 시나리오가 감시 값 `Reload.Count.<길>` · `Reload.FailedCount` · `Reload.LastMs.<길>` 로 본다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/Serialization/Serializer/ReloadResult.h"

namespace sw
{
    /** @brief 결과를 적은 리로드 길입니다. 정의(열거자 · 이름)는 `ReloadSource.h` — 열거자를 쓰는 단위만 그것을 넣는다. */
    enum class ReloadSource : uint8;
    /** @brief 리로드 길 수입니다(`ReloadSource` 의 칸 수 — 길마다 세는 배열의 크기). */
    constexpr uint32 kReloadSourceCount = 4;

    /**
     * @struct ReloadTrackerEntry
     * @brief 경로 하나의 마지막 리로드 상태입니다. 순번(`_sequence`)은 이 실행에서 적은 차례라 시계와 상관없이 앞뒤를 가른다.
     */
    struct ReloadTrackerEntry
    {
        string       _path;                     ///< 바뀐 파일(설정은 절대 경로, 에셋은 리소스 id, 셰이더는 소스 경로)
        string       _reason;                   ///< 마지막 실패의 까닭 한 줄(성공이면 빈 값)
        uint64       _lastRequestSequence{ 0 }; ///< 마지막으로 적은 차례
        uint64       _appliedSequence{ 0 };     ///< 마지막으로 성공한 차례(0 이면 이 실행에서 한 번도 성공하지 않았다 — 처음 읽은 값으로 돈다)
        uint32       _attemptCount{ 0 };        ///< 적은 횟수
        uint32       _failureCount{ 0 };        ///< 실패 횟수
        ReloadSource _source{};                 ///< 0 은 `Config` 다 — 적을 때 늘 채운다
        ReloadResult _lastResult{ ReloadResult::NotTracked };

        /** @brief 마지막 시도가 실패해 이전 값으로 돌고 있으면 true 입니다. */
        bool isStale() const { return _lastResult == ReloadResult::Failed; }
    };

    /**
     * @struct ModuleReloadPhases
     * @brief 모듈 연쇄 리로드 한 번의 단계별 시간(ms)입니다. 사람이 기다리는 시간은 빌드 끝 감지부터 화면이 돌아오기까지라 대기 구간을 함께 잰다.
     * @details 언리얼 Live Coding · Live++ 는 패치 시간을 매번 보고하고, 유니티는 도메인 리로드를 잰 뒤에야 줄였다. 어느 단계가 긴지 보이지 않으면 줄일 곳을 고를 수 없다.
     */
    struct ModuleReloadPhases
    {
        float64 _waitMs{ 0.0 };    ///< 빌드 끝 감지(이 프로세스의 빌드 끝 · 바깥 빌드 스탬프) → 연쇄 시작(디바운스 · 리로드 잡음 대기). 모르면 0
        float64 _prepareMs{ 0.0 }; ///< 섀도 복사 · 새 라이브러리 로드 · 검사(옛 라이브러리가 아직 돈다)
        float64 _suspendMs{ 0.0 }; ///< 배치 직전 콜백 — 게임 상태 맡기기와 게임 인스턴스 내리기
        float64 _commitMs{ 0.0 };  ///< 옛 라이브러리 언로드 · 교체 · 리로드 직후 콜백(인스턴스 다시 세우기 · 상태 되찾기)
    };

    /**
     * @class ReloadTracker
     * @brief 리로드 결과 집계입니다(개발 도구 서브시스템). 적기는 어느 스레드에서도 되고(잠금 하나), 읽기는 복사본을 돌려준다.
     */
    class SW_DEVTOOLS_API ReloadTracker final : public ISubsystem
    {
    public:
        static constexpr const utf8*               kSubsystemName    = "ReloadTracker";
        static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::HostModules; ///< 에디터 에셋 리로드가 결과를 적는다(`AssetHotReload`)
        /** @brief 기억하는 경로 수의 상한입니다(고정 용량 — 체크아웃 한 번에 수천 파일이 바뀌어도 이 이상 쌓지 않는다). */
        static constexpr uint32 kMaxPathCount = 256;

        ReloadTracker();
        ~ReloadTracker() override                        = default;
        ReloadTracker( const ReloadTracker& )            = delete;
        ReloadTracker& operator=( const ReloadTracker& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 리로드 하나의 결과를 적습니다. `NotTracked` 는 맡은 곳이 없다는 정상 결과라 적지 않습니다.
         * @param reason 실패의 까닭(첫 줄만 남긴다). 성공이면 비워 둔다.
         */
        void record( ReloadSource source, string_view path, ReloadResult result, string_view reason = {} );

        /** @brief 경로마다 마지막 상태의 복사본입니다(마지막 차례가 늦은 것부터). */
        [[nodiscard]] vector<ReloadTrackerEntry> copyEntries() const;
        /** @brief 지금 이전 값으로 돌고 있는(마지막 시도가 실패한) 경로 수입니다. */
        [[nodiscard]] uint32 countStale() const;
        /** @brief 그 길의 그 경로가 지금 이전 값으로 돌고 있으면 true 입니다(바이트가 같아 적지 않는 성공도 낡음을 풀어야 하는 셰이더가 묻는다). */
        [[nodiscard]] bool isStale( ReloadSource source, string_view path ) const;

        /** @brief 리로드 한 번(설정 파일 하나 · 에셋 하나 · 셰이더 묶음 하나 · 모듈 연쇄 하나)에 걸린 시간을 적습니다. 감시 값 `Reload.LastMs.<길>` 이 읽는다. */
        void recordElapsed( ReloadSource source, float64 elapsedMs );
        /** @brief 그 길에서 적은 결과 수입니다(Reloaded · Failed — 밀어낸 경로도 센다). */
        [[nodiscard]] uint32 getAttemptCount( ReloadSource source ) const;
        /** @brief 모든 길의 실패 수입니다(밀어낸 경로도 센다). */
        [[nodiscard]] uint32 getFailedCount() const;
        /** @brief 그 길의 마지막 리로드 시간(ms)입니다. 적은 적이 없으면 0 입니다. */
        [[nodiscard]] float64 getLastElapsedMs( ReloadSource source ) const;
        /** @brief 마지막 모듈 연쇄 리로드의 단계별 시간을 적습니다(`LiveReloadManager`). 감시 값 `Reload.Last<단계>Ms.Module` 이 읽는다. */
        void recordModulePhases( const ModuleReloadPhases& phases );
        /** @brief 마지막 모듈 연쇄 리로드의 단계별 시간입니다. 적은 적이 없으면 모두 0 입니다. */
        [[nodiscard]] ModuleReloadPhases getLastModulePhases() const;

        /**
         * @brief 사람이 읽는 표를 씁니다(`reload.status`). @p bStaleOnly 면 낡은 경로만입니다.
         * @details 한 줄: `<source> <OK|STALE> <path> attempts=N failures=M applied=#seq [reason]`. 맨 위에 경로 수와 낡은 수를 적는다.
         */
        [[nodiscard]] string formatReport( bool bStaleOnly ) const;

        /** @brief 리로드 길 이름(`Config` · `Asset` · `Shader` · `Module`)입니다. */
        static const utf8* getSourceName( ReloadSource source );
        /** @brief 이 집계를 비웁니다(테스트 · `reload.status clear`). */
        void clear();

    private:
        mutable mutex                         _mutex;
        vector<ReloadTrackerEntry> _listEntry SW_GUARDED_BY( _mutex );
        uint64 _nextSequence                  SW_GUARDED_BY( _mutex );
        uint32                                _arrAttemptCount[kReloadSourceCount] SW_GUARDED_BY( _mutex );
        float64                               _arrLastElapsedMs[kReloadSourceCount] SW_GUARDED_BY( _mutex );
        ModuleReloadPhases _lastModulePhases  SW_GUARDED_BY( _mutex );
        uint32 _failedCount                   SW_GUARDED_BY( _mutex );
    };
} // namespace sw
