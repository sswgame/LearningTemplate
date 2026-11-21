#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "DevTools/Client/Automation/Runner/AutomationExitCode.h"
#include "DevTools/Client/Automation/Runner/AutomationImage.h"
#include "DevTools/Client/Automation/Runner/AutomationRunTitle.h"
#include "DevTools/Client/Automation/Runner/AutomationRunner.h"
#include "DevTools/Client/Automation/Runner/AutomationStepArgs.h"
#include "DevTools/Client/Automation/Scenario/AutomationEnvironmentSteps.h"
#include "DevTools/Client/Automation/Scenario/AutomationStepValidation.h"
#include "DevTools/Client/Automation/Scenario/AutomationWindowSteps.h"

#include "Engine/Automation/Scenario/AutomationCaptureUtil.h"
#include "Engine/Automation/Scenario/AutomationRunnerParams.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Renderer/Frame/View/RenderStability.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/GenericWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    namespace
    {
        struct AutomationRunnerStepsInternal
        {
            // 시한 상수(기다리는 프레임 · 초)는 `AutomationTimeLimits` 한 곳에 있다.
            /**
             * @brief 에셋 일이 0 이 된 뒤 스크린샷 전에 더 기다리는 프레임 수입니다. 올린 텍스처는 같은 SRV 번호에 다시 묶이지만(R-6) 그 뒤 만든
             *        스냅샷이 렌더 큐를 지나야 화면에 나온다 — 씬 빌드 한 프레임 + 큐에 쌓이는 패킷 수(링 칸 수 − 1, `RenderThread::submit` 은 한 칸을 비워 둔다).
             */
            static constexpr uint32 kAssetStabilityTrailFrames = 1u + ( constant::render::kRenderFrameQueueDepth - 1u );

            // --- 단계 인자: 검사 함수와 준비 · 실행 함수가 같은 읽기(`parse*`) 하나를 쓴다 -----------------------------------------
            //     검사가 읽어 본 뒤 실행이 결과를 버리며 다시 읽던 것을 없앤다. 실행 때 읽기가 실패하면(핫 리로드로 바뀐 단계) 버리지 않고 오류로 끝낸다.
            //     기본값은 구조체의 초기값 하나다.

            /** @brief `Press` · `Release` · `Tap` 의 인자입니다. */
            struct SlotStepArgs
            {
                InputSlot _slot{};
                uint32    _holdFrameCount{ 1 }; ///< `Tap` 이 누르고 있는 프레임 수
            };

            [[nodiscard]] static expected<SlotStepArgs> parseSlotStep( const AutomationStep& step )
            {
                AutomationSlotStepParams params;
                const expected<void>     read = step.readProperties( params );
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                SlotStepArgs args;
                args._holdFrameCount = params._hold;
                if ( InputSlot::tryParse( params._slot, args._slot ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "unknown slot (Key.E · Mouse.Left · Gamepad.A · Gamepad1.A)", params._slot } );
                return args;
            }

            // --- 엔진 단계의 검사 함수(`AutomationStepValidateFunction`). 속성의 허용 값과 오류 메시지가 여기 있다 -------------

            /** @brief `Press` · `Release` · `Tap` 입니다. `hold` 는 `Tap` 에만 씁니다. */
            static expected<void> validateSlotStep( const AutomationStep& step )
            {
                if ( string_view{ step._kind } != "Tap" && step.findAttribute( "hold" ) != nullptr )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "hold is only for <Tap>" } );
                const expected<SlotStepArgs> args = parseSlotStep( step );
                if ( args.has_value() == false )
                    return unexpected( args.error() );
                return {};
            }

            /** @brief `name` 이 등록된 전역 변수인지 봅니다. 아니면 그 이름이 대상인 오류입니다(`Variable` · `ExpectVariable` 이 함께 씁니다). */
            static expected<GlobalVariableInfo*> findRegisteredVariable( const string& name )
            {
                GlobalVariableInfo* pInfo = engine::areEngineServicesBound() ? engine::getSubsystem<GlobalVariableManager>().findVariable( name ) : nullptr;
                if ( pInfo == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "unknown global variable", name } );
                return pInfo;
            }

            static expected<void> validateVariable( const AutomationStep& step )
            {
                AutomationVariableParams params;
                const expected<void>     read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
                const expected<GlobalVariableInfo*> info = findRegisteredVariable( params._name );
                if ( info.has_value() == false )
                    return unexpected( info.error() );
                return {};
            }

            /**
             * @brief `ExpectVariable` 의 `value` · `tolerance` 를 @p info 의 타입으로 읽어 지금 값과 비교합니다(검사와 실행이 함께 씁니다).
             * @return 같으면 true. `value` 가 그 타입으로 읽히지 않거나 `tolerance` 가 음수이거나 실수 변수가 아닌데 붙었으면 그 이유입니다.
             */
            [[nodiscard]] static expected<bool> evaluateExpectVariable( const AutomationStep& step, const GlobalVariableInfo& info )
            {
                AutomationExpectVariableParams params;
                const expected<void>           read = step.readProperties( params );
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                if ( step.findAttribute( "tolerance" ) != nullptr && info._type != GlobalVariableType::Float )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "tolerance is a non-negative number for float variables only", params._name } );
                bool bMatched = false;
                if ( AutomationStepArgsUtil::compareVariable( info, string_view{ params._value }, params._tolerance, bMatched ) == false )
                {
                    return unexpected( Error{ ErrorKind::InvalidArgument, "value is not of the variable's type (enum variables take the number)", params._value }
                                           .appendDetail( " for " )
                                           .appendDetail( info.getTypeName() ) );
                }
                return bMatched;
            }

            /** @brief `ExpectVariable` 입니다. 변수가 등록돼 있고, `value` 가 그 타입으로 읽히며, `tolerance`(0 이상)는 실수 변수에만 붙습니다. */
            static expected<void> validateExpectVariable( const AutomationStep& step )
            {
                AutomationExpectVariableParams params;
                const expected<void>           read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
                const expected<GlobalVariableInfo*> info = findRegisteredVariable( params._name );
                if ( info.has_value() == false )
                    return unexpected( info.error() );
                const expected<bool> matched = evaluateExpectVariable( step, **info );
                if ( matched.has_value() == false )
                    return unexpected( matched.error() );
                return {};
            }

            /** @brief 감시 값 이름과 그 감시 값이 꼭 받아야 하는 `target` · `key` 를 봅니다. */
            static expected<void> validateWatchReference( const AutomationStep& step )
            {
                const expected<const string*> watch = step.requireAttribute( "watch" );
                if ( watch.has_value() == false )
                    return unexpected( watch.error() );
                const AutomationWatchRegistration* pRegistration = AutomationWatchRegistry::find( **watch );
                if ( pRegistration == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "unknown watch (is the game that registers it running?)", **watch } );
                const bool bQueryOnlyAttribute = pRegistration->_pQueryFunction == nullptr && ( step.findAttribute( "target" ) != nullptr || step.findAttribute( "key" ) != nullptr );
                if ( bQueryOnlyAttribute )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "this watch takes no target or key", **watch } );
                if ( pRegistration->_bNeedsTarget && step.findAttribute( "target" ) == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "needs the attribute", "target" } );
                if ( pRegistration->_bNeedsKey && step.findAttribute( "key" ) == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "needs the attribute", "key" } );
                return {};
            }

            /** @brief 감시 값 단계(`Expect`, `WaitUntil`)의 공통 부분입니다. 감시 값이 있고 비교가 정확히 하나이며 수 비교는 수로 읽혀야 합니다(`is` 는 글 비교). */
            static expected<void> validateWatchComparison( const AutomationStep& step )
            {
                const expected<void> reference = validateWatchReference( step );
                if ( reference.has_value() == false )
                    return reference;
                if ( countComparisons( step, { "equals", "near", "atLeast", "atMost", "is", "sameAs", "differsFrom", "oppositeOf", "increaseBy" } ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "needs exactly one of equals · near · atLeast · atMost · is · sameAs · differsFrom · oppositeOf · increaseBy" } );
                // `increaseFrom` 은 `increaseBy` 의 짝이다 — 한쪽만 있으면 무엇에서 늘었는지 모른다.
                if ( ( step.findAttribute( "increaseBy" ) != nullptr ) != ( step.findAttribute( "increaseFrom" ) != nullptr ) )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "increaseBy=\"N\" and increaseFrom=\"<stored name>\" go together" } );
                const bool bTextRule = step.findAttribute( "is" ) != nullptr || step.findAttribute( "sameAs" ) != nullptr || step.findAttribute( "differsFrom" ) != nullptr;
                if ( bTextRule )
                {
                    if ( step.findAttribute( "tolerance" ) == nullptr )
                        return {};
                    if ( step.findAttribute( "is" ) != nullptr )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "tolerance goes with near · oppositeOf · sameAs · differsFrom (is compares text)" } );
                    float32 tolerance = AutomationStepArgsUtil::kWatchNearTolerance;
                    return AutomationStepArgsUtil::readFloat( step, "tolerance", tolerance ); // sameAs · differsFrom 을 수로 견준다
                }
                if ( step.findAttribute( "increaseBy" ) != nullptr )
                {
                    float32 increase = 0.0f;
                    return AutomationStepArgsUtil::readFloat( step, "increaseBy", increase ); // 기억한 수는 실행 때 본다
                }
                if ( step.findAttribute( "oppositeOf" ) != nullptr )
                {
                    float32 tolerance = AutomationStepArgsUtil::kWatchNearTolerance;
                    return AutomationStepArgsUtil::readFloat( step, "tolerance", tolerance ); // 기억한 수는 실행 때 본다
                }
                const expected<AutomationStepArgsUtil::NumberComparison> comparison = AutomationStepArgsUtil::parseNumberComparison( step, AutomationStepArgsUtil::kWatchNearTolerance );
                if ( comparison.has_value() == false )
                    return unexpected( comparison.error() );
                return {};
            }

            static expected<void> validateExpect( const AutomationStep& step )
            {
                AutomationExpectParams params;
                return step.readProperties( params, { "equals", "near", "atLeast", "atMost", "increaseBy" } ).and_then( [&]()
                { return validateWatchComparison( step ); } );
            }

            /** @brief `WaitUntil` 입니다. 그 프레임에 혼자 있는지는 준비 함수(`AutomationRunnerEngineSteps::prepareWaitUntil`)가 시나리오 전체를 보고 확인합니다. */
            static expected<void> validateWaitUntil( const AutomationStep& step )
            {
                AutomationWaitUntilParams params;
                const expected<void>      read = step.readProperties( params, { "equals", "near", "atLeast", "atMost", "increaseBy" } );
                if ( read.has_value() == false )
                    return read;
                const uint32 timeoutCount = ( step.findAttribute( "timeoutFrames" ) != nullptr ? 1u : 0u ) + ( step.findAttribute( "timeoutSeconds" ) != nullptr ? 1u : 0u ) +
                                            ( step.findAttribute( "timeoutWallSeconds" ) != nullptr ? 1u : 0u );
                if ( timeoutCount > 1 )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "give one of timeoutFrames, timeoutSeconds or timeoutWallSeconds" } );
                const expected<AutomationStepArgsUtil::WaitTimeoutArgs> timeout = AutomationStepArgsUtil::parseWaitTimeout( step );
                if ( timeout.has_value() == false )
                    return unexpected( timeout.error() );
                const expected<void> comparison = validateWatchComparison( step );
                if ( comparison.has_value() == false )
                    return comparison;
                // 바깥 일을 따르는 값(`SW_AUTOMATION_WALL_WATCH`)은 벽시계로만 기다린다 — 프레임은 실시간보다 빨리 돌아 시한이 몇 초인지 정하지 못한다.
                const AutomationWatchRegistration* pRegistration = AutomationWatchRegistry::find( *step.findAttribute( "watch" ) );
                if ( pRegistration->_bWallClock && timeout->_bWallSeconds == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "this watch follows work outside the engine frame - wait for it with timeoutWallSeconds", pRegistration->_pName } );
                return {};
            }

            /** @brief `ExpectVisited` 입니다. `is`(그 값을 한 번이라도 거쳤다) 또는 `sequence`(쉼표로 적은 값을 이 순서로 거쳤다) 하나와 `since`(프레임) 입니다. */
            static expected<void> validateExpectVisited( const AutomationStep& step )
            {
                AutomationExpectVisitedParams params;
                const expected<void>          read = step.readProperties( params ).and_then( [&]()
                { return validateWatchReference( step ); } );
                if ( read.has_value() == false )
                    return read;
                if ( countComparisons( step, { "is", "sequence" } ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "needs exactly one of is · sequence" } );
                return {};
            }

            /** @brief `Record` 입니다. 감시 값 값을 보고의 `records` 에 남깁니다. */
            static expected<void> validateRecord( const AutomationStep& step )
            {
                AutomationRecordParams params;
                return step.readProperties( params ).and_then( [&]()
                { return validateWatchReference( step ); } );
            }

            /** @brief `Store` 입니다. 감시 값 값을 `as` 이름으로 기억합니다(뒤 단계의 `sameAs` · `differsFrom` 이 비교한다). */
            static expected<void> validateStore( const AutomationStep& step )
            {
                AutomationStoreParams params;
                return step.readProperties( params ).and_then( [&]()
                { return validateWatchReference( step ); } );
            }

            static expected<void> validateExpectLog( const AutomationStep& step )
            {
                // 사건은 로그 id 로만 센다 — 문장으로 세면 문구를 고칠 때 단언이 조용히 낡는다. contains 는 그 id 줄의 값(경로 · 수)을 좁히는 데만 쓴다.
                // 세려는 줄에 id 가 없으면 소스에서 `SW_LOG_*_ID` 로 쓴다.
                AutomationExpectLogParams params;
                const expected<void>      read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
                if ( ( params._count == kAutomationNoCount ) == ( params._atLeast == kAutomationNoCount ) )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "needs exactly one of count · atLeast" } );
                return {};
            }

            /**
             * @brief `<Screenshot stability>` 의 값입니다 — 찍기 전에 무엇을 기다리는가.
             * @details `None` 은 찍는 프레임이 정해져야 하는 시나리오(디더 등장의 중간 단계, 프레임마다 움직이는 카메라)가 쓴다 — 자리 잡기를 기다리면 등장이 끝나거나 카메라가 멈춘 뒤를 찍는다.
             *          그런 시나리오는 앞에서 `<WaitRenderStable/>` · `<WaitUntil watch="Assets.Pending" equals="0">` 로 자리를 잡아 둔다.
             */
            enum class ScreenshotStabilityMode : uint8
            {
                Render, ///< 에셋 일 · 화면 덮개 + 렌더 정착 조건(`AutomationBlockScope::RenderStable` — TAA 기록 · 노출 적응). 기본
                Assets, ///< 에셋 일 · 화면 덮개만(카메라가 움직이는 장면 — TAA 조건은 끝내 서지 않는다)
                None,   ///< 그 프레임 그대로
            };

            [[nodiscard]] static expected<ScreenshotStabilityMode> parseScreenshotStability( const AutomationStep& step )
            {
                AutomationScreenshotParams params;
                const expected<void>       read = step.readProperties( params );
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                if ( params._stability == "render" )
                    return ScreenshotStabilityMode::Render;
                if ( params._stability == "assets" )
                    return ScreenshotStabilityMode::Assets;
                if ( params._stability == "none" )
                    return ScreenshotStabilityMode::None;
                return unexpected( Error{ ErrorKind::InvalidArgument, "stability is render (default), assets or none", params._stability } );
            }

            static expected<void> validateScreenshot( const AutomationStep& step )
            {
                const expected<ScreenshotStabilityMode> stability = parseScreenshotStability( step );
                if ( stability.has_value() == false )
                    return unexpected( stability.error() );
                return {};
            }

            static expected<void> validateExpectImagesDiffer( const AutomationStep& step )
            {
                AutomationExpectImagesDifferParams params;
                const expected<void>               read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
                if ( params._minDiffering > params._maxDiffering )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "minDiffering must not exceed maxDiffering" } );
                AutomationImageRegion region{};
                if ( params._region.empty() == false && AutomationImage::tryParseRegion( params._region, region ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "region must be x0,y0,x1,y1 in 0..1 with x0<x1 and y0<y1", params._region } );
                return {};
            }

            static expected<void> validateExpectImage( const AutomationStep& step )
            {
                const expected<AutomationStepArgsUtil::ExpectImageArgs> args = AutomationStepArgsUtil::parseExpectImage( step );
                if ( args.has_value() == false )
                    return unexpected( args.error() );
                return {};
            }

            /** @brief 값 비교 단계(`Expect`)의 비교 하나를 읽습니다. 정확히 하나여야 합니다. */
            static bool countComparisons( const AutomationStep& step, std::initializer_list<string_view> listName )
            {
                uint32 count = 0;
                for ( const string_view name : listName )
                {
                    count += step.findAttribute( name ) != nullptr ? 1u : 0u;
                }
                return count == 1;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @struct AutomationRunnerEngineSteps
     * @brief 실행기 상태(가상 입력 원천, 스크린샷 대기열, 종료 시한)를 직접 만지는 엔진 단계의 준비 · 실행 · 차례 함수입니다. `AutomationRunner` 의 friend 입니다.
     * @details 엔진 단계도 다른 모듈의 단계와 같이 `AutomationStepRegistry` 에 등록한다. 실행기는 단계 이름으로 분기하지 않는다.
     */
    struct AutomationRunnerEngineSteps
    {
        /** @brief 엔진 단계는 실행기 자신의 상태(가상 입력 원천 · 스크린샷 대기열 · 종료 시한)를 만진다. 그래서 문맥을 실행기로 되돌려 쓴다 — 이 단계들은 실행기에만 등록되어 돈다. */
        static AutomationRunner& asRunner( IAutomationStepContext& context ) { return static_cast<AutomationRunner&>( context ); }

        // --- 준비(`AutomationStepPrepareFunction`): 입력 단계는 가상 입력 원천으로 옮긴다. 인자는 검사와 같은 `parse*` 로 읽고, 실패하면 버리지 않고 오류다 ---

        [[nodiscard]] static expected<void> prepareSlotStep( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&                                           runner = asRunner( context );
            const expected<AutomationRunnerStepsInternal::SlotStepArgs> args   = AutomationRunnerStepsInternal::parseSlotStep( step );
            if ( args.has_value() == false )
                return unexpected( args.error() );
            const string_view kind = step._kind;
            // 가상 입력 세션의 패드는 끊긴 채 시작한다 — 패드 단추를 누르는 단계는 그 패드를 먼저 연결한다(안 하면 단추가 입력 맵에 닿지 않는다).
            if ( args->_slot._deviceKind == InputDeviceKind::Gamepad )
                runner._inputScript.addGamepadConnection( step._frameIndex, args->_slot._deviceIndex );
            const bool bAdded = kind == "Tap" ? runner._inputScript.addTap( step._frameIndex, args->_slot, args->_holdFrameCount )
                                              : runner._inputScript.addSlot( step._frameIndex, args->_slot, kind == "Press" );
            if ( bAdded == false )
                return unexpected( Error{ ErrorKind::Unsupported, "that slot cannot be pressed (an axis or an unknown device)" } );
            return {};
        }

        [[nodiscard]] static expected<void> prepareWheel( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&        runner = asRunner( context );
            AutomationAxisPairParams args;
            const expected<void>     read = step.readProperties( args );
            if ( read.has_value() == false )
                return read;
            if ( args._y != 0.0f )
                runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeMouseWheel( args._y ) );
            if ( args._x != 0.0f )
                runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeMouseHorizontalWheel( args._x ) );
            return {};
        }

        [[nodiscard]] static expected<void> prepareMouseDelta( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&        runner = asRunner( context );
            AutomationAxisPairParams args;
            const expected<void>     read = step.readProperties( args );
            if ( read.has_value() == false )
                return read;
            runner._inputScript.addMouseDelta( step._frameIndex, args._x, args._y );
            return {};
        }

        [[nodiscard]] static expected<void> prepareMousePosition( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            // 창 클라이언트 영역의 비율을 픽셀로 바꾼다(시작할 때의 창 크기). 창이 없으면(헤드리스) 그 위치를 낼 수 없다.
            const GenericWindow* pWindow             = MainWindowSubsystem::findMainWindow();
            const bool           bNoWindowForPointer = pWindow == nullptr || pWindow->getWidth() == 0 || pWindow->getHeight() == 0;
            if ( bNoWindowForPointer )
                return unexpected( Error{ ErrorKind::Unsupported, "no window to place the pointer in" } );
            AutomationMousePositionParams args;
            const expected<void>          read = step.readProperties( args );
            if ( read.has_value() == false )
                return read;
            const float32 maxX = static_cast<float32>( pWindow->getWidth() - 1 );
            const float32 maxY = static_cast<float32>( pWindow->getHeight() - 1 );
            runner._inputScript.addMousePosition( step._frameIndex, static_cast<int32>( MathUtil::round( args._x * maxX ) ), static_cast<int32>( MathUtil::round( args._y * maxY ) ) );
            return {};
        }

        [[nodiscard]] static expected<void> prepareTouch( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            // 창 클라이언트 영역의 비율을 픽셀로 바꾼다(`MousePosition` 과 같은 규칙). 손가락은 마우스와 따로 간다 — 터치 장치의 포인터 0 이다.
            const GenericWindow* pWindow           = MainWindowSubsystem::findMainWindow();
            const bool           bNoWindowForTouch = pWindow == nullptr || pWindow->getWidth() == 0 || pWindow->getHeight() == 0;
            if ( bNoWindowForTouch )
                return unexpected( Error{ ErrorKind::Unsupported, "no window to touch" } );
            AutomationTouchParams args;
            const expected<void>  read = step.readProperties( args );
            if ( read.has_value() == false )
                return read;
            const int32 x = static_cast<int32>( MathUtil::round( args._x * static_cast<float32>( pWindow->getWidth() - 1 ) ) );
            const int32 y = static_cast<int32>( MathUtil::round( args._y * static_cast<float32>( pWindow->getHeight() - 1 ) ) );
            runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeTouch( RawInputEventKind::TouchDown, 0, x, y ) );
            runner._inputScript.addSpanEnd( step._frameIndex + args._holdFrameCount, RawInputEvent::makeTouch( RawInputEventKind::TouchUp, 0, x, y ), step._frameIndex );
            return {};
        }

        [[nodiscard]] static expected<void> prepareGamepadAxis( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&           runner = asRunner( context );
            AutomationGamepadAxisParams args;
            const expected<void>        read = step.readProperties( args );
            if ( read.has_value() == false )
                return read;
            // 가상 입력 세션의 패드는 끊긴 채 시작하고 끊긴 패드의 스틱은 입력 맵 · 가상 커서가 읽지 않는다 — 축을 내는 단계는 그 패드를 먼저 연결한다.
            runner._inputScript.addGamepadConnection( step._frameIndex, static_cast<uint8>( args._pad ) );
            runner._inputScript.addGamepadAxis( step._frameIndex, static_cast<uint16>( args._axis ), args._value, static_cast<uint8>( args._pad ) );
            return {};
        }

        [[nodiscard]] static expected<void> prepareText( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&              runner = asRunner( context );
            const AutomationTextStepParams params = step.readValidatedProperties<AutomationTextStepParams>();
            // 조합 글은 한 조각으로 넣는다(시나리오의 조합은 음절 몇 개라 칸을 넘지 않는다 — 넘으면 페이로드가 글자 경계에서 자른다).
            runner._inputScript.addEvent( step._frameIndex, params._bComposition ? RawInputEvent::makeTextComposition( params._value, false ) : RawInputEvent::makeTextInput( params._value ) );
            return {};
        }

        /** @brief 붙잡는 동안 같은 프레임의 다른 단계가 여러 번 돌거나(입력 전 단계) 조건보다 먼저 돌지 않게, 기다림은 그 프레임에 혼자 둔다(순차 형식은 읽기가 자리를 나눈다 — 이것은 그 약속의 그물이다). */
        [[nodiscard]] static expected<void> prepareWaitUntil( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&     runner     = asRunner( context );
            const AutomationStep* pOtherStep = runner._scenario.findOtherStepAtSameFrame( step );
            if ( pOtherStep != nullptr )
                return unexpected( Error{ ErrorKind::InvalidArgument, "<WaitUntil> must be the only step at its frame - move the other step", pOtherStep->describe() } );
            return {};
        }

        // --- 실행(`AutomationStepFunction`): 씬 틱 뒤에 돈다 -------------------------------------------------------

        static bool runVariable( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&              runner = asRunner( context );
            const AutomationVariableParams params = step.readValidatedProperties<AutomationVariableParams>();
            if ( engine::getSubsystem<GlobalVariableManager>().setValueFromString( params._name, params._value ) == false )
            {
                StringBuilder<constant::buffer::kMax256> text;
                text.appendFormat( "could not set %# to '%#'", params._name, params._value );
                runner.recordFailure( step, text.view() );
            }
            return true;
        }

        static bool runExpectVariable( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&                    runner = asRunner( context );
            const AutomationExpectVariableParams params = step.readValidatedProperties<AutomationExpectVariableParams>();
            const string&                        name   = params._name;
            const string&                        value  = params._value;
            const GlobalVariableInfo*            pInfo  = engine::getSubsystem<GlobalVariableManager>().findVariable( name );
            if ( pInfo == nullptr )
            {
                // 검사 뒤에 모듈이 언로드됐다(핫 리로드)
                StringBuilder<constant::buffer::kMax256> text;
                text.appendFormat( "%# is no longer registered", name );
                runner.recordFailure( step, text.view() );
                return true;
            }
            const expected<bool> matched = AutomationRunnerStepsInternal::evaluateExpectVariable( step, *pInfo );
            if ( matched.has_value() == false ) // 변수가 다른 타입으로 다시 등록됐다(핫 리로드)
                runner.recordFailure( step, AutomationStep::describeReason( matched.error() ) );
            else if ( *matched == false )
            {
                StringBuilder<constant::buffer::kMax256> text;
                text.appendFormat( "%# is '%#', expected '%#'", name, pInfo->getValueAsString(), value );
                if ( const string* pTolerance = step.findAttribute( "tolerance" ); pTolerance != nullptr )
                    text.appendFormat( " (tolerance %#)", *pTolerance );
                runner.recordFailure( step, text.view() );
            }
            return true;
        }

        static bool runExpect( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._evaluator.runExpect( runner, step );
            return true;
        }

        static bool runExpectVisited( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._evaluator.runExpectVisited( runner, step );
            return true;
        }

        static bool runRecord( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._evaluator.runRecord( runner, step );
            return true;
        }

        static bool runStore( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._evaluator.runStore( runner, step );
            return true;
        }

        static bool runExpectLog( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._evaluator.runExpectLog( runner, step );
            return true;
        }

        static bool runScreenshot( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner    = asRunner( context );
            const string      path      = runner.resolveOutputPath( step.readValidatedProperties<AutomationScreenshotParams>()._file );
            const string      directory = FileUtil::getDirectoryPart( path );
            if ( directory.empty() == false )
                (void)FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( directory ) ); // 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
            (void)FileUtil::tryRemoveFile( FileUtil::toDiskPath( path ) );                  // 지난 실행의 이미지를 읽지 않게 지운다
            const optional<AbsolutePath> absolutePath = AbsolutePath::tryMake( path );
            if ( absolutePath.has_value() == false )
            {
                SW_LOG_ERROR_ID( "Automation.ScreenshotPathNotAbsolute", "Screenshot '%#' was not taken: the output path is not absolute", path );
                return true;
            }
            runner._run.requestScreenshot( *absolutePath );
            return true;
        }

        static bool runExpectImage( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._evaluator.deferExpectImage( runner, step );
            return true;
        }

        static bool runExpectImagesDiffer( IAutomationStepContext& context, const AutomationStep& step )
        {
            asRunner( context )._evaluator.deferExpectImagesDiffer( step );
            return true;
        }

        /** @brief 종료 시한을 겁니다. @p bClose 면 창 닫기도 요청합니다(`CloseWindow`). 시한을 읽지 못하면(핫 리로드로 바뀐 단계) 오류로 끝나도록 false 입니다. */
        [[nodiscard]] static bool startExitDeadline( AutomationRunner& runner, const AutomationStep& step, bool bClose )
        {
            string        error;
            const float32 seconds = bClose ? step.readValidatedProperties<AutomationCloseWindowParams>()._withinSeconds : step.readValidatedProperties<AutomationExpectExitWithinParams>()._seconds;
            // 종료 시한도 멈춘 루프를 잡는 그물이다(통과는 창이 닫힌 사건) — 다른 벽시계 시한처럼 기계 배율로 늘린다.
            runner._run._exitDeadlineSeconds = MonotonicClock::nowSeconds() + AutomationRunner::scaleWallSeconds( seconds );
            if ( bClose == false )
            {
                runner._run._expectedExitCode = step.readValidatedProperties<AutomationExpectExitWithinParams>()._exitCode;
                return true;
            }
            GenericWindow* pWindow = MainWindowSubsystem::findMainWindow();
            if ( pWindow == nullptr )
                runner.recordFailure( step, "there is no window to close" );
            else
                pWindow->requestClose();
            return true;
        }

        static bool runCloseWindow( IAutomationStepContext& context, const AutomationStep& step ) { return startExitDeadline( asRunner( context ), step, true ); }

        static bool runExpectExitWithin( IAutomationStepContext& context, const AutomationStep& step ) { return startExitDeadline( asRunner( context ), step, false ); }

        static bool runPass( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._evaluator.evaluateDeferred( runner ); // 스크린샷 · 끝 차단 조건의 뒤의 일은 `holdPass` 가 다 끝난 뒤에 놓는다
            (void)step;
            runner.finish( AutomationRunResult::passed() );
            return true;
        }

        /**
         * @brief 뒤의 일 - 요청한 스크린샷의 쓰기와 등록된 끝 차단 조건(`AutomationStepRegistry::findFinishBlocker`, 편집기 UI 캡처의 파일 쓰기) - 이 끝날 때까지 `<Pass/>` 를 붙잡습니다.
         *        미룬 `<ExpectImage>` 는 그 뒤 `runPass` 가 판정한다. 상한을 넘기면 실패로 적고 끝낸다.
         */
        static AutomationStepHold holdPass( IAutomationStepContext& context, const AutomationStep& step, InputManager& input )
        {
            AutomationRunner&                       runner   = asRunner( context );
            const utf8*                             pBlocker = runner._run.hasPendingScreenshot() ? "Screenshot" : AutomationStepRegistry::findFinishBlocker()._pName;
            fixed_string<constant::buffer::kMax256> waitingFor;
            formatstring( waitingFor.data(), waitingFor.capacity(), "background work to finish before <Pass/> (%#)", pBlocker != nullptr ? pBlocker : "none" );
            const AutomationStepHold hold = runner.holdUntilReady( step, input, pBlocker == nullptr, AutomationTimeLimits::kMaxFinishWaitFrames, waitingFor.view() );
            runner._run._bFinishWaiting   = hold == AutomationStepHold::Hold;
            if ( hold == AutomationStepHold::Run && pBlocker != nullptr )
            {
                fixed_string<constant::buffer::kMax256> failure;
                formatstring( failure.data(), failure.capacity(), "background work did not finish before the scenario ended (%#)", pBlocker );
                runner._evaluator.addFailure( step, failure.view(), AutomationFailureKind::WaitTimeout );
            }
            return hold;
        }

        static bool runFail( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner  = asRunner( context );
            const string*     pReason = step.findAttribute( "reason" );
            const string_view reason  = pReason != nullptr ? string_view{ *pReason } : string_view{ "<Fail/>" };
            runner.recordFailure( step, reason );
            runner.finish( AutomationRunResult::failed( AutomationFailureKind::Assertion, reason, step.describe() ) );
            return true;
        }

        static bool runSkip( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner  = asRunner( context );
            const string*     pReason = step.findAttribute( "reason" );
            runner.finish( AutomationRunResult::skipped( pReason != nullptr ? string_view{ *pReason } : string_view{ "<Skip/>" }, false ) );
            return true;
        }

        /** @brief 감시 값이 0 이면 건너뛴다(장치 능력이 없다고 선언된 백엔드). 못 읽으면 실패다 — 읽지 못한 것을 능력 없음으로 짐작하지 않는다. */
        static bool runSkipUnless( IAutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&    runner = asRunner( context );
            AutomationWatchValue value{};
            string               message;
            if ( runner._evaluator.readWatchValue( context, step, value, message ) == false || value._bText )
            {
                StringBuilder<constant::buffer::kMax256> reason;
                reason.appendFormat( "<SkipUnless> could not read a number: %#", message );
                runner.recordFailure( step, reason.view() );
                runner.finish( AutomationRunResult::failed( AutomationFailureKind::Assertion, reason.view(), step.describe() ) );
                return true;
            }
            if ( value._number == 0.0 )
            {
                const string* pReason = step.findAttribute( "reason" );
                runner.finish( AutomationRunResult::skipped( pReason != nullptr ? string_view{ *pReason } : string_view{ "<SkipUnless/>" }, false ) );
            }
            return true;
        }

        // --- 차례(`AutomationStepHoldFunction`) -------------------------------------------------------------------

        /** @brief 조건이 참이 될 때까지(또는 시한까지) 시나리오 시계를 붙잡습니다. 놓으면 단계는 끝난다(실행 함수가 없다). */
        static AutomationStepHold holdWaitUntil( IAutomationStepContext& context, const AutomationStep& step, InputManager& input )
        {
            AutomationRunner& runner = asRunner( context );
            return runner.shouldHoldForCondition( step, input ) ? AutomationStepHold::Hold : AutomationStepHold::Run;
        }

        /**
         * @brief 렌더 정착 — 에셋 일이 0 이고 정착 조건(`AutomationStepRegistry::findStabilityCondition` — TAA 기록 · 노출 적응, 해당 없음은 막지 않는다)이 모두 설 때까지 붙잡습니다.
         * @return 섰으면 Run(이 프레임은 붙잡지 않고 붙잡기를 놓았다 — `holdUntilReady`), 아직이면 Hold, 정지 시한 · 벽시계 상한에 걸려 시나리오를 끝냈으면 Skip
         * @details 정지 시한은 그린 프레임(`RenderPresentCounter`)으로 센다 — 장치를 다시 만드는 동안처럼 그리지 않는 프레임에는 조건이 나아갈 기회가 없다. 에셋 일은 바깥 일(워커 로드)이라
         *          진척을 세지 않고 벽시계 상한(`kMaxRenderStableWallSeconds`)만 본다. 카메라가 계속 움직이면 TAA 조건의 진척(정지 프레임)이 0 에 머물러 정지 시한에 바로 진다 —
         *          움직이는 장면은 `stability="assets"` 로 찍는다. `<Screenshot stability="render">` 와 `<WaitRenderStable/>` 가 같이 쓴다.
         */
        static AutomationStepHold holdForRenderStable( AutomationRunner& runner, const AutomationStep& step, InputManager& input )
        {
            const AutomationRunPolicy* pPolicy = runner.loadRunPolicy( step );
            if ( pPolicy == nullptr )
                return AutomationStepHold::Skip;
            const uint32                     pendingJobCount = AutomationCaptureUtil::countPendingJobs( runner.findActiveObjectManager() );
            const AutomationWaitingCondition condition       = AutomationStepRegistry::findStabilityCondition();
            if ( pendingJobCount == 0 && condition.isWaiting() == false )
            {
                runner._run._stabilityStallClock.reset();
                return runner.holdUntilReady( step, input, true, 0, "the render to settle" ); // 놓는다 — 붙잡은 프레임 수를 남기고 0 으로 되돌린다
            }
            const uint64 presentedFrames = RenderPresentCounter::read();
            if ( presentedFrames != runner._run._stabilityPresentedFrames )
            {
                runner._run._stabilityPresentedFrames = presentedFrames;
                (void)runner._run._stabilityStallClock.advance( pendingJobCount == 0 ? condition._pName : nullptr, condition._progress );
            }
            const string                             conditionName = condition.isWaiting() ? string( condition._pName ) : string( "Assets.Pending" );
            StringBuilder<constant::buffer::kMax256> repeatKeyBuilder;
            repeatKeyBuilder.appendFormat( "%# %#", step._kind, conditionName );
            const string_view repeatKey = repeatKeyBuilder.view();
            if ( runner._run._stabilityStallClock.getUnchangedCount() >= pPolicy->_stallFrames )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "the render did not settle - %# made no progress for %# drawn frame(s): %#", conditionName.c_str(), runner._run._stabilityStallClock.getUnchangedCount(),
                                          condition._pWhat != nullptr ? condition._pWhat : "" );
                runner.giveUpWait( step, AutomationFailureKind::WaitStalled, textBuilder.view(), repeatKey );
                return AutomationStepHold::Skip;
            }
            StringBuilder<constant::buffer::kMax256> waitingForBuilder;
            if ( pendingJobCount > 0 )
                waitingForBuilder.appendFormat( "the render to settle (%# asset job(s) pending)", pendingJobCount );
            else
                waitingForBuilder.appendFormat( "the render to settle (%#: %#, progress %#)", conditionName.c_str(), condition._pWhat != nullptr ? condition._pWhat : "", condition._progress );
            const string             waitingFor = string( waitingForBuilder.view() );
            const AutomationStepHold hold       = runner.holdUntilReadyWithin( step, input, false, AutomationTimeLimits::kMaxRenderStableWallSeconds, waitingFor );
            if ( hold == AutomationStepHold::Hold )
                return hold;
            StringBuilder<constant::buffer::kMax256> reason;
            reason.appendFormat( "the render did not settle within its wall-clock limit - waited for %#", waitingFor );
            runner.giveUpWait( step, AutomationFailureKind::WaitTimeout, reason.view(), repeatKey );
            return AutomationStepHold::Skip;
        }

        /** @brief 같은 시나리오 프레임에 @p step 보다 앞에 적힌 단계가 있으면 true 입니다 — 그 단계는 이 엔진 프레임에 먼저 돌았다. */
        [[nodiscard]] static bool hasEarlierStepInItsFrame( const AutomationRunner& runner, const AutomationStep& step )
        {
            for ( const AutomationStep& other : runner._scenario.getSteps() )
            {
                if ( &other != &step && other._frameIndex == step._frameIndex && other._orderInFile < step._orderInFile )
                    return true;
            }
            return false;
        }

        /**
         * @brief 사진 앞에서 화면이 자리 잡을 때까지 스크린샷을 미룹니다(`stability` — `AutomationRunnerStepsInternal::ScreenshotStabilityMode`).
         *        `render`(기본)는 먼저 렌더 정착(`holdForRenderStable`)을 기다리고, 선 프레임을 하나 붙잡은 뒤 화면 덮개 · 에셋 일 · 뒤 프레임을 기다린다 — 옛
         *        정착 감시 값 `<WaitUntil>` 뒤의 `<Screenshot>` 과 같은 프레임에 찍는다(그 대기는 제 프레임을 혼자 가졌고 사진은 다음 프레임에 돌았다).
         *        `assets` 는 화면 덮개 · 에셋 일 · 뒤 프레임만 기다린다. 어느 대기든 시한 · 정지 시한에 걸리면 시나리오를 끝낸다(`AutomationRunner::giveUpWait`).
         * @details 메시 · 텍스처는 워커가 읽고 페이드는 실제 시간으로 걷힌다 — 첫 프레임을 찍으면 빈 메시 · 로드 중 회색 · 어두운 화면이 찍힌다. 시나리오마다 기다림을 적지 않고 찍는 단계가 기다린다.
         *          기다리는 동안은 `WaitUntil` 처럼 시나리오 시계를 붙잡는다 — 세면 뒤 단계의 프레임이 지나 버려 사진을 요청한 프레임에 한꺼번에 돌고,
         *          그 변화(순간이동 · 변수)가 다음 패킷이 찍는 사진에 들어간다. 상용 엔진 비교: 언리얼 스크린샷 기능 테스트는 흔들리는 기능을 끄고 지연 뒤 찍는다 — 여기서는 끄지 않고 정착을 기다린다.
         */
        static AutomationStepHold holdScreenshot( IAutomationStepContext& context, const AutomationStep& step, InputManager& input )
        {
            using Stability                     = AutomationRunnerStepsInternal::ScreenshotStabilityMode;
            AutomationRunner&         runner    = asRunner( context );
            const expected<Stability> stability = AutomationRunnerStepsInternal::parseScreenshotStability( step );
            if ( stability.has_value() == false ) // 검사 뒤 단계가 바뀌었다(핫 리로드)
            {
                runner.finish( AutomationRunResult::failed( AutomationFailureKind::LoadError, AutomationStep::describeReason( stability.error() ), step.describe() ) );
                return AutomationStepHold::Skip;
            }
            if ( *stability == Stability::None )
                return AutomationStepHold::Run;
            AutomationStepTimeline& run = runner._run;
            if ( *stability == Stability::Render && run._stableHoldFrame == AutomationStepTimeline::kNotStable )
            {
                // 같은 프레임에 앞 단계가 있으면 첫 프레임은 보지 않고 붙잡는다 — 옛 대기 단계는 제 프레임을 혼자 가져(`AutomationScenario` 의 프레임 배정) 앞 단계 다음 프레임부터 봤다.
                if ( run._holdFrameCount == 0 && hasEarlierStepInItsFrame( runner, step ) )
                    return runner.holdUntilReady( step, input, false, UINT32_MAX, "the render to settle (the step before it in this frame runs first)" );
                const AutomationStepHold stableHold = holdForRenderStable( runner, step, input );
                if ( stableHold != AutomationStepHold::Run )
                    return stableHold;
                // 선 프레임은 붙잡는다 — 다음 프레임부터 덮개 · 뒤 프레임을 센다(옛 대기 단계가 놓인 뒤 사진 단계가 다음 프레임에 돌던 것과 같은 프레임에 찍는다).
                run._stableHoldFrame = run._holdFrameCount;
                return runner.holdUntilReady( step, input, false, UINT32_MAX, "the screen to settle before the screenshot (the render settled - trailing frames)" );
            }
            const bool bStable                      = AutomationCaptureUtil::isReadyForCapture( runner.findActiveObjectManager() );
            run._assetStableFrameCount              = bStable ? run._assetStableFrameCount + 1 : 0;
            const bool                       bReady = run._assetStableFrameCount > AutomationRunnerStepsInternal::kAssetStabilityTrailFrames;
            const AutomationWaitingCondition cover  = AutomationStepRegistry::findCaptureBlocker();
            // 덮개 · 에셋 일의 시한은 렌더 정착이 선 뒤부터 센다(정착을 기다린 프레임은 그 대기의 몫이다).
            const uint32                            firstFrame = run._stableHoldFrame == AutomationStepTimeline::kNotStable ? 0u : run._stableHoldFrame + 1u;
            fixed_string<constant::buffer::kMax256> waitingFor;
            if ( cover.isWaiting() )
                formatstring( waitingFor.data(), waitingFor.capacity(), "the screen to settle before the screenshot (covered by %#)", cover._pName );
            else
                formatstring( waitingFor.data(), waitingFor.capacity(), "the screen to settle before the screenshot (%#)", bStable ? "trailing frames" : "assets pending" );
            const AutomationStepHold hold = runner.holdUntilReady( step, input, bReady, firstFrame + AutomationTimeLimits::kMaxAssetStabilityFrames, waitingFor.view() );
            if ( hold == AutomationStepHold::Hold )
                return hold;
            if ( bReady == false )
            {
                fixed_string<constant::buffer::kMax256> reason;
                if ( cover.isWaiting() )
                    formatstring( reason.data(), reason.capacity(), "the screen did not settle before the screenshot was due (still covered by %#)", cover._pName );
                else
                    formatstring( reason.data(), reason.capacity(), "assets were still loading when the screenshot was due (Assets.Pending never reached 0)" );
                StringBuilder<constant::buffer::kMax256> repeatKey;
                repeatKey.appendFormat( "%# %#", step._kind, cover.isWaiting() ? cover._pName : "Assets.Pending" );
                runner.giveUpWait( step, AutomationFailureKind::WaitTimeout, reason.view(), repeatKey.view() );
                return AutomationStepHold::Skip;
            }
            run.clearScreenshotStability();
            return hold;
        }

        /** @brief `<WaitRenderStable/>` — 렌더 정착(`holdForRenderStable`)을 기다립니다. 사진이 아닌 단계(백엔드 교체 · 창 크기 · 값 단언) 앞이나 프레임이 정해진 사진(`stability="none"`) 앞에 둔다. */
        static AutomationStepHold holdWaitRenderStable( IAutomationStepContext& context, const AutomationStep& step, InputManager& input )
        {
            return holdForRenderStable( asRunner( context ), step, input );
        }
    };
} // namespace sw

