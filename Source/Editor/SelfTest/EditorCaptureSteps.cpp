/**
 * @file EditorCaptureSteps.cpp
 * @brief 자동화 시나리오 단계 `CaptureEditorUI` 를 구현합니다. 에디터 UI 전체(패널 + 플로팅 창)를 PNG 로 찍고 튼튼한 지표를 단언합니다.
 * @details `<CaptureEditorUI file="ui/hierarchy.png" maxMagentaFraction="0" maxClippedWindows="0"/>`. 경로는 시나리오 출력 폴더 기준이다(`<Screenshot>` 과 같다).
 *          단계는 차례가 오면 캡처를 요청하고 파일이 써질 때까지 시나리오 시계를 붙잡는다(`holdUntilReady`). 지표는 둘이다.
 *          - `maxMagentaFraction`: 마젠타 픽셀 비율 상한(적지 않으면 0). 빠진 텍스처 · 디버그 색이 UI 에 섞이면 오른다. 적은 캡처는 백엔드 분할에서도 모든 백엔드가 돈다.
 *          - `maxClippedWindows`: 내용이 창보다 큰데 스크롤바가 없어 잘린 창 수 상한. 어느 창인지는 실패 줄에 이름으로 남는다.
 *          이미지 자체(골든)는 비교하지 않는다. 글꼴 래스터와 DPI 가 기계마다 달라 같은 기계 · 같은 설정에서만 의미가 있다. 결과 파일로 사람이나 `ux-reviewer` 가 본다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Backend/EditorUICapture.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/SelfTest/EditorScenarioStepValidation.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"

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

            /**
             * @struct Session
             * @brief 위 목록의 값입니다. 주인은 `EditorStateStore` 라 컨텍스트와 함께 생기고 사라진다.
             * @details 단계 함수는 상태를 받지 못하는 함수 포인터라 `findSubsystem` 으로 저장소를 찾는다. 정적 변수에 두면 서비스 접근 게이트가 못 보고, 컨텍스트가 바뀌어도 이전 요청 번호가 남는다.
             *          저장소는 컨텍스트와 함께 이미 있다 — `SW_EDITOR_SUBSYSTEM` 은 첫 에디터 프레임 앞에야 생겨 시나리오의 첫 프레임 단계가 못 찾았다.
             *          이 형은 이 파일 밖에서 찾을 수 없다(익명 이름공간).
             */
            struct Session
            {
                vector<PendingCapture> _listPending;
            };

            /** @brief 지금 에디터 범위의 세션입니다. 에디터 컨텍스트가 없으면 nullptr 입니다. */
            static Session* findSession()
            {
                EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>();
                return pStore != nullptr ? &pStore->get<Session>() : nullptr;
            }

            static PendingCapture* findPending( const AutomationStep& step )
            {
                Session* pSession = findSession();
                if ( pSession == nullptr )
                    return nullptr;
                for ( PendingCapture& pending : pSession->_listPending )
                {
                    if ( pending._pStep == &step )
                        return &pending;
                }
                return nullptr;
            }

            /** @brief 처음 차례가 오면 캡처를 요청하고, 결과가 나올 때까지 붙잡습니다. */
            static AutomationStepHold holdCapture( AutomationStepContext& runner, const AutomationStep& step, InputManager& input )
            {
                EditorUICapture* const pCapture = findSubsystem<EditorUICapture>();
                Session* const         pSession = findSession();
                if ( pCapture == nullptr || pSession == nullptr )
                    return AutomationStepHold::Run; // 단계가 돌며 실패를 적는다
                PendingCapture* pPending = findPending( step );
                if ( pPending == nullptr )
                {
                    pSession->_listPending.push_back( PendingCapture{ &step, pCapture->request( runner.resolveOutputPath( *step.findAttribute( "file" ) ) ) } );
                    pPending = &pSession->_listPending.back();
                }
                EditorUICaptureResult result{};
                return runner.holdUntilReady( step, input, pCapture->findResult( pPending->_ticket, result ), kCaptureWaitFrames );
            }

            /** @brief 속성 @p attributeName 의 상한을 넘으면 실패를 남깁니다. 속성이 없으면 @p defaultLimit(비면 보지 않음)입니다. */
            static void expectAtMost( AutomationStepContext& runner, const AutomationStep& step, string_view attributeName, float64 value, string_view detail,
                                      string_view defaultLimit = {} )
            {
                const string* pAttribute = step.findAttribute( attributeName );
                const string  limitText  = pAttribute != nullptr ? *pAttribute : string( defaultLimit );
                const string* pLimit     = &limitText;
                float32       limit{ 0.0f };
                if ( limitText.empty() || StringUtil::parseFloat( limitText, limit ) == false )
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

            static bool runCapture( AutomationStepContext& runner, const AutomationStep& step )
            {
                EditorUICapture* const pCapture = findSubsystem<EditorUICapture>();
                PendingCapture* const  pPending = findPending( step );
                EditorUICaptureResult  result{};
                const bool             bHasResult = pCapture != nullptr && pPending != nullptr && pCapture->findResult( pPending->_ticket, result );
                if ( pPending != nullptr )
                {
                    vector<PendingCapture>& listPending = findSession()->_listPending; // 항목을 찾았으므로 세션이 있다
                    listPending.erase( listPending.begin() + ( pPending - listPending.data() ) );
                }

                if ( bHasResult == false || result._bWritten == SW_FALSE )
                {
                    runner.recordFailure( step, bHasResult ? "the editor UI capture could not be read or written" : "the editor UI capture never completed (is the editor on?)" );
                    return true;
                }
                // 마젠타(빠진 텍스처 · 디버그 색)는 어느 화면에서든 결함이라 문턱을 적지 않으면 0 이다 — 일부러 빠진 텍스처를 보이는 캡처만 문턱을 적어 푼다.
                expectAtMost( runner, step, "maxMagentaFraction", result._magentaFraction, {}, "0" );
                expectAtMost( runner, step, "maxClippedWindows", static_cast<float64>( result._job._clippedWindowCount ), result._job._clippedWindowNames );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_AUTOMATION_STEP_WITH_HOOKS( editorCaptureUI, EditorScenarioStepValidation::kCaptureKind, &EditorCaptureStepsInternal::runCapture, &EditorScenarioStepValidation::validateCapture, nullptr,
                                   &EditorCaptureStepsInternal::holdCapture, false );
} // namespace sw::editor
