/**
 * @file ReloadTracker.h
 * @brief 설정 · 에셋 · 셰이더 리로드의 결과를 한 곳에 모읍니다(읽기 전용 집계 — 리로드 주인과 오류 처리는 그대로).
 * @details 세 리로드는 감시 · 갱신 길이 다르고(설정 `ConfigReloadSubsystem`, 셰이더 `ShaderRecompiler`, 에셋 에디터 `AssetHotReload`), 실패하면 이전 값을 지킨 채
 *          로그 한 줄만 남겼다 — "이 파일은 지금 낡은 값으로 돈다" 를 어디서도 물을 수 없었다. 각 주인이 결과를 여기에 적고, 개발 명령 `reload.status` 가 보여 준다.
 *          언리얼은 Live Coding · 설정 리로드 · 셰이더 재컴파일이 각자 로그를 쓰고 모으는 뷰가 없다 — Godot 의 "Reimport" 독도 에셋만이다. 그래서 모으는 쪽을 둔다.
 *          고정 용량: 경로마다 마지막 상태 한 줄(`kMaxPathCount`, 넘으면 가장 오래된 경로를 밀어낸다).
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
    /** @enum ReloadSource @brief 결과를 적은 리로드 길입니다. */
    enum class ReloadSource : uint8
    {
        Config, ///< `ConfigReloadSubsystem`(호스트 설정 · 등록된 설정 처리기)
        Asset,  ///< 에디터 `AssetHotReload`(에셋 캐시 · 원본 임포트)
        Shader, ///< `ShaderRecompiler`(셰이더 재컴파일)
    };

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
        ReloadSource _source{ ReloadSource::Config };
        ReloadResult _lastResult{ ReloadResult::NotTracked };

        /** @brief 마지막 시도가 실패해 이전 값으로 돌고 있으면 true 입니다. */
        bool isStale() const { return _lastResult == ReloadResult::Failed; }
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

        /**
         * @brief 사람이 읽는 표를 씁니다(`reload.status`). @p bStaleOnly 면 낡은 경로만입니다.
         * @details 한 줄: `<source> <OK|STALE> <path> attempts=N failures=M applied=#seq [reason]`. 맨 위에 경로 수와 낡은 수를 적는다.
         */
        [[nodiscard]] string formatReport( bool bStaleOnly ) const;

        /** @brief 리로드 길 이름(`Config` · `Asset` · `Shader`)입니다. */
        static const utf8* getSourceName( ReloadSource source );
        /** @brief 이 집계를 비웁니다(테스트 · `reload.status clear`). */
        void clear();

    private:
        mutable mutex                         _mutex;
        vector<ReloadTrackerEntry> _listEntry SW_GUARDED_BY( _mutex );
        uint64 _nextSequence                  SW_GUARDED_BY( _mutex );
    };
} // namespace sw
