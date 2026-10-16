#include "pch.h"

#include "Engine/Automation/AutomationRunner.h"

#include "Core/Common/HashUtil.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationEnvironmentSteps.h"
#include "Engine/Automation/AutomationImageMetric.h"
#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    /** @brief 결정성이 깨졌을 때 원인 오브젝트를 찾는 진단 스위치입니다. 1 이면 `Scene.TransformHash` 를 읽을 때 오브젝트마다 이름과 위치를 로그로 남깁니다(두 실행의 로그를 비교한다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_automationHashDump, 0, "Scene.TransformHash 를 읽을 때 오브젝트마다 이름 · 위치를 로그로 (1=켜기, 결정성 진단)" );

    namespace
    {
        struct AutomationRunnerInternal
        {
            /** @brief `<ExpectImage>` 가 스크린샷을 기다리는 최대 프레임입니다. 렌더 큐 깊이보다 넉넉하게 잡습니다. */
            static constexpr uint32 kMaxImageWaitFrames = 30;
            /** @brief `darkFraction` 의 기본 임계값(영역 중앙값에 곱한다)입니다. */
            static constexpr float32 kDefaultDarkRatio = 0.7f;
            /** @brief `CloseWindow` 의 기본 종료 시한(초)입니다. */
            static constexpr float32 kDefaultExitWithinSeconds = 10.0f;
            /** @brief `WaitUntil` 의 기본 시한(프레임)입니다. 부하가 걸린 기계에서 에디터가 에셋을 여는 데 드는 시간보다 넉넉하게 잡습니다. */
            static constexpr uint32 kDefaultWaitTimeoutFrames = 1200;

            /**
             * @brief 이 구성에 없는 단계가 들어 있는 시나리오면 그 첫 단계를 반환합니다. 개발 명령(`DevCommand`)은 Shipping 에서 빠집니다.
             * @details 그런 시나리오는 이 구성에서 돌 수 없을 뿐 틀린 것이 아니다. 읽기 오류(11)가 아니라 건너뜀(13)으로 처리한다
             *          (언리얼 자동화 테스트가 구성에 없는 문맥의 테스트를 목록에서 빼는 것과 같다).
             */
            static const AutomationStep* findStepMissingFromThisBuild( [[maybe_unused]] const AutomationScenario& scenario )
            {
#if SW_DEV_COMMANDS_ENABLED
                return nullptr;
#else
                for ( const AutomationStep& step : scenario.getSteps() )
                {
                    if ( string_view{ step._kind } == AutomationEnvironmentSteps::kDevCommandKind )
                        return &step;
                }
                return nullptr;
#endif
            }

            /** @brief 단계 속성 이름이 @p listAllowed 안에 있는지 봅니다. 아니면 false 와 이유. */
            static bool checkAttributeNames( const AutomationStep& step, std::initializer_list<string_view> listAllowed, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    bool bKnown = false;
                    for ( const string_view allowed : listAllowed )
                    {
                        bKnown = bKnown || string_view{ attribute._name } == allowed;
                    }
                    if ( bKnown == false )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                return true;
            }

            /** @brief 꼭 있어야 하는 속성입니다. 없으면 nullptr 와 이유. */
            static const string* requireAttribute( const AutomationStep& step, string_view name, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    outError = step.describe() + ": needs " + string( name ) + "=\"…\"";
                return pValue;
            }

            /** @brief 있으면 수로 읽습니다(없으면 @p fallback). 형식이 틀리면 false 와 이유. */
            [[nodiscard]] static bool readFloat( const AutomationStep& step, string_view name, float32 fallback, float32& outValue, string& outError )
            {
                outValue             = fallback;
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return true;
                if ( StringUtil::parseFloat( string_view{ *pValue }, outValue ) )
                    return true;
                outError = step.describe() + ": " + string( name ) + " must be a number, got '" + *pValue + "'";
                return false;
            }

            /** @brief 있으면 음이 아닌 정수로 읽습니다(없으면 @p fallback). */
            [[nodiscard]] static bool readUint( const AutomationStep& step, string_view name, uint32 fallback, uint32& outValue, string& outError )
            {
                outValue             = fallback;
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return true;
                int32 value = 0;
                if ( StringUtil::parseInt( string_view{ *pValue }, value ) && value >= 0 )
                {
                    outValue = static_cast<uint32>( value );
                    return true;
                }
                outError = step.describe() + ": " + string( name ) + " must be a non-negative integer, got '" + *pValue + "'";
                return false;
            }

            // --- 엔진 단계의 검사 함수(`AutomationStepValidateFunction`). 속성의 허용 값과 오류 메시지가 여기 있다 -------------

            /** @brief `Press` · `Release` · `Tap` 입니다. `hold` 는 `Tap` 에만 씁니다. */
            static bool validateSlotStep( const AutomationStep& step, string& outError )
            {
                uint32 count = 0;
                if ( checkAttributeNames( step, { "slot", "hold" }, outError ) == false || readUint( step, "hold", 1, count, outError ) == false )
                    return false;
                if ( string_view{ step._kind } != "Tap" && step.findAttribute( "hold" ) != nullptr )
                {
                    outError = step.describe() + ": hold is only for <Tap>";
                    return false;
                }
                const string* pSlot = requireAttribute( step, "slot", outError );
                InputSlot     slot{};
                if ( pSlot == nullptr )
                    return false;
                if ( InputSlotUtil::tryParse( *pSlot, slot ) == false )
                {
                    outError = step.describe() + ": unknown slot '" + *pSlot + "' (Key.E · Mouse.Left · Gamepad.A · Gamepad1.A)";
                    return false;
                }
                return true;
            }

            static bool validateMouseDelta( const AutomationStep& step, string& outError )
            {
                float32 value = 0.0f;
                return checkAttributeNames( step, { "x", "y" }, outError ) && readFloat( step, "x", 0.0f, value, outError ) && readFloat( step, "y", 0.0f, value, outError );
            }

            /** @brief `Wheel` 입니다. `y` 는 세로 휠, `x` 는 가로 휠 눈금이고 없으면 0 입니다. */
            static bool validateWheel( const AutomationStep& step, string& outError )
            {
                float32 value = 0.0f;
                return checkAttributeNames( step, { "x", "y" }, outError ) && readFloat( step, "x", 0.0f, value, outError ) && readFloat( step, "y", 0.0f, value, outError );
            }

            static bool validateMousePosition( const AutomationStep& step, string& outError )
            {
                const bool bInvalidMouseAttributes = checkAttributeNames( step, { "x", "y" }, outError ) == false || requireAttribute( step, "x", outError ) == nullptr ||
                                                     requireAttribute( step, "y", outError ) == nullptr;
                if ( bInvalidMouseAttributes )
                    return false;
                float32 valueX = 0.0f;
                float32 valueY = 0.0f;
                if ( readFloat( step, "x", 0.0f, valueX, outError ) == false || readFloat( step, "y", 0.0f, valueY, outError ) == false )
                    return false;
                const bool bMouseFractionOutOfRange = valueX < 0.0f || valueX > 1.0f || valueY < 0.0f || valueY > 1.0f;
                if ( bMouseFractionOutOfRange )
                {
                    outError = step.describe() + ": x and y are fractions of the client area (0..1)";
                    return false;
                }
                return true;
            }

            static bool validateGamepadAxis( const AutomationStep& step, string& outError )
            {
                float32 value = 0.0f;
                uint32  count = 0;
                return checkAttributeNames( step, { "axis", "value", "pad" }, outError ) && requireAttribute( step, "axis", outError ) != nullptr &&
                       requireAttribute( step, "value", outError ) != nullptr && readUint( step, "axis", 0, count, outError ) && readFloat( step, "value", 0.0f, value, outError ) &&
                       readUint( step, "pad", 0, count, outError );
            }

            static bool validateText( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "value" }, outError ) && requireAttribute( step, "value", outError ) != nullptr;
            }

            static bool validateVariable( const AutomationStep& step, string& outError )
            {
                if ( checkAttributeNames( step, { "name", "value" }, outError ) == false )
                    return false;
                const string* pName = requireAttribute( step, "name", outError );
                if ( pName == nullptr || requireAttribute( step, "value", outError ) == nullptr )
                    return false;
                if ( engine::areEngineServicesBound() == false || engine::getSubsystem<GlobalVariableManager>().findVariable( *pName ) == nullptr )
                {
                    outError = step.describe() + ": unknown global variable '" + *pName + "'";
                    return false;
                }
                return true;
            }

            /** @brief 탐침 이름과 그 탐침이 꼭 받아야 하는 `target` · `key` 를 봅니다. */
            static bool validateProbeReference( const AutomationStep& step, string& outError )
            {
                const string* pProbe = requireAttribute( step, "probe", outError );
                if ( pProbe == nullptr )
                    return false;
                const AutomationProbeRegistration* pRegistration = AutomationProbes::find( *pProbe );
                if ( pRegistration == nullptr )
                {
                    outError = step.describe() + ": unknown probe '" + *pProbe + "' (is the game that registers it running?)";
                    return false;
                }
                const bool bQueryOnlyAttribute = pRegistration->_pQueryFunction == nullptr && ( step.findAttribute( "target" ) != nullptr || step.findAttribute( "key" ) != nullptr );
                if ( bQueryOnlyAttribute )
                {
                    outError = step.describe() + ": probe '" + *pProbe + "' takes no target or key";
                    return false;
                }
                if ( pRegistration->_bNeedsTarget && requireAttribute( step, "target", outError ) == nullptr )
                    return false;
                return pRegistration->_bNeedsKey == false || requireAttribute( step, "key", outError ) != nullptr;
            }

            /** @brief 탐침 단계(`Expect`, `WaitUntil`)의 공통 부분입니다. 탐침이 있고 비교가 정확히 하나이며 수 비교는 수로 읽혀야 합니다(`is` 는 글 비교). */
            static bool validateProbeComparison( const AutomationStep& step, string& outError )
            {
                if ( validateProbeReference( step, outError ) == false )
                    return false;
                if ( countComparisons( step, { "equals", "near", "atLeast", "atMost", "is", "sameAs", "differsFrom" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost · is · sameAs · differsFrom";
                    return false;
                }
                float32 value = 0.0f;
                return readFloat( step, "equals", 0.0f, value, outError ) && readFloat( step, "near", 0.0f, value, outError ) &&
                       readFloat( step, "tolerance", 0.0f, value, outError ) && readFloat( step, "atLeast", 0.0f, value, outError ) &&
                       readFloat( step, "atMost", 0.0f, value, outError );
            }

            static bool validateExpect( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "probe", "target", "key", "equals", "near", "tolerance", "atLeast", "atMost", "is", "sameAs", "differsFrom" }, outError ) &&
                       validateProbeComparison( step, outError );
            }

            /** @brief `WaitUntil` 입니다. 그 프레임에 혼자 있는지는 준비 함수(`AutomationRunnerEngineSteps::prepareWaitUntil`)가 시나리오 전체를 보고 확인합니다. */
            static bool validateWaitUntil( const AutomationStep& step, string& outError )
            {
                uint32  count   = 0;
                float32 seconds = 0.0f;
                if ( checkAttributeNames( step,
                                          { "probe", "target", "key", "equals", "near", "tolerance", "atLeast", "atMost", "is", "sameAs", "differsFrom", "timeoutFrames",
                                            "timeoutSeconds" },
                                          outError ) == false )
                    return false;
                if ( step.findAttribute( "timeoutFrames" ) != nullptr && step.findAttribute( "timeoutSeconds" ) != nullptr )
                {
                    outError = step.describe() + ": give timeoutFrames or timeoutSeconds, not both";
                    return false;
                }
                return readUint( step, "timeoutFrames", kDefaultWaitTimeoutFrames, count, outError ) && readFloat( step, "timeoutSeconds", 0.0f, seconds, outError ) &&
                       validateProbeComparison( step, outError );
            }

            /** @brief `ExpectVisited` 입니다. `is`(그 값을 한 번이라도 거쳤다) 또는 `sequence`(쉼표로 적은 값을 이 순서로 거쳤다) 하나와 `since`(프레임) 입니다. */
            static bool validateExpectVisited( const AutomationStep& step, string& outError )
            {
                uint32 since = 0;
                if ( checkAttributeNames( step, { "probe", "target", "key", "is", "sequence", "since" }, outError ) == false || validateProbeReference( step, outError ) == false )
                    return false;
                if ( countComparisons( step, { "is", "sequence" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of is · sequence";
                    return false;
                }
                return readUint( step, "since", 0, since, outError );
            }

            /** @brief `Record` 입니다. 탐침 값을 보고의 `records` 에 남깁니다. */
            static bool validateRecord( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "probe", "target", "key" }, outError ) && validateProbeReference( step, outError );
            }

            /** @brief `Remember` 입니다. 탐침 값을 `as` 이름으로 기억합니다(뒤 단계의 `sameAs` · `differsFrom` 이 견준다). */
            static bool validateRemember( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "probe", "target", "key", "as" }, outError ) && requireAttribute( step, "as", outError ) != nullptr &&
                       validateProbeReference( step, outError );
            }

            /** @brief 쉼표로 가른 값 목록입니다(앞뒤 공백을 제거하고 빈 항목은 버린다). */
            static vector<string> splitList( string_view text )
            {
                vector<string> listValue;
                while ( text.empty() == false )
                {
                    const size_t      comma = text.find( ',' );
                    const string_view value = StringUtil::trim( text.substr( 0, comma ) );
                    if ( value.empty() == false )
                        listValue.emplace_back( value );
                    text = comma == string_view::npos ? string_view{} : text.substr( comma + 1 );
                }
                return listValue;
            }

            /** @brief 탐침 값의 글입니다(수는 끝의 0 을 정리한다). */
            static string makeValueText( const AutomationProbeValue& value ) { return value._bText ? value._text : formatNumber( value._number ); }

            static bool validateExpectLog( const AutomationStep& step, string& outError )
            {
                if ( checkAttributeNames( step, { "contains", "count", "atLeast", "since" }, outError ) == false || requireAttribute( step, "contains", outError ) == nullptr )
                    return false;
                if ( countComparisons( step, { "count", "atLeast" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of count · atLeast";
                    return false;
                }
                uint32 count = 0;
                return readUint( step, "count", 0, count, outError ) && readUint( step, "atLeast", 0, count, outError ) && readUint( step, "since", 0, count, outError );
            }

            static bool validateScreenshot( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "file" }, outError ) && requireAttribute( step, "file", outError ) != nullptr;
            }

            static bool validateExpectImage( const AutomationStep& step, string& outError )
            {
                if ( checkAttributeNames( step, { "file", "region", "metric", "ratio", "reference", "equals", "near", "tolerance", "atLeast", "atMost" }, outError ) == false ||
                     requireAttribute( step, "file", outError ) == nullptr )
                    return false;
                const string*             pMetric = requireAttribute( step, "metric", outError );
                AutomationImageMetricKind metric  = AutomationImageMetricKind::MeanLuma;
                if ( pMetric == nullptr )
                    return false;
                if ( AutomationImageMetric::tryParseKind( *pMetric, metric ) == false )
                {
                    outError = step.describe() + ": unknown metric '" + *pMetric + "' (meanLuma · darkFraction · meanRedMinusBlue · differentFrom)";
                    return false;
                }
                const string*         pRegion = step.findAttribute( "region" );
                AutomationImageRegion region{};
                if ( pRegion != nullptr && AutomationImageMetric::tryParseRegion( *pRegion, region ) == false )
                {
                    outError = step.describe() + ": region must be x0,y0,x1,y1 in 0..1 with x0<x1 and y0<y1, got '" + *pRegion + "'";
                    return false;
                }
                if ( ( metric == AutomationImageMetricKind::DifferentFrom ) != ( step.findAttribute( "reference" ) != nullptr ) )
                {
                    outError = step.describe() + ": reference=\"…\" goes with metric=\"differentFrom\" (and only with it)";
                    return false;
                }
                if ( countComparisons( step, { "equals", "near", "atLeast", "atMost" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost";
                    return false;
                }
                float32 value = 0.0f;
                return readFloat( step, "ratio", 0.0f, value, outError ) && readFloat( step, "equals", 0.0f, value, outError ) && readFloat( step, "near", 0.0f, value, outError ) &&
                       readFloat( step, "tolerance", 0.0f, value, outError ) && readFloat( step, "atLeast", 0.0f, value, outError ) &&
                       readFloat( step, "atMost", 0.0f, value, outError );
            }

            /** @brief 종료 시한 단계의 시간 속성 @p pName 하나만 받습니다. */
            static bool validateExitSeconds( const AutomationStep& step, const utf8* pName, string& outError )
            {
                float32 value = 0.0f;
                return checkAttributeNames( step, { pName }, outError ) && readFloat( step, pName, 0.0f, value, outError );
            }

            static bool validateCloseWindow( const AutomationStep& step, string& outError ) { return validateExitSeconds( step, "withinSeconds", outError ); }

            static bool validateExpectExitWithin( const AutomationStep& step, string& outError ) { return validateExitSeconds( step, "seconds", outError ); }

            static bool validatePass( const AutomationStep& step, string& outError ) { return checkAttributeNames( step, {}, outError ); }

            static bool validateFailOrSkip( const AutomationStep& step, string& outError ) { return checkAttributeNames( step, { "reason" }, outError ); }

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

            static string formatNumber( float64 value )
            {
                string text = to_string( value );
                // 끝의 0 을 정리한다(1.000000 → 1)
                while ( text.find( '.' ) != string::npos && ( text.back() == '0' || text.back() == '.' ) )
                {
                    const bool bDot = text.back() == '.';
                    text.pop_back();
                    if ( bDot )
                        break;
                }
                return text;
            }

            /** @brief 탐침 `Input.OsDeviceInputBlocked` 입니다. 무인 자동화가 사람의 마우스와 키보드를 창 메시지 단계에서 버리는 중이면 1 입니다. */
            [[nodiscard]] static bool readOsDeviceInputBlocked( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                if ( engine::areEngineServicesBound() == false )
                    return false;
                outValue = engine::getSubsystem<InputManager>().isOsDeviceInputBlocked() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 탐침 `Input.VirtualCursorEnabled` 입니다. 패드 가상 커서가 켜져 있으면 1 입니다(명령형 디렉터가 플레이 동안 켠다). */
            [[nodiscard]] static bool readVirtualCursorEnabled( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                if ( engine::areEngineServicesBound() == false )
                    return false;
                outValue = engine::getSubsystem<InputManager>().getVirtualCursor().isEnabled() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 탐침 `Terrain.WrittenEditCount` 입니다. 디스크에서 다시 읽은 뒤 높이나 스플랫을 쓴 지형 수입니다(저장하지 않은 편집이 교체 · 리로드를 넘었는지 본다). */
            [[nodiscard]] static bool readTerrainWrittenEditCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 count{ 0 };
                for ( const TerrainComponent* pTerrain : pManager->getComponentRegistry().getAll<TerrainComponent>() )
                {
                    if ( pTerrain->hasWrittenEdits() )
                        ++count;
                }
                outValue = static_cast<float64>( count );
                return true;
            }

            /** @brief 탐침 `Scene.ObjectCount` 입니다. 활성 씬의 오브젝트 수입니다(핫 리로드 · 백엔드 교체 앞뒤로 씬이 그대로인지 볼 때 씁니다). */
            [[nodiscard]] static bool readSceneObjectCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 count = 0;
                pManager->forEachGameObject( [&count]( GameObject* /*pObject*/ )
                { ++count; } );
                outValue = static_cast<float64>( count );
                return true;
            }

            /**
             * @brief 탐침 `Scene.Transitioning` 입니다. 비동기 씬 로드나 전환이 진행 중이면 1 입니다.
             * @details 씬 열기(`scene.open`)는 요청만 하고 돌아온다. 이미 같은 상태의 씬이 열려 있으면 다른 탐침에 차이가 나타나지 않으므로, 열기가 끝났는지는 이 탐침이 0 인 것으로 본다.
             */
            [[nodiscard]] static bool readSceneTransitioning( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( engine::areEngineServicesBound() == false )
                    return false;
                outValue = engine::getSubsystem<SceneManager>().isTransitioning() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 질의 탐침의 대상 오브젝트 월드 위치입니다. 대상이 없으면 이유를 적고 false 입니다. */
            [[nodiscard]] static bool readTargetPosition( const AutomationProbeQuery& query, float3& outPosition, AutomationProbeValue& outValue )
            {
                const GameObject*     pTarget = query.findTargetObject();
                const SceneComponent* pScene  = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
                if ( pScene == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "transform" ) );
                    return false;
                }
                outPosition = pScene->getWorldPosition();
                return true;
            }

            /** @brief 탐침 `Object.X` · `Object.Y` · `Object.Z` 의 몸통입니다. */
            [[nodiscard]] static bool readObjectAxis( const AutomationProbeQuery& query, uint32 axis, AutomationProbeValue& outValue )
            {
                float3 position{};
                if ( readTargetPosition( query, position, outValue ) == false )
                    return false;
                outValue.setNumber( static_cast<float64>( axis == 0 ? position._x : ( axis == 1 ? position._y : position._z ) ) );
                return true;
            }

            [[nodiscard]] static bool readObjectX( const AutomationProbeQuery& query, AutomationProbeValue& outValue ) { return readObjectAxis( query, 0, outValue ); }
            [[nodiscard]] static bool readObjectY( const AutomationProbeQuery& query, AutomationProbeValue& outValue ) { return readObjectAxis( query, 1, outValue ); }
            [[nodiscard]] static bool readObjectZ( const AutomationProbeQuery& query, AutomationProbeValue& outValue ) { return readObjectAxis( query, 2, outValue ); }

            /** @brief 탐침 `Object.Exists` 입니다. 활성 씬에 그 오브젝트가 있으면 1, 없으면 0 입니다. 고를 수 없는 대상(같은 이름 여럿, 틀린 번호 글)은 값이 없습니다. */
            [[nodiscard]] static bool readObjectExists( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                if ( query.getTargetLookup() == AutomationTargetLookup::Rejected )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                outValue.setNumber( query.getTargetLookup() == AutomationTargetLookup::Found ? 1.0 : 0.0 );
                return true;
            }

            /**
             * @brief 탐침 `Object.Property` 입니다. 대상 오브젝트의 컴포넌트 PROPERTY 값(`key="<컴포넌트 타입>.<프로퍼티>"`)입니다.
             * @details 수(float32 · int32 · uint32)와 글(string)을 읽는다. 선택과 무관하게 이름으로 고르므로 여러 오브젝트를 한 프레임에 견줄 수 있다
             *          (프리팹 인스턴스마다의 값, 플레이 전후의 값). 비트필드 bool 은 PROPERTY 주소가 없어 읽지 못한다.
             */
            [[nodiscard]] static bool readObjectProperty( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject();
                if ( pTarget == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                const size_t dot = query._key.find( '.' );
                if ( dot == string_view::npos )
                {
                    outValue.setText( "key must be <ComponentType>.<property>" );
                    return false;
                }
                const string componentName( query._key.substr( 0, dot ) );
                const string propertyName( query._key.substr( dot + 1 ) );
                Component*   pComponent = pTarget->findComponentByTypeName( hashed_string( componentName ) );
                if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                {
                    outValue.setText( "no " + componentName + " on the target" );
                    return false;
                }
                const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( propertyName ) );
                if ( pProperty == nullptr )
                {
                    outValue.setText( "no property " + propertyName );
                    return false;
                }
                if ( pProperty->_typeName == hashed_string( "float32" ) )
                    outValue.setNumber( static_cast<float64>( *pProperty->getValuePtr<float32>( pComponent ) ) );
                else if ( pProperty->_typeName == hashed_string( "int32" ) )
                    outValue.setNumber( static_cast<float64>( *pProperty->getValuePtr<int32>( pComponent ) ) );
                else if ( pProperty->_typeName == hashed_string( "uint32" ) )
                    outValue.setNumber( static_cast<float64>( *pProperty->getValuePtr<uint32>( pComponent ) ) );
                else if ( pProperty->_typeName == hashed_string( "string" ) )
                    outValue.setText( *pProperty->getValuePtr<string>( pComponent ) );
                else
                {
                    outValue.setText( "property type " + string( pProperty->_typeName.c_str() ) + " is not readable here" );
                    return false;
                }
                return true;
            }

            /** @brief 탐침 `Object.DistanceTo` 입니다. 대상과 `key` 이름의 오브젝트 사이 월드 거리(m)입니다. */
            [[nodiscard]] static bool readObjectDistanceTo( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                float3 position{};
                if ( readTargetPosition( query, position, outValue ) == false )
                    return false;
                const AutomationProbeQuery otherQuery = AutomationProbeQuery::make( query._pManager, query._key, string_view{} );
                float3                     otherPosition{};
                if ( readTargetPosition( otherQuery, otherPosition, outValue ) == false )
                    return false;
                outValue.setNumber( static_cast<float64>( float3::getDistance( position, otherPosition ) ) );
                return true;
            }

            /**
             * @brief 탐침 `Scene.TransformHash` 입니다. 활성 씬 오브젝트마다 (이름, 1 mm 로 자른 월드 위치)를 해시해 더한 32 비트 값입니다.
             * @details 오브젝트 순서 · id 와 무관하다(합). 같은 시나리오를 같은 고정 틱으로 돌리면 백엔드와 상관없이 같아야 한다 — 게임 상태는 CPU 에서만 정해진다.
             *          32 비트라 `float64` 에 정확히 담기고 `equals` · `<Record>` 로 견줄 수 있다.
             */
            [[nodiscard]] static bool readSceneTransformHash( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 sum = 0;
                pManager->forEachGameObject( [&sum]( GameObject* pObject )
                {
                    const SceneComponent* pScene   = pObject->getPrimarySceneComponent();
                    const float3          position = pScene != nullptr ? pScene->getWorldPosition() : float3{};
                    uint64                hash     = static_cast<uint64>( pObject->getName().getHash() );
                    hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._x * kHashUnitsPerMeter ) ) ) );
                    hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._y * kHashUnitsPerMeter ) ) ) );
                    hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._z * kHashUnitsPerMeter ) ) ) );
                    sum += static_cast<uint32>( HashUtil::mix64( hash ) );
                    if ( gv_automationHashDump != 0 )
                        SW_LOG_INFO( "[Scenario] hash item %# %# %# %#", pObject->getName().c_str(), position._x, position._y, position._z );
                } );
                outValue = static_cast<float64>( sum );
                return true;
            }

            /** @brief `Scene.TransformHash` 가 위치를 자르는 단위(1 mm)입니다. */
            static constexpr float32 kHashUnitsPerMeter = 1000.0f;

