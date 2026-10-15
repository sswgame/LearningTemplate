/**
 * @file EditorCaptureSteps.cpp
 * @brief 자동화 시나리오 단계 `CaptureEditorUI` 를 구현합니다. 에디터 UI 전체(패널 + 플로팅 창)를 PNG 로 찍고 튼튼한 지표를 단언합니다.
 * @details `<CaptureEditorUI file="ui/hierarchy.png" maxMagentaFraction="0" maxClippedWindows="0"/>`. 경로는 시나리오 출력 폴더 기준이다(`<Screenshot>` 과 같다).
 *          단계는 차례가 오면 캡처를 요청하고 파일이 써질 때까지 시나리오 시계를 붙잡는다(`holdUntilReady`). 지표는 둘이다.
 *          - `maxMagentaFraction`: 마젠타 픽셀 비율 상한. 빠진 텍스처 · 디버그 색이 UI 에 섞이면 오른다.
 *          - `maxClippedWindows`: 내용이 창보다 큰데 스크롤바가 없어 잘린 창 수 상한. 어느 창인지는 실패 줄에 이름으로 남는다.
 *          그림 자체(골든)는 비교하지 않는다. 글꼴 래스터와 DPI 가 기계마다 달라 같은 기계 · 같은 설정에서만 의미가 있다. 결과 파일로 사람이나 `ux-reviewer` 가 본다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Backend/EditorUICapture.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct EditorCaptureStepsInternal
        {
            /** @brief 캡처가 써질 때까지 붙잡는 최대 프레임입니다. 렌더 큐 깊이와 GPU 읽기보다 넉넉하다. */
            static constexpr uint32 kCaptureWaitFrames = 120;

            /** @brief 차례가 온 단계가 낸 요청 번호입니다. UI 스레드 전용이고, 단계가 돌면 지운다. */
            struct PendingCapture
            {
                const AutomationStep* _pStep{ nullptr };
                uint32                _ticket{ 0 };
            };

            static vector<PendingCapture>& pendingCaptures()
            {
                static vector<PendingCapture> s_listPending;
                return s_listPending;
            }

            static PendingCapture* findPending( const AutomationStep& step )
            {
                for ( PendingCapture& pending : pendingCaptures() )
                {
                    if ( pending._pStep == &step )
                        return &pending;
                }
                return nullptr;
            }

            static bool validateCapture( const AutomationStep& step, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    const string_view name{ attribute._name };
                    if ( name == "file" )
                        continue;
                    float32    value{ 0.0f };
                    const bool bLimit      = name == "maxMagentaFraction" || name == "maxClippedWindows";
                    const bool bValidLimit = bLimit && StringUtil::parseFloat( attribute._value, value ) && value >= 0.0f;
                    if ( bValidLimit )
                        continue;
                    outError = step.describe() + ": unknown or invalid attribute '" + attribute._name + "' (file · maxMagentaFraction · maxClippedWindows)";
                    return false;
                }
                const string* pFile = step.findAttribute( "file" );
                if ( pFile != nullptr && StringUtil::endsWith( *pFile, ".png", true ) )
                    return true;
                outError = step.describe() + ": needs file=\"….png\"";
                return false;
            }

            /** @brief 처음 차례가 오면 캡처를 요청하고, 결과가 나올 때까지 붙잡습니다. */
            static AutomationStepHold holdCapture( AutomationRunner& runner, const AutomationStep& step, InputManager& input )
            {
                EditorUICapture* const pCapture = findSubsystem<EditorUICapture>();
                if ( pCapture == nullptr )
                    return AutomationStepHold::Run; // 단계가 돌며 실패를 적는다
                PendingCapture* pPending = findPending( step );
                if ( pPending == nullptr )
                {
                    pendingCaptures().push_back( PendingCapture{ &step, pCapture->request( runner.resolveOutputPath( *step.findAttribute( "file" ) ) ) } );
                    pPending = &pendingCaptures().back();
                }
                EditorUICaptureResult result{};
                return runner.holdUntilReady( step, input, pCapture->findResult( pPending->_ticket, result ), kCaptureWaitFrames );
            }

            static void expectAtMost( AutomationRunner& runner, const AutomationStep& step, string_view attributeName, float64 value, string_view detail )
            {
                const string* pLimit = step.findAttribute( attributeName );
                float32       limit{ 0.0f };
                if ( pLimit == nullptr || StringUtil::parseFloat( *pLimit, limit ) == false )
                    return;
                if ( value <= static_cast<float64>( limit ) )
                    return;
                StringBuilder<constant::kMaxBuffer256> number;
                number.appendFormat( "%#", value );
                string message{ "CaptureEditorUI " };
                message.append( attributeName );
                message.append( " " );
                message.append( *pLimit );
                message.append( ", got " );
                message.append( number.c_str() );
                if ( detail.empty() == false )
                {
                    message.append( " (" );
                    message.append( detail );
                    message.append( ")" );
                }
                runner.recordFailure( step, message );
            }

            static bool runCapture( AutomationRunner& runner, const AutomationStep& step )
            {
                EditorUICapture* const pCapture = findSubsystem<EditorUICapture>();
                PendingCapture* const  pPending = findPending( step );
                EditorUICaptureResult  result{};
                const bool             bHasResult = pCapture != nullptr && pPending != nullptr && pCapture->findResult( pPending->_ticket, result );
                if ( pPending != nullptr )
                    pendingCaptures().erase( pendingCaptures().begin() + ( pPending - pendingCaptures().data() ) );

                if ( bHasResult == false || result._bWritten == SW_FALSE )
                {
                    runner.recordFailure( step, bHasResult ? "the editor UI capture could not be read or written" : "the editor UI capture never completed (is the editor on?)" );
                    return true;
                }
                expectAtMost( runner, step, "maxMagentaFraction", result._magentaFraction, {} );
                expectAtMost( runner, step, "maxClippedWindows", static_cast<float64>( result._job._clippedWindowCount ), result._job._clippedWindowNames );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_AUTOMATION_STEP_WITH_HOOKS( editorCaptureUI, "CaptureEditorUI", &EditorCaptureStepsInternal::runCapture, &EditorCaptureStepsInternal::validateCapture, nullptr,
                                   &EditorCaptureStepsInternal::holdCapture, false );
} // namespace sw::editor
