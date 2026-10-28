/**
 * @file EditorAssetValidation.h
 * @brief 저장 · 임포트 직후 그 에셋에 검증 규칙(`Config/Editor/AssetValidationRules.json`)을 돌려 결과를 로그로 남깁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/Process.h"

#include "Editor/Common/EditorSubsystem.h"

namespace sw::editor
{
    class EditorExternalToolJob;

    /** @brief 검증 결과 한 줄(`Scripts/qa/ValidateAssets.py` 의 출력)을 나눈 것입니다. */
    struct AssetValidationFinding
    {
        string _severity; ///< "error" · "warning" · "info"
        string _path;     ///< 리소스 경로
        string _rule;     ///< 규칙 이름
        string _message;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorAssetValidation
     * @brief 바뀐 에셋을 모아 두었다가 검증 스크립트를 한 번에 돌리고, 오류는 Error · 경고는 Warning 로그로 남깁니다(언리얼 Data Validation 의
     *        "저장할 때 검증" 에 해당한다).
     * @details 규칙과 검사는 파이썬 한 곳(`Scripts/common/AssetValidation.py`)에 있다 — 커밋 훅 · CI · 이 에디터가 같은 코드를 부른다.
     *          스크립트가 없는 실행(배포된 에디터 없음 · 저장소 밖)이면 한 번 알리고 꺼진다. 검증은 저장을 막지 않는다 — 결과를 보고 사람이 고친다.
     */
    class EditorAssetValidation : public EditorSubsystem
    {
    public:
        EditorAssetValidation();
        ~EditorAssetValidation() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorAssetValidation";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        EditorAssetValidation( const EditorAssetValidation& )            = delete;
        EditorAssetValidation& operator=( const EditorAssetValidation& ) = delete;

        /** @brief 리소스 경로 하나를 검증 대기열에 넣습니다(같은 경로는 한 번). */
        void requestValidation( string_view resourceRelativePath );

        /** @brief 에디터 프레임마다 부릅니다. 끝난 결과를 로그로 남기고, 대기열이 있으면 다음 실행을 띄웁니다. */
        void update();

        /**
         * @brief 아직 로그로 남기지 않은 검증이 있으면 true 입니다(대기열 · 도는 실행 · 꺼내지 않은 결과).
         * @details 자동화 실행기는 이것이 참인 동안 시나리오 시계를 잡는다(`EditorFrameSteps.cpp` 의 `SW_AUTOMATION_CLOCK_HOLDER`) — 검증은 저장 뒤 파일을 늦게 읽으므로,
         *          시계가 앞서 가면 시나리오의 뒤 단계(임시 폴더 지우기)가 바꾼 트리를 읽고 그 결과가 다음 시나리오 창에 떨어진다.
         */
        bool isBusy() const;

        /** @brief 검증 명령을 만듭니다(파이썬 실행기 · 스크립트 · `--files` · 경고까지). 경로는 인자 하나씩이다. */
        static ProcessCommand makeCommand( string_view projectRoot, const vector<string>& listAssetPath );

        /** @brief 출력 한 줄을 결과로 나눕니다. 결과 줄이 아니면(요약 · 빈 줄) false 입니다. */
        [[nodiscard]] static bool parseFindingLine( string_view line, AssetValidationFinding& outFinding );

    private:
        unique_ptr<EditorExternalToolJob> _pJob;
        vector<string>                    _listPendingPath;
        string                            _projectRoot;
        uint8                             _bDisabled;
    };
} // namespace sw::editor