#if SW_PROFILER_TRACE_ENABLED
            /** @brief 탐침 `Profiler.TraceRecording` 입니다. 긴 캡처(`profile.trace record`)가 아직 모으는 중이면 1 입니다. */
            [[nodiscard]] static bool readTraceRecording( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                outValue = ProfilerTraceExport::isRecording() ? 1.0 : 0.0;
                return true;
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_AUTOMATION_PROBE( inputOsDeviceInputBlocked, "Input.OsDeviceInputBlocked", "1 while unattended automation drops the person's mouse and keyboard window events",
                         &AutomationRunnerInternal::readOsDeviceInputBlocked );
    SW_AUTOMATION_PROBE( inputVirtualCursorEnabled, "Input.VirtualCursorEnabled", "1 while the gamepad virtual cursor is on (command directors turn it on for play)",
                         &AutomationRunnerInternal::readVirtualCursorEnabled );
    SW_AUTOMATION_PROBE( sceneObjectCount, "Scene.ObjectCount", "Objects in the active scene", &AutomationRunnerInternal::readSceneObjectCount );
    SW_AUTOMATION_PROBE( terrainWrittenEditCount, "Terrain.WrittenEditCount", "Terrains whose heights or splat were written since they were last read from disk",
                         &AutomationRunnerInternal::readTerrainWrittenEditCount );
    SW_AUTOMATION_PROBE( sceneTransitioning, "Scene.Transitioning", "1 while an asynchronous scene load or transition is in flight",
                         &AutomationRunnerInternal::readSceneTransitioning );
    SW_AUTOMATION_PROBE( sceneTransformHash, "Scene.TransformHash", "Order-independent hash of every object's name and world position (1 mm) - equal across backends",
                         &AutomationRunnerInternal::readSceneTransformHash );
    SW_AUTOMATION_QUERY_PROBE( objectX, "Object.X", "World X of the target object (m)", &AutomationRunnerInternal::readObjectX, true, false );
    SW_AUTOMATION_QUERY_PROBE( objectY, "Object.Y", "World Y of the target object (m)", &AutomationRunnerInternal::readObjectY, true, false );
    SW_AUTOMATION_QUERY_PROBE( objectZ, "Object.Z", "World Z of the target object (m)", &AutomationRunnerInternal::readObjectZ, true, false );
    SW_AUTOMATION_QUERY_PROBE( objectExists, "Object.Exists", "1 when the active scene has an object named target", &AutomationRunnerInternal::readObjectExists, true, false );
    SW_AUTOMATION_QUERY_PROBE( objectProperty, "Object.Property", "Component property of the target object (key = <ComponentType>.<property>; numbers and strings)",
                               &AutomationRunnerInternal::readObjectProperty, true, true );
    SW_AUTOMATION_QUERY_PROBE( objectDistanceTo, "Object.DistanceTo", "World distance from the target object to the object named key (m)",
                               &AutomationRunnerInternal::readObjectDistanceTo, true, true );
#if SW_PROFILER_TRACE_ENABLED
    // 긴 캡처는 벽시계로 끝난다. 시나리오는 고정 프레임이 아니라 이 탐침을 `WaitUntil` 로 기다린다(빠른 백엔드는 몇 백 프레임이 0.3 초 안에 돈다).
    SW_AUTOMATION_PROBE( profilerTraceRecording, "Profiler.TraceRecording", "1 while a profile.trace record capture is still collecting",
                         &AutomationRunnerInternal::readTraceRecording );
#endif
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
        // --- 준비(`AutomationStepPrepareFunction`): 입력 단계는 가상 입력 원천으로 옮긴다. 속성은 검사 함수가 이미 읽어 봤으므로 다시 읽기는 실패할 수 없다 ---

        static bool prepareSlotStep( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            InputSlot slot{};
            (void)InputSlotUtil::tryParse( *step.findAttribute( "slot" ), slot ); // 검사 함수가 이미 확인했다
            uint32 holdFrameCount = 1;
            (void)AutomationRunnerInternal::readUint( step, "hold", 1, holdFrameCount, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            const string_view kind   = step._kind;
            const bool        bAdded = kind == "Tap" ? runner._inputScript.addTap( step._frameIndex, slot, holdFrameCount )
                                                     : runner._inputScript.addSlot( step._frameIndex, slot, kind == "Press" );
            if ( bAdded == false )
                outError = step.describe() + ": that slot cannot be pressed (an axis or an unknown device)";
            return bAdded;
        }

        static bool prepareWheel( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            float32 valueX = 0.0f;
            float32 valueY = 0.0f;
            (void)AutomationRunnerInternal::readFloat( step, "x", 0.0f, valueX, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "y", 0.0f, valueY, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            if ( valueY != 0.0f )
                runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeMouseWheel( valueY ) );
            if ( valueX != 0.0f )
                runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeMouseHorizontalWheel( valueX ) );
            return true;
        }

        static bool prepareMouseDelta( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            float32 valueX = 0.0f;
            float32 valueY = 0.0f;
            (void)AutomationRunnerInternal::readFloat( step, "x", 0.0f, valueX, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "y", 0.0f, valueY, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            runner._inputScript.addMouseDelta( step._frameIndex, valueX, valueY );
            return true;
        }

        static bool prepareMousePosition( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            // 창 클라이언트 영역의 비율을 픽셀로 바꾼다(시작할 때의 창 크기). 창이 없으면(헤드리스) 그 위치를 낼 수 없다.
            const IWindow* pWindow             = IWindow::getActiveWindow();
            const bool     bNoWindowForPointer = pWindow == nullptr || pWindow->getWidth() == 0 || pWindow->getHeight() == 0;
            if ( bNoWindowForPointer )
            {
                outError = step.describe() + ": no window to place the pointer in";
                return false;
            }
            float32 valueX = 0.0f;
            float32 valueY = 0.0f;
            (void)AutomationRunnerInternal::readFloat( step, "x", 0.0f, valueX, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "y", 0.0f, valueY, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            const float32 maxX = static_cast<float32>( pWindow->getWidth() - 1 );
            const float32 maxY = static_cast<float32>( pWindow->getHeight() - 1 );
            runner._inputScript.addMousePosition( step._frameIndex, static_cast<int32>( MathUtil::round( valueX * maxX ) ), static_cast<int32>( MathUtil::round( valueY * maxY ) ) );
            return true;
        }

        static bool prepareGamepadAxis( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            uint32  axisIndex = 0;
            uint32  padIndex  = 0;
            float32 value     = 0.0f;
            (void)AutomationRunnerInternal::readUint( step, "axis", 0, axisIndex, outError );  // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "value", 0.0f, value, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readUint( step, "pad", 0, padIndex, outError );    // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            runner._inputScript.addGamepadAxis( step._frameIndex, static_cast<uint16>( axisIndex ), value, static_cast<uint8>( padIndex ) );
            return true;
        }

        static bool prepareText( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            (void)outError;
            runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeTextInput( *step.findAttribute( "value" ) ) );
            return true;
        }

        /** @brief 붙잡는 동안 같은 프레임의 다른 단계가 여러 번 돌거나(입력 전 단계) 조건보다 먼저 돌지 않게, 기다림은 그 프레임에 혼자 둔다. */
        static bool prepareWaitUntil( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            for ( const AutomationStep& otherStep : runner._scenario.getSteps() )
            {
                if ( &otherStep != &step && otherStep._frameIndex == step._frameIndex )
                {
                    outError = step.describe() + ": <WaitUntil> must be the only step at its frame (move " + otherStep.describe() + ")";
                    return false;
                }
            }
            return true;
        }

        // --- 실행(`AutomationStepFunction`): 씬 틱 뒤에 돈다 -------------------------------------------------------

        static bool runVariable( AutomationRunner& runner, const AutomationStep& step )
        {
            const string& name = *step.findAttribute( "name" );
            if ( engine::getSubsystem<GlobalVariableManager>().setValueFromString( name, *step.findAttribute( "value" ) ) == false )
                runner.recordFailure( step, "could not set " + name + " to '" + *step.findAttribute( "value" ) + "'" );
            return true;
        }

        static bool runExpect( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runExpect( step );
            return true;
        }

        static bool runExpectVisited( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runExpectVisited( step );
            return true;
        }

        static bool runRecord( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runRecord( step );
            return true;
        }

        static bool runRemember( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runRemember( step );
            return true;
        }

        static bool runExpectLog( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runExpectLog( step );
            return true;
        }

        static bool runScreenshot( AutomationRunner& runner, const AutomationStep& step )
        {
            const string path      = runner.resolveOutputPath( *step.findAttribute( "file" ) );
            const string directory = FileUtil::getDirectoryPart( path );
            if ( directory.empty() == false )
                (void)FileUtil::ensureDirectoryExists( directory );
            (void)FileUtil::tryRemoveFile( path ); // 지난 실행의 이미지를 읽지 않게 지운다
            runner._listPendingScreenshot.push_back( path );
            ++runner._screenshotRequestedCount;
            return true;
        }

        static bool runExpectImage( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runExpectImage( step );
            return true;
        }

        /** @brief 종료 시한을 겁니다. @p bClose 면 창 닫기도 요청합니다(`CloseWindow`). */
        static void startExitDeadline( AutomationRunner& runner, const AutomationStep& step, bool bClose )
        {
            string  error;
            float32 seconds = 0.0f;
            // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다.
            (void)AutomationRunnerInternal::readFloat( step, bClose ? "withinSeconds" : "seconds", AutomationRunnerInternal::kDefaultExitWithinSeconds, seconds, error );
            runner._exitDeadlineSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 + static_cast<float64>( seconds );
            if ( bClose == false )
                return;
            IWindow* pWindow = IWindow::getActiveWindow();
            if ( pWindow == nullptr )
                runner.recordFailure( step, "there is no window to close" );
            else
                pWindow->requestClose();
        }

        static bool runCloseWindow( AutomationRunner& runner, const AutomationStep& step )
        {
            startExitDeadline( runner, step, true );
            return true;
        }

        static bool runExpectExitWithin( AutomationRunner& runner, const AutomationStep& step )
        {
            startExitDeadline( runner, step, false );
            return true;
        }

        static bool runPass( AutomationRunner& runner, const AutomationStep& step )
        {
            (void)step;
            runner.finish( AutomationResult::Passed, "<Pass/>" );
            return true;
        }

        static bool runFail( AutomationRunner& runner, const AutomationStep& step )
        {
            const string*     pReason = step.findAttribute( "reason" );
            const string_view reason  = pReason != nullptr ? string_view{ *pReason } : string_view{ "<Fail/>" };
            runner.recordFailure( step, reason );
            runner.finish( AutomationResult::Failed, reason );
            return true;
        }

        static bool runSkip( AutomationRunner& runner, const AutomationStep& step )
        {
            const string* pReason = step.findAttribute( "reason" );
            runner.finish( AutomationResult::Skipped, pReason != nullptr ? string_view{ *pReason } : string_view{ "<Skip/>" } );
            return true;
        }

        // --- 차례(`AutomationStepHoldFunction`) -------------------------------------------------------------------

        /** @brief 조건이 참이 될 때까지(또는 시한까지) 시나리오 시계를 붙잡습니다. 놓으면 단계는 끝난다(실행 함수가 없다). */
        static AutomationStepHold holdWaitUntil( AutomationRunner& runner, const AutomationStep& step, InputManager& input )
        {
            return runner.shouldHoldForCondition( step, input ) ? AutomationStepHold::Hold : AutomationStepHold::Run;
        }

        /** @brief 요청한 스크린샷이 다 써질 때까지 기다립니다. 상한을 넘기면 실패로 적고 측정하지 않고 넘깁니다. */
        static AutomationStepHold holdExpectImage( AutomationRunner& runner, const AutomationStep& step, InputManager& input )
        {
            (void)input;
            const bool bPending = runner._listPendingScreenshot.empty() == false || runner._screenshotCompletedCount < runner._screenshotRequestedCount;
            if ( bPending == false )
                return AutomationStepHold::Run;
            if ( ++runner._imageWaitFrameCount <= AutomationRunnerInternal::kMaxImageWaitFrames )
                return AutomationStepHold::Hold;
            runner.recordFailure( step, "the screenshot never completed (is there a renderer?)" );
            return AutomationStepHold::Skip;
        }
    };
} // namespace sw

