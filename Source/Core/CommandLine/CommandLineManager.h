/**
 * @file CommandLineManager.h
 * @brief 실행할 때 받은 커맨드라인 인자를 파싱하고 보관하는 매니저입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/ServiceSlot.h"

#include <variant>

namespace sw
{
    /**
     * @struct CommandLineDiskPath
     * @brief 디스크 경로 인자의 저장 형식입니다. `ArgumentList.xxx` 에서 기본값을 `CommandLineDiskPath{}` 로 적은 줄이 이 형식이다.
     * @details `parse` 가 받을 때 한 번 절대화한다 - 상대 경로는 기동 때 넘긴 기준 폴더(그 셸의 작업 폴더) 기준이다(R5). 읽는 쪽은 `AbsolutePath` 로 꺼낸다(글로 꺼내도 늘 절대 경로다). 예전에는 받은 글을 그대로 두어, 읽는 쪽마다 실행 중 작업 폴더로 다시 풀었다(작업 폴더를 바꾸면 산출물 위치가 바뀌었다).
     */
    struct CommandLineDiskPath
    {
        AbsolutePath _path;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) CommandLineArgument — ArgumentList.xxx 에서 만드는 표준 키
    // ------------------------------------------------------------------------------
    /**
     * @enum CommandLineArgumentUsage
     * @brief 인자를 쓰는 구성입니다 — `ArgumentList.xxx` 줄마다의 둘째 칸입니다.
     * @details 받지 않는 구성은 철자를 등록하지 않는다(`CommandLineManager::initialize`) — 주면 "알 수 없는 인자" 로 기동이 멈추고, 열거값은 남아 읽으면 "주지 않음" 이다.
     *          언리얼은 배포 구성에서 개발 명령줄 · 콘솔 변수를 `#if !UE_BUILD_SHIPPING` 으로 빼고, 유니티는 Development Build 에서만 듣는 인자를 둔다 — 여기는 빼는 대신 거절한다.
     */
    enum class CommandLineArgumentUsage : uint8
    {
        All,       /**< 모든 구성(배포 포함) */
        Dev,       /**< 개발 도구 · 진단(에디터 · 임포트 · 개별 쿠킹 · 검증 층) — 배포가 아닌 구성만 */
        Test,      /**< 단위 테스트 · 자동화 시나리오 장치 — 배포가 아닌 구성만 */
        Server,    /**< 서버 코드가 있는 타깃(Game · Server)만 — 클라이언트 전용 타깃은 받지 않는다 */
        AndroidDev /**< Android 의 배포가 아닌 구성만 */
    };

    /**
     * @struct CommandLineBuildPolicy
     * @brief 이 빌드가 어느 쓰는 구성(`CommandLineArgumentUsage`)의 인자를 받는지 정하는 빌드 사실입니다.
     * @details 값은 `current`(빌드 상수 `build::kShipping` · `kWithServerCode` · `kPlatformName`)가 정한다. 단위 테스트가 배포 정책을 개발 빌드에서 재현하려고 값으로 둔다.
     */
    struct CommandLineBuildPolicy
    {
        uint8                  _bShipping       : 1;
        uint8                  _bWithServerCode : 1;
        uint8                  _bAndroid        : 1;
        [[maybe_unused]] uint8 _reserved        : 5;

        /** @brief 이 빌드의 정책입니다. */
        [[nodiscard]] static CommandLineBuildPolicy current();

        /** @brief @p usage 의 인자를 이 빌드가 받으면 true 입니다. */
        [[nodiscard]] constexpr bool isUsageAccepted( const CommandLineArgumentUsage usage ) const
        {
            switch ( usage )
            {
                case CommandLineArgumentUsage::All:
                    return true;
                case CommandLineArgumentUsage::Dev:
                case CommandLineArgumentUsage::Test:
                    return _bShipping == 0;
                case CommandLineArgumentUsage::Server:
                    return _bWithServerCode != 0;
                case CommandLineArgumentUsage::AndroidDev:
                    return _bShipping == 0 && _bAndroid != 0;
            }
            return false;
        }
    };

    /**
     * @enum CommandLineArgument
     * @brief 엔진이 정해 둔 표준 커맨드라인 인자의 열거형 식별자입니다.
     */
    enum class CommandLineArgument : uint8
    {
/** @brief ArgumentList.xxx 한 줄을 열거 멤버로 펼칩니다. */
#define SW_REGISTER_ARGUMENT( name, ... ) name,
#include "Core/Predefined/ArgumentList.xxx"

#undef SW_REGISTER_ARGUMENT
        Count /**< 등록된 인자의 총 개수 */
    };

    /**
     * @brief 표준 인자의 명령줄 철자(`ArgumentList.xxx` 줄의 첫 철자, 앞의 `-` 없이)입니다 — 열거값 순서의 표입니다.
     * @details 인자 글을 코드에서 만들어야 하는 쪽(테스트가 App 을 띄울 때)이 철자를 다시 적지 않게 한다. 정본은 `ArgumentList.xxx` 한 곳이다.
     */
    inline constexpr const utf8* kArrCommandLineArgumentSpelling[] = {
/** @brief 줄의 철자 목록(`...`)에서 첫 철자만 고릅니다. 철자가 하나뿐인 줄에서도 가변 인자가 비지 않게 뒤에 nullptr 를 붙여 부릅니다. */
#define SW_ARGUMENT_FIRST_SPELLING( first, ... )              first
#define SW_REGISTER_ARGUMENT( name, usage, valueSample, ... ) SW_ARGUMENT_FIRST_SPELLING( __VA_ARGS__, nullptr ),
#include "Core/Predefined/ArgumentList.xxx"

#undef SW_REGISTER_ARGUMENT
#undef SW_ARGUMENT_FIRST_SPELLING
    };

    /** @brief 표준 인자의 쓰는 구성(`ArgumentList.xxx` 줄의 둘째 칸)입니다 — 열거값 순서의 표입니다. */
    inline constexpr CommandLineArgumentUsage kArrCommandLineArgumentUsage[] = {
#define SW_REGISTER_ARGUMENT( name, usage, ... ) CommandLineArgumentUsage::usage,
#include "Core/Predefined/ArgumentList.xxx"

#undef SW_REGISTER_ARGUMENT
    };

    /** @brief 표준 인자의 쓰는 구성입니다. */
    constexpr CommandLineArgumentUsage getCommandLineArgumentUsage( CommandLineArgument argument )
    {
        return kArrCommandLineArgumentUsage[static_cast<size_t>( argument )];
    }

    /** @brief 표준 인자의 첫 명령줄 철자(`"scenario"`)입니다. 앞의 `-` 는 붙어 있지 않다. */
    constexpr const utf8* getCommandLineArgumentSpelling( CommandLineArgument argument )
    {
        return kArrCommandLineArgumentSpelling[static_cast<size_t>( argument )];
    }

    // ------------------------------------------------------------------------------
    // 2) CommandLineManager — initialize(등록) → parse → getArgument
    // ------------------------------------------------------------------------------
    /**
     * @class CommandLineManager
     * @brief 커맨드라인 인자를 파싱해 타입별(variant) 값으로 보관하는 매니저입니다.
     */
    class SW_API CommandLineManager final
    {
        /** @brief 인자 하나의 현재 값 · 저장 타입 견본 · 파싱 여부입니다. */
        struct ArgumentInfo
        {
            using Value = std::variant<int32, bool, float32, string, CommandLineDiskPath>;

            Value                  _value;
            Value                  _valueSample; ///< 저장 타입을 정하는 견본(표의 `valueSample`). 값은 읽지 않는다
            uint8                  _bParsed  : 1;
            [[maybe_unused]] uint8 _reserved : 7;

            /**
             * @brief 값을 비우고 플래그를 끕니다.
             * @details 헤더에 둡니다. `addArgument` 가 템플릿이라 호출하는 쪽 TU 에서 이 생성자를 찾기 때문입니다. .cpp 에 두면
             *          Engine.dll 에서 내보내야만 링크됩니다.
             */
            ArgumentInfo()
                : _value{}
                , _valueSample{}
                , _bParsed{ SW_FALSE }
                , _reserved{ 0 } {}

            /**
             * @brief `-key` 처럼 값 없이 적을 수 있는 인자인지 확인합니다.
             * @details 그럴 수 있는 것은 bool 뿐입니다. 값 없는 `-dx12` 는 "true" 라는 뜻이 되지만, 값 없는 `-W` 나
             *          `-gv_stressTestMeshes` 에는 그런 뜻이 없습니다. 판단은 타입이 합니다(별도 칸을 두지 않습니다) — 값을 빠뜨린
             *          `-gv_stressTestMeshes` 를 받아 주면 int32 항목에 bool 이 들어가고, `readValue` 의 `get_if<int32>` 가 nullptr 을
             *          반환해 경고 한 줄 없이 아무 일도 일어나지 않습니다.
             */
            bool isFlagArgument() const { return std::holds_alternative<bool>( _valueSample ); }
        };

    public:
        /** @brief 빈 인자 맵으로 둡니다. */
        CommandLineManager() = default;
        /** @brief 인자 맵을 해제합니다. */
        ~CommandLineManager() = default;
        /** @brief 복사를 금지합니다. */
        CommandLineManager( const CommandLineManager& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        CommandLineManager& operator=( const CommandLineManager& ) = delete;
        /** @brief 이동 생성을 금지합니다. */
        CommandLineManager( CommandLineManager&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        CommandLineManager& operator=( CommandLineManager&& ) = delete;

        /**
         * @brief ArgumentList.xxx 를 읽어 허용하는 커맨드라인 인자를 등록합니다.
         * @param policy 받는 쓰는 구성 — @p policy 가 받지 않는 줄은 자리(열거값 인덱스)만 두고 철자를 등록하지 않는다(주면 `parse` 가 모르는 인자로 거절한다).
         *               기본은 이 빌드의 정책이고, 테스트만 다른 정책(배포)을 넘긴다.
         * @details 등록 순서는 그 파일의 줄 순서이고, `CommandLineArgument` 열거 멤버도 같은 파일에서 같은 순서로 만들어집니다.
         *          그래서 열거값이 곧 `_listArgument` 인덱스입니다. 그 일치를 줄마다 assert 하므로, 열거형으로 조회할 때는 이름을
         *          만들지도 해시하지도 않습니다. 표를 먼저 비우므로 몇 번을 불러도 같은 표가 됩니다. 다만 그 전에
         *          `addArgument` 로 넣어 둔 커스텀 인자는 함께 사라집니다.
         */
        void initialize( const CommandLineBuildPolicy& policy = CommandLineBuildPolicy::current() );
        /** @brief 파싱 결과는 프로세스가 끝날 때까지 쓰므로 할 일이 없습니다. */
        void shutdown() {}

        /**
         * @brief main 함수가 받은 UTF-8 인자 목록을 파싱합니다(Linux 또는 표준 C++).
         * @param argc 인자 개수
         * @param pPpArgv 문자열 포인터 배열(UTF-8)
         * @param baseDirectory 디스크 경로 인자(`CommandLineDiskPath`)의 상대 경로 기준 - 기동 때 한 번 정한 폴더다.
         * @details 틀린 인자(모르는 이름 · 빠진 값 · 정수 아닌 정수 · 빈 경로)는 값을 추측해 넣지 않고 `getParseErrors` 에 모은다 - 호스트가 기동을 멈춘다.
         */
        void parse( int32 argc, utf8* pPpArgv[], const AbsolutePath& baseDirectory );

        /** @brief main 함수가 받은 UTF-16 인자 목록을 파싱합니다(Windows). 나머지는 UTF-8 판과 같습니다. */
        void parse( int32 argc, utf16* pPpArgv[], const AbsolutePath& baseDirectory );

        /**
         * @brief 엔진이 서기 전(명령줄 표가 없을 때) 플래그 인자 @p argument 가 켜져 적혔는지 봅니다 - `parse` 와 같은 규칙(앞 하이픈 떼기 · `=값` · 철자는 `ArgumentList.xxx`)이다.
         *        이 빌드가 받지 않는 인자(`CommandLineArgumentUsage`)는 적혀도 false 다 — `parse` 가 그 줄을 모르는 인자로 거절한다.
         * @details 서비스 호스트처럼 기동 전에 갈라야 하는 쪽이 글자를 따로 비교하지 않게 한다(`"--service"` 한 철자만 받던 결함).
         */
        [[nodiscard]] static bool isFlagGivenBeforeParse( int32 argc, utf8* pPpArgv[], CommandLineArgument argument );
        /**
         * @brief 엔진이 서기 전 값 인자 @p argument 의 값(`-key=값`)을 찾습니다 — `isFlagGivenBeforeParse` 와 같은 철자 규칙이다.
         * @details 엔진보다 앞에서 갈라야 하는 플랫폼 진입점(`AndroidMain` 의 `-fileserve` — 첫 팩을 읽기 전에 콘텐츠 소스를 건다)이 쓴다.
         * @return 적혔으면 true 이고 @p outValue 가 값이다(값 없이 적었으면 빈 글 — 그 판단은 부른 쪽이 한다. `parse` 도 같은 줄을 오류로 모은다).
         */
        [[nodiscard]] static bool findValueBeforeParse( int32 argc, utf8* pPpArgv[], CommandLineArgument argument, string& outValue );

        /** @brief `parse` 가 거절한 인자의 이유(한 줄씩)입니다. 비어 있지 않으면 기동을 멈춘다(`CommandLineSubsystem`). */
        [[nodiscard]] const vector<string>& getParseErrors() const { return _listParseError; }

        /**
         * @brief 문자열 키로 파싱된 인자 값을 찾습니다.
         * @tparam T 가져올 값의 타입(bool, 정수, 부동소수, sw::string, string_view)
         * @param key 등록된 명령줄 철자(예: "W", "lang"). 앞의 하이픈은 파싱할 때 제거하므로 **조회 키에는 붙이지
         *            않습니다.** `"--W"` 로는 찾지 못합니다. 열거자 이름("WIDTH")은 키가 아닙니다.
         * @param outValue 찾은 값을 받을 변수
         * @return 키가 있고 타입 변환에 성공하면 true
         */
        template <typename T>
        bool getArgument( string_view key, T& outValue ) const;

        /**
         * @brief 미리 정의된 열거값으로 파싱된 인자 값을 빠르게 찾습니다.
         * @tparam T 가져올 값의 타입
         * @param argument 찾을 표준 인자의 열거값
         * @param outValue 찾은 값을 받을 변수
         * @return 키가 있고 타입 변환에 성공하면 true
         */
        template <typename T>
        bool getArgument( CommandLineArgument argument, T& outValue ) const;

        /**
         * @brief 그 인자가 커맨드라인에 실제로 적혔는지 반환합니다.
         * @details `getArgument` 로는 알 수 없습니다. 기본값이 있는 인자는 적지 않아도 true 를 반환하기 때문입니다. "설정 파일
         *          기본값보다 커맨드라인이 우선" 같은 판단에는 이 함수가 필요합니다(안 그러면 `EngineConfig` 의 백엔드 기본값이
         *          `-gv_rhiBackend` 를 조용히 덮어씁니다).
         */
        bool isArgumentProvided( string_view key ) const;

        /** @brief 표준 인자가 커맨드라인에 실제로 적혔는지 반환합니다. */
        bool isArgumentProvided( CommandLineArgument argument ) const;

        /**
         * @brief 자동화 실행(`-unattended` 또는 `-scenario`)이면 true 입니다 — 사람의 사용자 설정 · 세이브 · 상태 폴더를 읽지도 쓰지도 않는 실행의 판정 하나입니다.
         * @details 사용자 설정의 무인 자리(`UserSettingsManager`) · 사용자 데이터 뿌리 격리(`AutomationUserDataIsolation`) · 어서트 대화상자가 같은 판정을 쓴다.
         */
        bool isAutomatedRun() const;

        /**
         * @brief 받는 인자의 목록(`-help` 가 찍는 글)을 만듭니다 — 줄마다 표(ArgumentList.xxx)의 철자와 값 모양(`-key` · `-key=<int>` …)입니다.
         * @details 목록은 표에서 만든다(손으로 쓴 도움말이 표와 어긋나지 않는다).
         */
        string makeUsageText() const;

        /**
         * @brief 인자를 추가합니다.
         * @tparam T 저장 타입. 값을 반드시 적어야 하는지도 이것으로 정해집니다. `bool` 이면 `-key` 만 적을 수 있고(그때
         *         true), 나머지는 `-key=value` 를 요구합니다(`ArgumentInfo::isFlagArgument`).
         * @param listAlias 이 인자를 부르는 이름들. 하나라도 이미 쓰이고 있으면 아무것도 넣지 않습니다.
         * @param valueSample 저장 타입을 정하는 견본 값(값 자체는 쓰이지 않는다 — 주지 않은 인자는 `getArgument` 가 false)
         */
        template <typename T>
        void addArgument( const std::initializer_list<string_view>& listAlias, T valueSample );
        /** @brief `initialize` 가 줄마다 부릅니다 — 다음 등록이 열거값 @p expected 자리인지 단언합니다(단언을 줄마다 펼치지 않으려고 함수로 둔다). */
        void assertRegistrationOrder( CommandLineArgument expected, const utf8* pName ) const;

        /**
         * @brief 아직 등록된 인자가 없어 보류해 둔 `gv_` 값을 찾습니다.
         * @details 커맨드라인은 모듈(EditorModule · SWGame)이 로드되기 전에 파싱됩니다. 그래서 모듈이 선언하는 전역 변수의
         *          값은 파싱 시점에 갈 곳이 없습니다. 버리는 대신 여기에 남겨 두고, 모듈이 나중에 등록할 때
         *          `GlobalVariableManager` 가 꺼내 씁니다.
         */
        [[nodiscard]] bool findPendingGlobalValue( string_view name, string& outValue ) const;

        /**
         * @brief 그 전역 변수를 명령줄이 실제로 줬는지 반환합니다.
         * @details 등록된 변수(Engine 전역 변수)와 보류된 변수(모듈 것)를 둘 다 묻는다. `findPendingGlobalValue` 는 보류표만 보므로
         *          Engine 변수를 놓친다 — "명령줄 값이 저장된 값을 이긴다" 같은 판단은 이 함수로 한다.
         */
        bool isGlobalVariableProvided( string_view name ) const;
        /**
         * @brief 자동화 단계의 `<Launch>` 변수 이름을 그 단계의 명령줄로 둡니다 — `isGlobalVariableProvided` 가 참이 되어, 게임을 다시 세울 때의 사용자 설정 적용이 그 값을 덮지 않는다.
         * @details 체인의 둘째 단계부터는 `<Launch>` 를 실행 중에 걸므로 명령줄 표에 없다. 표시하지 않으면 설정에 묶인 변수(`gv_displayOutput` — display.hdr)가 재시작에서 설정 기본값으로 돌아간다
         *          (언리얼 콘솔 변수의 SetByCommandline 이 SetByGameSetting 을 이기는 것과 같은 순서). 단계마다 통째로 바꾼다 — 빈 목록이면 지운다.
         */
        void setStageGlobalVariables( vector<string> listName ) { _listStageGlobal = std::move( listName ); }

        /** @brief 보류 중인 `gv_` 키 이름을 모두 반환합니다(오타 진단용). */
        vector<string> collectPendingGlobalNames() const;

        /** @brief `parse` 가 받은 원본 인자(프로그램 이름 제외, UTF-8, 적은 순서 그대로)입니다. 재현 명령처럼 해석 전 글이 필요한 쪽이 읽습니다. */
        const vector<string>& getOriginalArguments() const { return _listOriginalArgument; }

        /**
         * @brief 인자 한 줄(`-key=value` · `--key` · `key=value`)의 이름입니다 — 앞뒤 공백, `=` 뒤, 앞의 `-` 를 뗀 것(`key`).
         * @details `parse` 가 이름을 얻는 규칙 그대로입니다(`parse` 도 이것을 부른다). 원본 인자(`getOriginalArguments`)에서 이름을 가려내는 쪽(자식 프로세스에 넘길 인자 거르기,
         *          재현 명령)이 `find( '=' )` · 하이픈 떼기를 따로 적으면 공백 · 빈 키 처리가 파서와 어긋난다. 등록된 이름인지는 보지 않습니다.
         */
        [[nodiscard]] static string_view getArgumentName( string_view argumentLine );
        /** @brief 엔진이 서기 전 @p argument 가 적힌 첫 줄(앞뒤 공백을 뗀 글)입니다. 없으면 빈 값이다 — `isFlagGivenBeforeParse` · `findValueBeforeParse` 의 찾기 하나. */
        [[nodiscard]] static optional<string_view> findLineBeforeParse( int32 argc, utf8* pPpArgv[], CommandLineArgument argument );

    private:
        /** @brief 보류표에 담을 키의 접두어입니다. 이것으로 시작하는 미등록 키만 남깁니다. */
        static constexpr auto kGlobalVariablePrefix = "gv_";
        /** @brief 인자 한 줄(예: "--width=1280", "-fullscreen")을 파싱해 표에 반영합니다. 거절하면 `_listParseError` 에 이유를 더합니다. */
        void parseArgumentLine( string_view argumentLine, const AbsolutePath& baseDirectory );

        /** @brief 등록된 명령줄 철자로 인자를 찾습니다. 없으면 nullptr 입니다. */
        const ArgumentInfo* findArgument( string_view key ) const;

        /** @brief 열거값을 `_listArgument` 인덱스로 바로 씁니다(initialize 가 그 일치를 보장합니다). */
        const ArgumentInfo* findArgument( CommandLineArgument argument ) const;

        /** @brief 인자 값을 등록된 기본값의 타입에 맞춰 변환해 넣습니다. 변환하지 못하면 `ErrorKind::InvalidArgument`(대상은 받은 값)이고 값을 바꾸지 않는다. */
        [[nodiscard]] static expected<void> trySetValue( ArgumentInfo& argument, string_view newValue, const AbsolutePath& baseDirectory );

        /** @brief 도움말 한 줄(철자들과 값 모양)을 @p inoutText 에 붙입니다. */
        static void appendUsageLine( string& inoutText, const std::initializer_list<string_view>& listAlias, const ArgumentInfo& argument );

        /**
         * @brief 파싱된 값(없으면 기본값)을 요청한 타입으로 꺼냅니다.
         * @details 저장 타입은 넷(bool · int32 · float32 · string)뿐이므로, 요청 타입 T 를 그중 하나로 대응시킨 뒤 variant 에서
         *          한 번만 꺼냅니다.
         */
        template <typename T>
        [[nodiscard]] static bool readValue( const ArgumentInfo& argument, T& outValue );

    private:
        vector<ArgumentInfo> _listArgument;
        /** @brief 명령줄 철자 → `_listArgument` 인덱스입니다. 기본 해시가 transparent 라 string_view 로 할당 없이 조회됩니다. */
        unordered_map<string, uint32> _mapArgument;
        /** @brief 등록된 인자가 없어 보류해 둔 `gv_` 키 → 값입니다. 모듈이 나중에 선언할 때 꺼내 씁니다. */
        unordered_map<string, string> _mapPendingGlobal;
        /** @brief `parse` 가 받은 원본 인자입니다(`getOriginalArguments`). `initialize` 가 비웁니다. */
        vector<string> _listOriginalArgument;
        /** @brief `parse` 가 거절한 인자의 이유입니다(`getParseErrors`). `initialize` 가 비웁니다. */
        vector<string> _listParseError;
        /** @brief 지금 자동화 단계의 `<Launch>` 변수 이름입니다(`setStageGlobalVariables`). */
        vector<string> _listStageGlobal;
        /** @brief `initialize` 가 받은 정책입니다 — 도움말(`makeUsageText`)이 받지 않는 줄을 뺀다. */
        CommandLineBuildPolicy _buildPolicy{};

        SW_SERVICE_SLOT( CommandLineManager, Required );
    };

    template <typename T>
    bool CommandLineManager::readValue( const ArgumentInfo& argument, T& outValue )
    {
        static_assert( std::is_same_v<T, bool> || std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_same_v<T, string> || std::is_same_v<T, string_view> || std::is_same_v<T, AbsolutePath>,
                       "커맨드라인 값은 bool·정수·부동소수·string·string_view·AbsolutePath(디스크 경로 인자) 로만 꺼낼 수 있습니다" );

        const ArgumentInfo::Value* pTargetValue{ nullptr };
        if ( argument._bParsed != SW_FALSE )
            pTargetValue = &argument._value;
        else
            return false;

        // 디스크 경로 인자만 형식으로 꺼낸다 — 글 인자는 절대 경로라고 말할 수 없다.
        if constexpr ( std::is_same_v<T, AbsolutePath> )
        {
            const CommandLineDiskPath* pDiskPath = std::get_if<CommandLineDiskPath>( pTargetValue );
            if ( pDiskPath == nullptr )
                return false;
            outValue = pDiskPath->_path;
            return true;
        }
        else
        {
            // 요청 타입 → 저장 타입. bool 을 먼저 걸러야 한다(bool 도 정수 타입이다).
            using StoredType = std::conditional_t<std::is_same_v<T, bool>, bool,
                                                  std::conditional_t<std::is_integral_v<T>, int32,
                                                                     std::conditional_t<std::is_floating_point_v<T>, float32, string>>>;

            // 경로 인자는 절대 경로 글로 꺼낸다(저장 형식만 다르다).
            if constexpr ( std::is_same_v<StoredType, string> )
            {
                const CommandLineDiskPath* pDiskPath = std::get_if<CommandLineDiskPath>( pTargetValue );
                if ( pDiskPath != nullptr )
                {
                    outValue = T{ pDiskPath->_path.str() };
                    return true;
                }
            }

            const StoredType* pStoredValue = std::get_if<StoredType>( pTargetValue );
            if ( pStoredValue == nullptr )
                return false;

            if constexpr ( std::is_same_v<T, string> || std::is_same_v<T, string_view> )
                outValue = T{ *pStoredValue };
            else
                outValue = static_cast<T>( *pStoredValue );

            return true;
        }
    }

    template <typename T>
    bool CommandLineManager::getArgument( string_view key, T& outValue ) const
    {
        const ArgumentInfo* pArgument = findArgument( key );
        if ( pArgument == nullptr )
            return false;

        return readValue( *pArgument, outValue );
    }

    template <typename T>
    bool CommandLineManager::getArgument( const CommandLineArgument argument, T& outValue ) const
    {
        const ArgumentInfo* pArgument = findArgument( argument );
        if ( pArgument == nullptr )
            return false;

        return readValue( *pArgument, outValue );
    }

    template <typename T>
    void CommandLineManager::addArgument( const std::initializer_list<string_view>& listAlias, T valueSample )
    {
        // 먼저 모두 검사한다. 겹치는 이름을 만난 즉시 돌아가면 그 앞에서 이미 넣은 별칭들이 끝내 만들어지지
        // 않는 인덱스를 가리킨 채 남고, 그 별칭으로 조회하면 _listArgument 범위 밖을 읽는다.
        for ( string_view alias : listAlias )
        {
            if ( _mapArgument.find( alias ) != _mapArgument.end() )
            {
                SW_LOG_ASSERT( false, "%# is already in use", alias );
                return;
            }
        }

        ArgumentInfo argument{};
        argument._valueSample = std::move( valueSample );

        const uint32 newArgumentIndex = static_cast<uint32>( _listArgument.size() );
        _listArgument.push_back( std::move( argument ) );
        for ( string_view alias : listAlias )
        {
            _mapArgument.emplace( string{ alias }, newArgumentIndex );
        }
    }
} // namespace sw