namespace sw
{
    // 엔진 단계입니다. 다른 모듈의 단계와 같은 레지스트리에 올라, 같은 이름의 등록은 중복으로 거절됩니다.
    SW_AUTOMATION_STEP( runnerPress, "Press", nullptr, &AutomationRunnerStepsInternal::validateSlotStep, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareSlotStep ) );
    SW_AUTOMATION_STEP( runnerRelease, "Release", nullptr, &AutomationRunnerStepsInternal::validateSlotStep, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareSlotStep ) );
    SW_AUTOMATION_STEP( runnerTap, "Tap", nullptr, &AutomationRunnerStepsInternal::validateSlotStep, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareSlotStep ) );
    SW_AUTOMATION_STEP( runnerMouseDelta, "MouseDelta", nullptr, &AutomationStep::validateProperties<AutomationAxisPairParams>, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareMouseDelta ) );
    SW_AUTOMATION_STEP( runnerWheel, "Wheel", nullptr, &AutomationStep::validateProperties<AutomationAxisPairParams>, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareWheel ) );
    SW_AUTOMATION_STEP( runnerMousePosition, "MousePosition", nullptr, &AutomationStep::validateProperties<AutomationMousePositionParams>, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareMousePosition ) );
    SW_AUTOMATION_STEP( runnerTouch, "Touch", nullptr, &AutomationStep::validateProperties<AutomationTouchParams>, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareTouch ) );
    SW_AUTOMATION_STEP( runnerGamepadAxis, "GamepadAxis", nullptr, &AutomationStep::validateProperties<AutomationGamepadAxisParams>, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareGamepadAxis ) );
    SW_AUTOMATION_STEP( runnerText, "Text", nullptr, &AutomationStep::validateProperties<AutomationTextStepParams>, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareText ) );
    SW_AUTOMATION_STEP( runnerVariable, "Variable", &AutomationRunnerEngineSteps::runVariable, &AutomationRunnerStepsInternal::validateVariable, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerExpectVariable, "ExpectVariable", &AutomationRunnerEngineSteps::runExpectVariable, &AutomationRunnerStepsInternal::validateExpectVariable, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerExpect, "Expect", &AutomationRunnerEngineSteps::runExpect, &AutomationRunnerStepsInternal::validateExpect, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerWaitUntil, "WaitUntil", nullptr, &AutomationRunnerStepsInternal::validateWaitUntil, AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareWaitUntil ).withHold( &AutomationRunnerEngineSteps::holdWaitUntil ) );
    SW_AUTOMATION_STEP( runnerExpectVisited, "ExpectVisited", &AutomationRunnerEngineSteps::runExpectVisited, &AutomationRunnerStepsInternal::validateExpectVisited, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerRecord, "Record", &AutomationRunnerEngineSteps::runRecord, &AutomationRunnerStepsInternal::validateRecord, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerStore, "Store", &AutomationRunnerEngineSteps::runStore, &AutomationRunnerStepsInternal::validateStore, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerExpectLog, "ExpectLog", &AutomationRunnerEngineSteps::runExpectLog, &AutomationRunnerStepsInternal::validateExpectLog, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerScreenshot, "Screenshot", &AutomationRunnerEngineSteps::runScreenshot, &AutomationRunnerStepsInternal::validateScreenshot, AutomationStepOptions{}.withHold( &AutomationRunnerEngineSteps::holdScreenshot ) );
    SW_AUTOMATION_STEP( runnerWaitRenderStable, "WaitRenderStable", nullptr, &AutomationStep::validateProperties<AutomationNoParams>,
                        AutomationStepOptions{}.withPrepare( &AutomationRunnerEngineSteps::prepareWaitUntil ).withHold( &AutomationRunnerEngineSteps::holdWaitRenderStable ).withWallBudget( AutomationTimeLimits::kMaxRenderStableWallSeconds ) );
    SW_AUTOMATION_STEP( runnerExpectImage, "ExpectImage", &AutomationRunnerEngineSteps::runExpectImage, &AutomationRunnerStepsInternal::validateExpectImage, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerExpectImagesDiffer, "ExpectImagesDiffer", &AutomationRunnerEngineSteps::runExpectImagesDiffer, &AutomationRunnerStepsInternal::validateExpectImagesDiffer, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerCloseWindow, "CloseWindow", &AutomationRunnerEngineSteps::runCloseWindow, &AutomationStep::validateProperties<AutomationCloseWindowParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerExpectExitWithin, "ExpectExitWithin", &AutomationRunnerEngineSteps::runExpectExitWithin, &AutomationStep::validateProperties<AutomationExpectExitWithinParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerPass, "Pass", &AutomationRunnerEngineSteps::runPass, &AutomationStep::validateProperties<AutomationNoParams>, AutomationStepOptions{}.withHold( &AutomationRunnerEngineSteps::holdPass ) );
    SW_AUTOMATION_STEP( runnerFail, "Fail", &AutomationRunnerEngineSteps::runFail, &AutomationStep::validateProperties<AutomationEndStepParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerSkip, "Skip", &AutomationRunnerEngineSteps::runSkip, &AutomationStep::validateProperties<AutomationEndStepParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( runnerSkipUnless, "SkipUnless", &AutomationRunnerEngineSteps::runSkipUnless, &AutomationStep::validateProperties<AutomationSkipUnlessParams>, AutomationStepOptions{} );
} // namespace sw
