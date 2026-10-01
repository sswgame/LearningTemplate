#include "pch.h"

#include "Core/Common/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Time/CpuTimer.h"

#include "ReflectionParser/AnnotationFields.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserOptions.h"
#include "ReflectionParser/ParserSession.h"
#include "ReflectionParser/ReflectBuiltinsLoader.h"
#include "ReflectionParser/ReflectionPipeline.h"

SW_LOG_CALLER( "ReflectionParser" );
namespace sw
{
    namespace
    {
        /**
         * @brief main 이 어디서 빠져나가든 로거를 내립니다.
         * @details 예전에는 `logger->shutdown(); return 1;` 을 조기 반환마다 손으로 적었습니다(11곳). 로거는 비동기라
         *          내리지 않으면 마지막 메시지를 잃는데, 실패 경로일수록 그 메시지가 필요합니다. 새 조기 반환을 넣는 사람이
         *          잊을 수 있는 구조였습니다.
         */
        class LoggerScope
        {
        public:
            LoggerScope()
                : _logger{ sw::make_unique<sw::Logger>() }
            {
                _logger->initialize();
            }
            ~LoggerScope() { _logger->shutdown(); }

            LoggerScope( const LoggerScope& )            = delete;
            LoggerScope& operator=( const LoggerScope& ) = delete;

        private:
            sw::unique_ptr<sw::Logger> _logger;
        };

        struct ReflectionParserInternal
        {
            /** @brief 템플릿 디렉터리를 읽습니다. 두 모드 모두 필요합니다. */
            static bool loadTemplates( const ParserOptions& options, ParserSession& session )
            {
                if ( session._emitTemplateStore.loadDirectory( options._emitTemplatesDir, session._config._emitTemplateExtension ) )
                    return true;
                SW_LOG_ERROR( "Failed to load %#: %#", cliConstants::kEmitTemplates, options._emitTemplatesDir );
                return false;
            }

            /**
             * @brief 파싱에 쓰는 표들을 채웁니다: builtins(선택) · 철자 표(필수, 필드 표와 대조).
             * @details 철자 표와 필드 표가 어긋나면 그 토큰은 애노테이션을 적는 자리에서 보이지 않게 사라진다. 시작할 때 막는다.
             */
            static bool loadTables( const ParserOptions& options, ParserSession& session )
            {
                session._typeNameMap.setStripPrefixes( session._config._listTypeStripPrefix );
                if ( options._builtinsPath.empty() )
                {
                    SW_LOG_WARNING( "No %#; scalar aliases / std containers will not be registered.", cliConstants::kBuiltins );
                }
                else if ( loadReflectBuiltins( options._builtinsPath, session ) == false )
                {
                    SW_LOG_ERROR( "Failed to load %#: %#", cliConstants::kBuiltins, options._builtinsPath );
                    return false;
                }

                if ( session._annotationMeta.loadFile( options._annotationMetaPath ) == false )
                {
                    SW_LOG_ERROR( "Failed to load %#: %#", cliConstants::kAnnotationMeta, options._annotationMetaPath );
                    return false;
                }
                if ( AnnotationFields::validateBindings( session._annotationMeta ) == false )
                {
                    SW_LOG_ERROR( "%# and PredefinedAnnotationField.xxx disagree (see above).", options._annotationMetaPath );
                    return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief 설정 · 표 · 템플릿을 읽고 입력을 처리합니다.
 *
 * 단계:
 *  1) CLI (`ParserOptions` — 플래그는 표 한 줄씩)
 *  2) 설정(`ParserConfig`) · 템플릿
 *  3) builtins-gen 전용 모드면 ReflectBuiltins.gen.cpp 를 쓰고 끝
 *  4) builtins · 철자 표(필드 표와 대조)
 *  5) `ReflectionPipeline` — 증분 판정 → 키워드 거르기 → 파싱 · 수집 → 코드젠 → FlagOps 우산
 */
int32 main( int32 argc, utf8* argv[] )
{
    const sw::LoggerScope loggerScope;

    sw::ParserOptions options;
    if ( options.parse( argc, argv ) == false )
    {
        sw::ParserOptions::printUsage();
        return 1;
    }
    if ( options._bHelp )
    {
        sw::ParserOptions::printUsage();
        return 0;
    }

    // 파서 한 번 실행이 쓰는 설정과 표들이다. 여기가 소유자고, 아래로는 읽기만 하게 내려 준다.
    sw::ParserSession session;
    const bool        bConfigLoaded = session._config.load();
    if ( sw::ReflectionParserInternal::loadTemplates( options, session ) == false )
        return 1;

    // ReflectBuiltins.gen.cpp 전용 모드 — clang 을 부르지 않으므로 설정은 기본값이어도 된다.
    if ( options.isBuiltinsGenMode() )
        return sw::emitReflectBuiltinsGen( options._builtinsPath, options._emitBuiltinsGenPath, session ) ? 0 : 1;

    if ( bConfigLoaded == false || sw::ReflectionParserInternal::loadTables( options, session ) == false )
        return 1;

    sw::CpuTimer timer;
    timer.resetTimer();
    timer.startTimer();

    sw::ReflectionPipeline pipeline( options, session );
    const int32            errorCount = pipeline.run();

    timer.updateTimer();
    [[maybe_unused]] const float32 elapsedMs = timer.getDeltaTime() * 1000.0f;
    if ( errorCount == 0 )
        SW_LOG_INFO( "Done in %# ms. All files processed successfully.", elapsedMs );
    else
        SW_LOG_ERROR( "Done in %# ms with %# error(s).", elapsedMs, errorCount );
    return errorCount == 0 ? 0 : 1;
}