namespace sw
{
    // 엔진 단계입니다. 다른 모듈의 단계와 같은 레지스트리에 올라, 같은 이름의 등록은 중복으로 거절됩니다.
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerPress, "Press", nullptr, &AutomationRunnerInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerRelease, "Release", nullptr, &AutomationRunnerInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerTap, "Tap", nullptr, &AutomationRunnerInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerMouseDelta, "MouseDelta", nullptr, &AutomationRunnerInternal::validateMouseDelta, &AutomationRunnerEngineSteps::prepareMouseDelta,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerWheel, "Wheel", nullptr, &AutomationRunnerInternal::validateWheel, &AutomationRunnerEngineSteps::prepareWheel, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerMousePosition, "MousePosition", nullptr, &AutomationRunnerInternal::validateMousePosition,
                                   &AutomationRunnerEngineSteps::prepareMousePosition, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerGamepadAxis, "GamepadAxis", nullptr, &AutomationRunnerInternal::validateGamepadAxis, &AutomationRunnerEngineSteps::prepareGamepadAxis,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerText, "Text", nullptr, &AutomationRunnerInternal::validateText, &AutomationRunnerEngineSteps::prepareText, nullptr, false );
    SW_AUTOMATION_STEP( runnerVariable, "Variable", &AutomationRunnerEngineSteps::runVariable, &AutomationRunnerInternal::validateVariable, false );
    SW_AUTOMATION_STEP( runnerExpect, "Expect", &AutomationRunnerEngineSteps::runExpect, &AutomationRunnerInternal::validateExpect, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerWaitUntil, "WaitUntil", nullptr, &AutomationRunnerInternal::validateWaitUntil, &AutomationRunnerEngineSteps::prepareWaitUntil,
                                   &AutomationRunnerEngineSteps::holdWaitUntil, false );
    SW_AUTOMATION_STEP( runnerExpectVisited, "ExpectVisited", &AutomationRunnerEngineSteps::runExpectVisited, &AutomationRunnerInternal::validateExpectVisited, false );
    SW_AUTOMATION_STEP( runnerRecord, "Record", &AutomationRunnerEngineSteps::runRecord, &AutomationRunnerInternal::validateRecord, false );
    SW_AUTOMATION_STEP( runnerRemember, "Remember", &AutomationRunnerEngineSteps::runRemember, &AutomationRunnerInternal::validateRemember, false );
    SW_AUTOMATION_STEP( runnerExpectLog, "ExpectLog", &AutomationRunnerEngineSteps::runExpectLog, &AutomationRunnerInternal::validateExpectLog, false );
    SW_AUTOMATION_STEP( runnerScreenshot, "Screenshot", &AutomationRunnerEngineSteps::runScreenshot, &AutomationRunnerInternal::validateScreenshot, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerExpectImage, "ExpectImage", &AutomationRunnerEngineSteps::runExpectImage, &AutomationRunnerInternal::validateExpectImage, nullptr,
                                   &AutomationRunnerEngineSteps::holdExpectImage, false );
    SW_AUTOMATION_STEP( runnerCloseWindow, "CloseWindow", &AutomationRunnerEngineSteps::runCloseWindow, &AutomationRunnerInternal::validateCloseWindow, false );
    SW_AUTOMATION_STEP( runnerExpectExitWithin, "ExpectExitWithin", &AutomationRunnerEngineSteps::runExpectExitWithin, &AutomationRunnerInternal::validateExpectExitWithin,
                        false );
    SW_AUTOMATION_STEP( runnerPass, "Pass", &AutomationRunnerEngineSteps::runPass, &AutomationRunnerInternal::validatePass, false );
    SW_AUTOMATION_STEP( runnerFail, "Fail", &AutomationRunnerEngineSteps::runFail, &AutomationRunnerInternal::validateFailOrSkip, false );
    SW_AUTOMATION_STEP( runnerSkip, "Skip", &AutomationRunnerEngineSteps::runSkip, &AutomationRunnerInternal::validateFailOrSkip, false );
} // namespace sw

