/**
 * @file CookStore.h
 * @brief 쿠킹 출력 하나를 기록(`.cookrec`)과 DDC 를 거쳐 만듭니다. 기록이 새것이면 DDC 의 출력을 다시 쓰고, 아니면 굽고 기록과 DDC 를 새로 씁니다.
 * @details 쿠커(씬 · 프리팹 · 레지스트리 · VAT · 내비메시)는 출력 하나마다 `cookFile` 을 부르고, 굽는 일은 함수로 넘긴다. 그 함수는 작업 범위(`CookJob`) 안에서
 *          돌므로 읽은 리소스가 모두 입력으로 기록된다.
 *          - 작업은 **빈 에셋 캐시로 시작한다**(`AssetManager::clearAssetCaches`). 앞 작업이 읽어 캐시에 둔 프리팹 · 머티리얼을 쓰면 그 읽기가 이 작업의
 *            입력에서 빠진다 — 캐시마다 기록을 다시 남기게 하는 대신, 캐시가 비어 있다는 것을 작업의 시작 조건으로 둔다.
 *          - 기록은 쿠킹 폴더가 아니라 `<기록 루트>/<출력 id>.cookrec` 에 둔다. 쿠킹 폴더의 파일은 모두 팩에 들어간다(`PackCooker`).
 *          - 입력을 하나도 기록하지 않은 작업은 실패다. 그 기록은 무엇이 바뀌어도 늘 새것이 된다(쿠커가 `ResourceIO` 를 거치지 않고 읽었다는 뜻이다).
 *          - 쿠커 버전은 쿠커 소스 옆의 사람이 올리는 상수다. 출력 형식이나 출력을 바꾸는 코드(기본값, 알고리즘)를 바꾸면 올린다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Resource/Cook/CookJob.h"
#include "Engine/Resource/Cook/CookRecord.h"

namespace sw
{
    /** @brief 굽는 함수가 알리는 결과입니다. */
    enum class CookStep : uint8
    {
        Written, ///< 출력 파일을 썼다
        Skipped, ///< 이 출력은 만들지 않는다(쿠킹 대상이 아님). 기록을 남기지 않는다
        Failed,  ///< 굽지 못했다(이유는 함수가 오류로 남긴다)
    };

    /** @brief `cookFile` 의 결과입니다. */
    enum class CookOutcome : uint8
    {
        Reused,  ///< 기록이 새것이라 DDC 의 출력을 다시 썼다
        Cooked,  ///< 구웠고 기록과 DDC 를 새로 썼다
        Skipped, ///< 굽는 함수가 건너뛰었다
        Failed,  ///< 굽지 못했다
    };

    /** @struct CookTally @brief 쿠킹 결과 수입니다(로그 요약 · 테스트). */
    struct CookTally
    {
        uint32 _reusedCount{ 0 };
        uint32 _cookedCount{ 0 };
        uint32 _failedCount{ 0 };

        /** @brief 결과 하나를 셉니다. */
        void add( CookOutcome outcome )
        {
            _reusedCount += outcome == CookOutcome::Reused ? 1u : 0u;
            _cookedCount += outcome == CookOutcome::Cooked ? 1u : 0u;
            _failedCount += outcome == CookOutcome::Failed ? 1u : 0u;
        }
        /** @brief 쓴 출력(다시 쓰기 · 굽기) 수입니다. */
        uint32 getWrittenCount() const { return _reusedCount + _cookedCount; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class CookStore
     * @brief 기록 폴더와 DDC 한 쌍입니다. 파일 머리말 참고.
     */
    class SW_API CookStore
    {
    public:
        CookStore( string recordRootPath, string ddcRootPath );

        /** @brief 로컬 저장소입니다. 기록은 `<프로젝트>/Saved/CookRecord`, DDC 는 `<프로젝트>/Saved/DDC` 입니다. */
        static CookStore makeLocal();

        /**
         * @brief 출력 파일 하나를 만듭니다.
         * @param desc 작업 키의 쿠커 쪽 값입니다.
         * @param outputID 쿠킹 폴더 기준 출력 경로입니다(예: `game/empty/maps/x.scene.bin`). 기록 파일 이름이 됩니다.
         * @param outputPath 출력 파일의 절대 경로입니다.
         * @param cook 출력 파일을 @p outputPath 에 쓰고 `CookStep` 을 돌려주는 함수입니다. 빈 에셋 캐시와 작업 범위 안에서 부릅니다.
         */
        template <typename CookFunction>
        [[nodiscard]] CookOutcome cookFile( const CookJobDesc& desc, string_view outputID, string_view outputPath, CookFunction&& cook ) const
        {
            if ( reuseFreshOutput( desc, outputID, outputPath ) )
            {
                _tally.add( CookOutcome::Reused );
                return CookOutcome::Reused;
            }
            clearAssetCachesForJob();
            CookJob           job( desc );
            const CookStep    step    = cook();
            const CookOutcome outcome = finishCook( job, step, outputID, outputPath );
            _tally.add( outcome );
            return outcome;
        }

        /** @brief 출력 @p outputID 의 기록 파일 경로입니다. */
        string                  makeRecordPath( string_view outputID ) const;
        const DerivedDataCache& getDerivedDataCache() const { return _derivedDataCache; }
        /** @brief 이 저장소로 만든 출력의 결과 수입니다(누적). */
        const CookTally& getTally() const { return _tally; }

    private:
        /** @brief 기록이 새것이고 DDC 에 그 출력이 있으면 출력 파일에 쓰고 true 입니다. */
        [[nodiscard]] bool reuseFreshOutput( const CookJobDesc& desc, string_view outputID, string_view outputPath ) const;
        /** @brief 작업이 쓴 출력으로 기록과 DDC 를 씁니다. */
        [[nodiscard]] CookOutcome finishCook( const CookJob& job, CookStep step, string_view outputID, string_view outputPath ) const;
        /** @brief 에셋 캐시를 모두 비웁니다(작업의 시작 조건 — 파일 머리말). */
        static void clearAssetCachesForJob();

        string            _recordRootPath;
        DerivedDataCache  _derivedDataCache;
        mutable CookTally _tally; ///< 결과 수만 센다 — 출력을 만드는 일은 저장소의 상태를 바꾸지 않는다
    };
} // namespace sw