namespace sw
{
    AutomationRunner::AutomationRunner()
        : _scenario{}
        , _inputScript{}
        , _logCapture{}
        , _logPolicy{}
        , _reportWriter{}
        , _listFailure{}
        , _listPendingScreenshot{}
        , _listWatch{}
        , _listRemembered{}
        , _outputDirectory{}
        , _finishReason{}
        , _pObjectManagerOverride{ nullptr }
        , _pInputManager{ nullptr }
        , _exitDeadlineSeconds{ 0.0 }
        , _loadDeadlineSeconds{ 0.0 }
        , _nextStepIndex{ 0 }
        , _frameIndex{ 0 }
        , _waitFrameCount{ 0 }
        , _screenshotRequestedCount{ 0 }
        , _screenshotCompletedCount{ 0 }
        , _imageWaitFrameCount{ 0 }
        , _holdFrameCount{ 0 }
        , _result{ AutomationResult::Running }
        , _bStarted{ SW_FALSE }
        , _bEnded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    // 로그 리스너 해제는 `_logCapture` 의 소멸자가 맡는다.
    AutomationRunner::~AutomationRunner() = default;

    void AutomationRunner::startFromPath( string_view scenarioPath, string_view reportPath )
    {
        _reportWriter.setReportPath( reportPath );
        string error;
        if ( _scenario.loadFromPath( scenarioPath, error ) == false )
        {
            SW_LOG_ERROR( "[Scenario] could not load '%#': %#", string( scenarioPath ).c_str(), error.c_str() );
            finish( AutomationResult::LoadError, error );
            return;
        }
        _outputDirectory = "Saved/Automation/" + _scenario.getName();
        if ( skipIfStepsAreMissingFromThisBuild() )
            return;
        SW_LOG_INFO( "[Scenario] %# loaded from %# (%# step(s), fixed delta %#)", _scenario.getName().c_str(), string( scenarioPath ).c_str(),
                     static_cast<uint32>( _scenario.getSteps().size() ), _scenario.getFixedDelta() );
        if ( _scenario.getInputMode() == VirtualInputMode::Mixed )
            SW_LOG_WARNING( "[Scenario] %# uses mixed input - do not touch the keyboard or mouse while it runs", _scenario.getName().c_str() );
    }

    bool AutomationRunner::startFromText( string_view xmlText, string_view reportPath )
    {
        _reportWriter.setReportPath( reportPath );
        string error;
        if ( _scenario.parse( xmlText, "<text>", error ) == false )
        {
            finish( AutomationResult::LoadError, error );
            return false;
        }
        _outputDirectory = "Saved/Automation/" + _scenario.getName();
        return skipIfStepsAreMissingFromThisBuild() == false;
    }

    bool AutomationRunner::skipIfStepsAreMissingFromThisBuild()
    {
        const AutomationStep* pMissingStep = AutomationRunnerInternal::findStepMissingFromThisBuild( _scenario );
        if ( pMissingStep == nullptr )
            return false;
        finish( AutomationResult::Skipped, pMissingStep->describe() + ": dev commands are compiled out of this build" );
        return true;
    }

    GameObjectManager* AutomationRunner::findActiveObjectManager() const
    {
        if ( _pObjectManagerOverride != nullptr )
            return _pObjectManagerOverride;
        if ( engine::areEngineServicesBound() == false )
            return nullptr;
        Scene* pScene = engine::getSubsystem<SceneManager>().getActiveScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    void AutomationRunner::onFrameBegin( InputManager& input )
    {
        if ( _result != AutomationResult::Running )
            return;
        if ( _bStarted == SW_FALSE )
        {
            // 씬 플레이 중이란 활성 씬이 플레이를 시작했고 시작 차단 조건이 모두 풀렸다는 뜻이다(UI 의 로딩 화면은 게임 입력을 막으므로, 그동안 넣은 입력은 폰에 닿지 않는다).
            const GameObjectManager* pManager = findActiveObjectManager();
            const bool               bBlocked = AutomationStepRegistry::isStartBlocked();
            const bool               bReady   = _scenario.getStartCondition() == AutomationStartCondition::Immediately || ( pManager != nullptr && pManager->hasBegunPlay() && bBlocked == false );
            // 배타 시나리오는 시작 조건을 기다리는 프레임에도 사람의 장치를 버린다. 기다리는 동안 게임이 실제 마우스 위치를 읽으면(건설 커서 등)
            // 그 값이 게임 상태에 남아 같은 시나리오가 실행마다 다른 결과를 낸다. 시작할 때 다시 붙여 가상 입력 프레임을 0 부터 센다.
            const bool bBlockWhileWaiting = _scenario.getInputMode() == VirtualInputMode::Exclusive && input.getVirtualInput() != &_inputScript;
            if ( bBlockWhileWaiting )
            {
                input.attachVirtualInput( &_inputScript, VirtualInputMode::Exclusive );
                input.setOsDeviceInputBlocked( true );
            }
            if ( bReady == false )
            {
                if ( ++_waitFrameCount > _scenario.getStartTimeoutFrames() )
                    finish( AutomationResult::TimedOut, "the start condition never became true" );
                return;
            }
            _pInputManager = &input;
            string error;
            if ( prepare( error ) == false )
            {
                finish( AutomationResult::LoadError, error );
                return;
            }
            _bStarted = SW_TRUE;
            input.attachVirtualInput( &_inputScript, _scenario.getInputMode() );
            // 배타 시나리오는 무인 실행이다. 사람이 같은 PC 에서 마우스를 움직여도 에디터 UI 까지 결과가 바뀌지 않게 장치 이벤트를 창 메시지 단계에서 버린다.
            input.setOsDeviceInputBlocked( _scenario.getInputMode() == VirtualInputMode::Exclusive );
            SW_LOG_INFO( "[Scenario] %# started after %# frame(s)", _scenario.getName().c_str(), _waitFrameCount );
            startLoadWindow();
        }
        _logCapture.setFrameIndex( _frameIndex );
        // 액션 계층 단계(레지스트리의 `_bBeforeInput`)는 그 프레임의 입력 재생과 게임 틱 전에 넣는다.
        const vector<AutomationStep>& listStep = _scenario.getSteps();
        for ( size_t stepIndex = _nextStepIndex; stepIndex < listStep.size() && listStep[stepIndex]._frameIndex == _frameIndex; ++stepIndex )
        {
            const AutomationStepRegistration* pRegistration    = AutomationStepRegistry::find( listStep[stepIndex]._kind );
            const bool                        bRunsBeforeInput = pRegistration != nullptr && pRegistration->_bBeforeInput && pRegistration->_pRun != nullptr;
            if ( bRunsBeforeInput && pRegistration->_pRun( *this, listStep[stepIndex] ) == false )
                finish( AutomationResult::LoadError, listStep[stepIndex].describe() + ": the step could not run" );
        }
    }

    AutomationResult AutomationRunner::onFrameEnd( InputManager& input )
    {
        if ( _result == AutomationResult::Running && _bStarted == SW_TRUE )
        {
            sampleWatches();
            const vector<AutomationStep>& listStep = _scenario.getSteps();
            // `<=` 이므로 스크린샷을 기다리느라 밀린 단계는 다음 프레임에 이어서 돈다.
            while ( true )
            {
                const bool bNextStepDue = _nextStepIndex < listStep.size() && listStep[_nextStepIndex]._frameIndex <= _frameIndex && _result == AutomationResult::Running;
                if ( bNextStepDue == false )
                    break;
                const AutomationStep&             step          = listStep[_nextStepIndex];
                const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
                const AutomationStepHold          hold          = pRegistration != nullptr && pRegistration->_pHold != nullptr ? pRegistration->_pHold( *this, step, input )
                                                                                                                               : AutomationStepHold::Run;
                if ( hold == AutomationStepHold::Hold )
                    break;
                _imageWaitFrameCount = 0;
                ++_nextStepIndex;
                if ( hold == AutomationStepHold::Skip )
                    continue;
                // 등록이 없으면 검사 뒤에 그 단계를 등록한 모듈이 내려갔다(핫 리로드). 실행 함수가 없는 단계(입력)는 준비에서 이미 끝났다.
                const bool bRunsNow = pRegistration != nullptr && pRegistration->_bBeforeInput == false && pRegistration->_pRun != nullptr;
                if ( pRegistration == nullptr || ( bRunsNow && pRegistration->_pRun( *this, step ) == false ) )
                    finish( AutomationResult::LoadError, step.describe() + ": the step could not run" );
            }
            const bool bExitDeadlineExceeded = _result == AutomationResult::Running && _exitDeadlineSeconds > 0.0 &&
                                               _exitDeadlineSeconds < static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
            if ( bExitDeadlineExceeded )
                finish( AutomationResult::Failed, "the window was asked to close but the loop kept running past ExpectExitWithin" );
            // 끝난 프레임은 세지 않는다. 보고의 프레임은 끝낸 단계의 프레임이다. `WaitUntil` 이 붙잡은 프레임도 세지 않는다(뒤 단계가 그만큼 밀린다).
            if ( _result == AutomationResult::Running && _holdFrameCount == 0 )
                ++_frameIndex;
            // 창 닫기를 기다리는 동안은 프레임 시한을 보지 않는다(벽시계 시한이 본다). 부하 시나리오는 측정 창이 끝내므로 벽시계 그물만 본다.
            const bool bFrameTimeoutReached = _result == AutomationResult::Running && _exitDeadlineSeconds <= 0.0 && _loadDeadlineSeconds <= 0.0 &&
                                              _frameIndex > _scenario.getTimeoutFrames();
            if ( bFrameTimeoutReached )
                finish( AutomationResult::TimedOut, "timeoutFrames reached without <Pass/>" );
            const bool bLoadDeadlineExceeded = _result == AutomationResult::Running && _loadDeadlineSeconds > 0.0 &&
                                               _loadDeadlineSeconds < static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
            if ( bLoadDeadlineExceeded )
                finish( AutomationResult::TimedOut, "the <Load> measurement window did not close in time (was -gv_profileFrames also given?)" );
        }
        if ( _result != AutomationResult::Running )
            endRun( &input );
        return _result;
    }

    void AutomationRunner::startLoadWindow()
    {
        const AutomationLoad& load = _scenario.getLoad();
        if ( load._bDeclared == false )
            return;
        // 측정 창은 시나리오가 시작한 프레임부터다(로딩을 빼고 잰다). 끝은 측정 보고가 낸 종료 요청이다(`FrameProfileSession` 이 늦게 건 목표를 읽는다).
        const int32 seconds = static_cast<int32>( MathUtil::ceil( load._durationSeconds ) );
        if ( engine::getSubsystem<GlobalVariableManager>().setValueFromString( "gv_profileSeconds", to_string( seconds ) ) == false )
        {
            finish( AutomationResult::LoadError, "<Load> could not set gv_profileSeconds (a build without the frame profiler?)" );
            return;
        }
        // 멈춤 그물 — 워밍업(60 프레임)과 느린 기계를 넉넉히 덮는다.
        constexpr float64 kLoadDeadlineSlackSeconds = 120.0;
        _loadDeadlineSeconds                        = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 + 2.0 * static_cast<float64>( seconds ) + kLoadDeadlineSlackSeconds;
        SW_LOG_INFO( "[Scenario] load window %# s (clients %#, meshes %#, movers %#, network '%#')", seconds, load._clientCount, load._meshCount, load._moverCount,
                     load._network.c_str() );
    }

    AutomationResult AutomationRunner::onWindowClosed( InputManager* pInput )
    {
        if ( _result == AutomationResult::Running )
        {
            if ( _exitDeadlineSeconds > 0.0 )
            {
                const float64 nowSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
                if ( nowSeconds <= _exitDeadlineSeconds )
                    finish( AutomationResult::Passed, "the window closed within ExpectExitWithin" );
                else
                    finish( AutomationResult::Failed, "the window closed after the ExpectExitWithin deadline" );
            }
            else if ( _loadDeadlineSeconds > 0.0 && _nextStepIndex >= _scenario.getSteps().size() )
            {
                // 부하 시나리오의 정상 끝 — 측정 창이 닫혀 엔진이 종료를 요청했다(보고가 남았는지는 실행기가 출력으로 본다).
                finish( AutomationResult::Passed, "the <Load> measurement window closed" );
            }
            else
            {
                finish( AutomationResult::Failed, "the window closed before the scenario ended" );
            }
        }
        endRun( pInput );
        return _result;
    }

    void AutomationRunner::recordFailure( const AutomationStep& step, string_view message )
    {
        string line = step.describe() + ": " + string( message );
        SW_LOG_WARNING( "[Scenario] %#", line.c_str() );
        _listFailure.push_back( std::move( line ) );
    }

    void AutomationRunner::finish( AutomationResult result, string_view reason )
    {
        if ( _result != AutomationResult::Running )
            return;
        _result       = ( result == AutomationResult::Passed && _listFailure.empty() == false ) ? AutomationResult::Failed : result;
        _finishReason = string( reason );
    }

    const utf8* AutomationRunner::getResultName( AutomationResult result )
    {
        switch ( result )
        {
            case AutomationResult::Running:
                return "RUNNING";
            case AutomationResult::Passed:
                return "PASS";
            case AutomationResult::Failed:
                return "FAIL";
            case AutomationResult::LoadError:
                return "LOAD ERROR";
            case AutomationResult::TimedOut:
                return "TIMEOUT";
            case AutomationResult::Skipped:
                return "SKIP";
        }
        return "UNKNOWN";
    }

    bool AutomationRunner::prepare( string& outError )
    {
        for ( const AutomationStep& step : _scenario.getSteps() )
        {
            const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
            if ( pRegistration == nullptr )
            {
                outError = step.describe() + ": unknown step (nothing registered under that name)";
                return false;
            }
            if ( pRegistration->_pValidate != nullptr && pRegistration->_pValidate( step, outError ) == false )
                return false;
            if ( pRegistration->_pPrepare != nullptr && pRegistration->_pPrepare( *this, step, outError ) == false )
                return false;
        }
        if ( _logPolicy.loadAllowList( outError ) == false )
            return false;
        for ( const AutomationStep& step : _scenario.getSteps() )
        {
            if ( step._kind == "ExpectLog" )
                _logPolicy.addAllowed( *step.findAttribute( "contains" ), "declared by <ExpectLog>" ); // 검사 함수가 contains 를 요구했다
        }
        prepareWatches();
        _listRemembered.clear();
        _logCapture.start();
        return true;
    }

    void AutomationRunner::runExpect( const AutomationStep& step )
    {
        bool   bMatched = false;
        string message;
        if ( compareProbe( step, bMatched, message ) == false )
            recordFailure( step, message );
        else if ( bMatched == false )
            recordFailure( step, "Expect " + message );
    }

    AutomationStepHold AutomationRunner::holdUntilReady( [[maybe_unused]] const AutomationStep& step, InputManager& input, bool bReady, uint32 timeoutFrames )
    {
        if ( bReady || _holdFrameCount >= timeoutFrames )
        {
            if ( bReady && _holdFrameCount > 0 )
                SW_LOG_INFO( "[Scenario] %# held the clock for %# frame(s)", step.describe().c_str(), _holdFrameCount );
            _holdFrameCount = 0;
            return AutomationStepHold::Run;
        }
        ++_holdFrameCount;
        // 이 프레임은 시나리오 프레임으로 세지 않는다. 다음 프레임에 낼 입력 이벤트부터 한 프레임씩 밀어 시나리오 프레임과 맞춘다.
        _inputScript.delayEventsFrom( input.getVirtualFrameIndex(), 1 );
        return AutomationStepHold::Hold;
    }

    bool AutomationRunner::shouldHoldForCondition( const AutomationStep& step, InputManager& input )
    {
        bool       bMatched = false;
        string     message;
        const bool bHasValue = compareProbe( step, bMatched, message );
        const bool bReady    = bHasValue && bMatched;
        string     error;
        uint32     timeoutFrames  = 0;
        float32    timeoutSeconds = 0.0f;
        (void)AutomationRunnerInternal::readUint( step, "timeoutFrames", AutomationRunnerInternal::kDefaultWaitTimeoutFrames, timeoutFrames,
                                                  error );                                                // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        (void)AutomationRunnerInternal::readFloat( step, "timeoutSeconds", 0.0f, timeoutSeconds, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        if ( step.findAttribute( "timeoutSeconds" ) != nullptr )
            timeoutFrames = convertSecondsToFrames( timeoutSeconds );
        const uint32 heldFrameCount = _holdFrameCount;
        if ( holdUntilReady( step, input, bReady, timeoutFrames ) == AutomationStepHold::Hold )
            return true;
        if ( bReady == false )
            recordFailure( step, "WaitUntil gave up after " + to_string( heldFrameCount ) + " frame(s): " + message );
        return false;
    }

    bool AutomationRunner::readProbeValue( const AutomationStep& step, AutomationProbeValue& outValue, string& outMessage ) const
    {
        const string&                      probeName     = *step.findAttribute( "probe" );
        const AutomationProbeRegistration* pRegistration = AutomationProbes::find( probeName );
        // 모듈을 언로드했다 로드하면 탐침이 빠질 수 있다. 시작 때 있었어도 다시 찾는다.
        if ( pRegistration == nullptr )
        {
            outMessage = probeName + " is no longer registered";
            return false;
        }
        const string*              pTarget = step.findAttribute( "target" );
        const string*              pKey    = step.findAttribute( "key" );
        const AutomationProbeQuery query   = AutomationProbeQuery::make( findActiveObjectManager(), pTarget != nullptr ? string_view{ *pTarget } : string_view{},
                                                                       pKey != nullptr ? string_view{ *pKey } : string_view{} );
          string subject = probeName;
        if ( pTarget != nullptr )
            subject += "(" + *pTarget + ( pKey != nullptr ? ", " + *pKey : string() ) + ")";
        else if ( pKey != nullptr )
            subject += "(" + *pKey + ")";
        if ( AutomationProbes::read( *pRegistration, query, outValue ) == false )
        {
            const string reason = outValue._bText && outValue._text.empty() == false ? outValue._text : string( "no target in the scene" );
            outMessage          = subject + " has no value (" + reason + ")";
            return false;
        }
        outMessage = subject;
        return true;
    }

    bool AutomationRunner::compareProbe( const AutomationStep& step, bool& outMatched, string& outMessage ) const
    {
        using Internal = AutomationRunnerInternal;
        AutomationProbeValue value{};
        outMatched = false;
        if ( readProbeValue( step, value, outMessage ) == false )
            return false;
        const string valueText = Internal::makeValueText( value );
        const string subject   = outMessage;
        string       error;
        float32      expected  = 0.0f;
        float32      tolerance = 0.0f;
        bool         bOk       = true;
        string       rule;
        const bool   bNumberRule = step.findAttribute( "is" ) == nullptr && step.findAttribute( "sameAs" ) == nullptr && step.findAttribute( "differsFrom" ) == nullptr;
        if ( bNumberRule && value._bText )
        {
            outMessage = subject + " is text ('" + value._text + "') - compare it with is=\"…\"";
            return false;
        }
        const string* pSameAs      = step.findAttribute( "sameAs" );
        const string* pDiffersFrom = step.findAttribute( "differsFrom" );
        if ( pSameAs != nullptr || pDiffersFrom != nullptr )
        {
            // 기억한 값과는 글 그대로 견준다 — 64 비트 해시 같은 값이 수 비교(float32)에서 뭉개지지 않는다.
            const string& name        = pSameAs != nullptr ? *pSameAs : *pDiffersFrom;
            const string* pRemembered = nullptr;
            for ( const RememberedValue& remembered : _listRemembered )
            {
                if ( remembered._name == name )
                    pRemembered = &remembered._text;
            }
            if ( pRemembered == nullptr )
            {
                outMessage = subject + ": nothing was remembered as '" + name + "' (add <Remember as=\"" + name + "\"> before this step)";
                return false;
            }
            bOk        = pSameAs != nullptr ? valueText == *pRemembered : valueText != *pRemembered;
            outMatched = bOk;
            outMessage = subject + ( pSameAs != nullptr ? " same as " : " differs from " ) + name + " ('" + *pRemembered + "'), got " + valueText;
            return true;
        }
        if ( step.findAttribute( "is" ) != nullptr )
        {
            bOk  = valueText == *step.findAttribute( "is" );
            rule = "is '" + *step.findAttribute( "is" ) + "'";
        }
        else if ( step.findAttribute( "equals" ) != nullptr )
        {
            (void)Internal::readFloat( step, "equals", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value._number == static_cast<float64>( expected );
            rule = "== " + *step.findAttribute( "equals" );
        }
        else if ( step.findAttribute( "near" ) != nullptr )
        {
            (void)Internal::readFloat( step, "near", 0.0f, expected, error );          // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)Internal::readFloat( step, "tolerance", 1.0e-4f, tolerance, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            const float64 lower = static_cast<float64>( expected ) - static_cast<float64>( tolerance );
            const float64 upper = static_cast<float64>( expected ) + static_cast<float64>( tolerance );
            bOk                 = lower <= value._number && value._number <= upper;
            rule                = "near " + *step.findAttribute( "near" );
        }
        else if ( step.findAttribute( "atLeast" ) != nullptr )
        {
            (void)Internal::readFloat( step, "atLeast", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value._number >= static_cast<float64>( expected );
            rule = ">= " + *step.findAttribute( "atLeast" );
        }
        else
        {
            (void)Internal::readFloat( step, "atMost", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value._number <= static_cast<float64>( expected );
            rule = "<= " + *step.findAttribute( "atMost" );
        }
        outMatched = bOk;
        outMessage = subject + " " + rule + ", got " + valueText;
        return true;
    }

    uint32 AutomationRunner::convertSecondsToFrames( float32 seconds ) const
    {
        return static_cast<uint32>( seconds / _scenario.getFixedDelta() + 0.5f );
    }

    void AutomationRunner::addRecordLine( string line )
    {
        SW_LOG_INFO( "[Scenario] record %#", line.c_str() );
        _reportWriter.addRecordLine( std::move( line ) );
    }

    void AutomationRunner::prepareWatches()
    {
        _listWatch.clear();
        for ( const AutomationStep& step : _scenario.getSteps() )
        {
            if ( string_view{ step._kind } != "ExpectVisited" )
                continue;
            const string* pTarget = step.findAttribute( "target" );
            const string* pKey    = step.findAttribute( "key" );
            ProbeWatch    watch{};
            watch._probe  = *step.findAttribute( "probe" );
            watch._target = pTarget != nullptr ? *pTarget : string();
            watch._key    = pKey != nullptr ? *pKey : string();
            bool bKnown   = false;
            for ( const ProbeWatch& existing : _listWatch )
            {
                bKnown = bKnown || ( existing._probe == watch._probe && existing._target == watch._target && existing._key == watch._key );
            }
            if ( bKnown == false )
                _listWatch.push_back( std::move( watch ) );
        }
    }

    void AutomationRunner::sampleWatches()
    {
        for ( ProbeWatch& watch : _listWatch )
        {
            AutomationStep step{};
            step._kind = "ExpectVisited";
            step._listAttribute.push_back( AutomationAttribute{ "probe", watch._probe } );
            if ( watch._target.empty() == false )
                step._listAttribute.push_back( AutomationAttribute{ "target", watch._target } );
            if ( watch._key.empty() == false )
                step._listAttribute.push_back( AutomationAttribute{ "key", watch._key } );
            AutomationProbeValue value{};
            string               message;
            if ( readProbeValue( step, value, message ) == false )
                continue;
            string text = AutomationRunnerInternal::makeValueText( value );
            if ( watch._listValue.empty() == false && watch._listValue.back() == text )
                continue;
            watch._listValue.push_back( std::move( text ) );
            watch._listFrame.push_back( _frameIndex );
        }
    }

    void AutomationRunner::runExpectVisited( const AutomationStep& step )
    {
        const string*     pTarget = step.findAttribute( "target" );
        const string*     pKey    = step.findAttribute( "key" );
        const string_view probe   = *step.findAttribute( "probe" );
        const string_view target  = pTarget != nullptr ? string_view{ *pTarget } : string_view{};
        const string_view key     = pKey != nullptr ? string_view{ *pKey } : string_view{};
        const ProbeWatch* pWatch  = nullptr;
        for ( const ProbeWatch& watch : _listWatch )
        {
            const bool bSame = watch._probe == probe && watch._target == target && watch._key == key;
            if ( bSame )
                pWatch = &watch;
        }
        string error;
        uint32 since = 0;
        (void)AutomationRunnerInternal::readUint( step, "since", 0, since, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        const string*        pIs      = step.findAttribute( "is" );
        const vector<string> listWant = pIs != nullptr ? vector<string>{ *pIs } : AutomationRunnerInternal::splitList( *step.findAttribute( "sequence" ) );
        // `since` 프레임에 이미 들어 있던 값(그 앞의 마지막 바뀜)도 거친 것으로 본다.
        size_t first = 0;
        for ( size_t index = 0; pWatch != nullptr && index < pWatch->_listFrame.size(); ++index )
        {
            if ( pWatch->_listFrame[index] <= since )
                first = index;
        }
        size_t wantIndex = 0;
        string seen;
        for ( size_t index = first; pWatch != nullptr && index < pWatch->_listValue.size(); ++index )
        {
            seen += seen.empty() ? "" : " > ";
            seen += pWatch->_listValue[index] + "@" + to_string( pWatch->_listFrame[index] );
            if ( wantIndex < listWant.size() && pWatch->_listValue[index] == listWant[wantIndex] )
                ++wantIndex;
        }
        if ( wantIndex == listWant.size() )
            return;
        string want;
        for ( const string& value : listWant )
        {
            want += want.empty() ? string( value ) : " > " + value;
        }
        string subject = string( probe );
        if ( target.empty() == false || key.empty() == false )
            subject += "(" + string( target ) + ( target.empty() || key.empty() ? "" : ", " ) + string( key ) + ")";
        recordFailure( step, subject + " never went through '" + want + "' since frame " + to_string( since ) + " (missing '" + listWant[wantIndex] +
                                 "'), saw: " + ( seen.empty() ? string( "no value" ) : seen ) );
    }

    void AutomationRunner::runRecord( const AutomationStep& step )
    {
        AutomationProbeValue value{};
        string               message;
        if ( readProbeValue( step, value, message ) == false )
        {
            recordFailure( step, "Record " + message );
            return;
        }
        addRecordLine( "frame " + to_string( _frameIndex ) + " " + message + " = " + AutomationRunnerInternal::makeValueText( value ) );
    }

    void AutomationRunner::runRemember( const AutomationStep& step )
    {
        AutomationProbeValue value{};
        string               message;
        if ( readProbeValue( step, value, message ) == false )
        {
            recordFailure( step, "Remember " + message );
            return;
        }
        const string& name = *step.findAttribute( "as" );
        string        text = AutomationRunnerInternal::makeValueText( value );
        SW_LOG_INFO( "[Scenario] frame %# remember %# = %# (%#)", _frameIndex, name.c_str(), text.c_str(), message.c_str() );
        for ( RememberedValue& remembered : _listRemembered )
        {
            if ( remembered._name == name )
            {
                remembered._text = std::move( text );
                return;
            }
        }
        _listRemembered.push_back( RememberedValue{ name, std::move( text ) } );
    }

    void AutomationRunner::runExpectLog( const AutomationStep& step )
    {
        const string& needle = *step.findAttribute( "contains" );
        string        error;
        uint32        since = 0;
        (void)AutomationRunnerInternal::readUint( step, "since", 0, since, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        const uint32 matchCount = _logCapture.countMatchingLines( needle, since );
        uint32       expected   = 0;
        if ( step.findAttribute( "count" ) != nullptr )
        {
            (void)AutomationRunnerInternal::readUint( step, "count", 0, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            if ( matchCount != expected )
                recordFailure( step, "ExpectLog '" + needle + "' count == " + to_string( expected ) + ", got " + to_string( matchCount ) );
            return;
        }
        (void)AutomationRunnerInternal::readUint( step, "atLeast", 0, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        if ( matchCount < expected )
            recordFailure( step, "ExpectLog '" + needle + "' count >= " + to_string( expected ) + ", got " + to_string( matchCount ) );
    }

    void AutomationRunner::runExpectImage( const AutomationStep& step )
    {
        using Internal = AutomationRunnerInternal;
        string                    error;
        AutomationImageMetricKind metric = AutomationImageMetricKind::MeanLuma;
        AutomationImageRegion     region{};
        float32                   ratio = Internal::kDefaultDarkRatio;
        (void)AutomationImageMetric::tryParseKind( *step.findAttribute( "metric" ), metric ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        const string* pRegion = step.findAttribute( "region" );
        if ( pRegion != nullptr )
            (void)AutomationImageMetric::tryParseRegion( *pRegion, region );                   // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        (void)Internal::readFloat( step, "ratio", Internal::kDefaultDarkRatio, ratio, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다

        const string    path = resolveOutputPath( *step.findAttribute( "file" ) );
        AutomationImage image;
        AutomationImage reference;
        const string*   pReference = step.findAttribute( "reference" );
        if ( AutomationImageMetric::loadPpm( path, image, error ) == false ||
             ( pReference != nullptr && AutomationImageMetric::loadPpm( resolveOutputPath( *pReference ), reference, error ) == false ) )
        {
            recordFailure( step, "could not read the image: " + error );
            return;
        }
        float64 value = 0.0;
        if ( AutomationImageMetric::measure( image, region, metric, ratio, pReference != nullptr ? &reference : nullptr, value, error ) == false )
        {
            recordFailure( step, error );
            return;
        }
        // 값은 항상 적는다. 임계값을 정할 때 숫자를 보기 위해서다.
        string line = *step.findAttribute( "metric" ) + "(" + ( pRegion != nullptr ? *pRegion : string( "0,0,1,1" ) ) + ") " + *step.findAttribute( "file" ) + " = " +
                      Internal::formatNumber( value );
        SW_LOG_INFO( "[Scenario] metric %#", line.c_str() );
        _reportWriter.addMetricLine( std::move( line ) );

        float32 expected  = 0.0f;
        float32 tolerance = 0.0f;
        bool    bOk       = true;
        string  rule;
        if ( step.findAttribute( "equals" ) != nullptr )
        {
            (void)Internal::readFloat( step, "equals", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value == static_cast<float64>( expected );
            rule = "== " + *step.findAttribute( "equals" );
        }
        else if ( step.findAttribute( "near" ) != nullptr )
        {
            (void)Internal::readFloat( step, "near", 0.0f, expected, error );          // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)Internal::readFloat( step, "tolerance", 1.0e-3f, tolerance, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = MathUtil::abs( value - static_cast<float64>( expected ) ) <= static_cast<float64>( tolerance );
            rule = "near " + *step.findAttribute( "near" );
        }
        else if ( step.findAttribute( "atLeast" ) != nullptr )
        {
            (void)Internal::readFloat( step, "atLeast", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value >= static_cast<float64>( expected );
            rule = ">= " + *step.findAttribute( "atLeast" );
        }
        else
        {
            (void)Internal::readFloat( step, "atMost", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value <= static_cast<float64>( expected );
            rule = "<= " + *step.findAttribute( "atMost" );
        }
        if ( bOk == false )
            recordFailure( step, "ExpectImage " + *step.findAttribute( "metric" ) + " " + rule + ", got " + Internal::formatNumber( value ) );
    }

    string AutomationRunner::resolveOutputPath( string_view file ) const
    {
        const bool bAbsolute = file.empty() == false && ( file[0] == '/' || file[0] == '\\' || ( file.size() > 1 && file[1] == ':' ) );
        return bAbsolute ? string( file ) : _outputDirectory + "/" + string( file );
    }

    bool AutomationRunner::takePendingScreenshotPath( string& outPath )
    {
        if ( _listPendingScreenshot.empty() )
            return false;
        outPath = _listPendingScreenshot.front();
        _listPendingScreenshot.erase( _listPendingScreenshot.begin() );
        return true;
    }

    void AutomationRunner::applyLogGate()
    {
        vector<string> listUnexpected = _logCapture.collectUnexpectedLines( _logPolicy );
        if ( engine::areEngineServicesBound() )
        {
            for ( const string& key : engine::getSubsystem<LocalizationManager>().getMissingKeys() )
            {
                const string line         = "Missing localized text for key '" + key + "'";
                const bool   bAlreadySeen = _logCapture.containsLine( line ); // 시나리오 구간에 남은 경고 줄은 위에서 이미 셌다
                if ( bAlreadySeen == false && _logPolicy.isAllowed( line ) == false )
                    listUnexpected.push_back( "[Warning] [LocalizationManager] " + line + " (before the scenario started)" );
            }
        }
        if ( listUnexpected.empty() )
            return;
        for ( string& line : listUnexpected )
        {
            _listFailure.push_back( "unexpected log: " + std::move( line ) );
        }
        _result       = AutomationResult::Failed;
        _finishReason = "unexpected Warning/Error log line(s) - fix them, declare them with <ExpectLog>, or add them with a reason to " +
                        string( AutomationLogPolicy::kAllowListPath );
    }

    void AutomationRunner::endRun( InputManager* pInput )
    {
        if ( _bEnded == SW_TRUE )
            return;
        _bEnded = SW_TRUE;
        if ( pInput != nullptr && pInput->getVirtualInput() == &_inputScript )
            pInput->detachVirtualInput();
        _logCapture.stop();
        if ( _result == AutomationResult::Passed )
            applyLogGate();
        if ( _result == AutomationResult::Passed )
        {
            SW_LOG_INFO( "[Scenario] PASS %# (%# frame(s))", _scenario.getName().c_str(), _frameIndex );
        }
        else if ( _result == AutomationResult::Skipped )
        {
            SW_LOG_WARNING( "[Scenario] SKIP %#: %#", _scenario.getName().c_str(), _finishReason.c_str() );
        }
        else
        {
            string detail = _finishReason;
            for ( const string& failure : _listFailure )
            {
                detail += "\n    ";
                detail += failure;
            }
            SW_LOG_ERROR( "[Scenario] %# %# at frame %#: %#", getResultName( _result ), _scenario.getName().c_str(), _frameIndex, detail.c_str() );
        }
        AutomationReportParams report{};
        report._scenarioName = _scenario.getName();
        report._pResultName  = getResultName( _result );
        report._exitCode     = static_cast<int32>( _result );
        report._frameCount   = _frameIndex;
        report._reason       = _finishReason;
        report._pListFailure = &_listFailure;
        _reportWriter.write( report );
    }
} // namespace sw
