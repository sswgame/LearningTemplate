/**
 * @file GlobalVariableManager.h
 * @brief 인게임 치트 · 디버그 변수 · 환경 설정 같은 전역 변수를 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include <variant>

namespace sw
{
    /** @brief 커맨드라인 파서. registerToCommandLine / updateFromCommandLine 에 넘깁니다. */
    class CommandLineManager;

    /** @brief 등록된 전역 변수 한 항목입니다. */
    struct GlobalVariableInfo;
    SW_DECLARE_DELEGATE( void, GlobalVariableChangedDelegate, GlobalVariableInfo* );
    /** @brief enum 타입 이름과 글(열거자 이름)을 받아 정수 값을 냅니다. 모르는 이름이면 false 입니다. */
    SW_DECLARE_DELEGATE( bool, GlobalVariableEnumTextParser, string_view, string_view, int32& );

    // ------------------------------------------------------------------------------
    // 1) GlobalVariableType / GlobalVariableInfo. 이름, 타입, 기본값, 콜백을 정의한다.
    // ------------------------------------------------------------------------------
    /**
     * @brief 전역 변수의 저장 타입입니다. Enum 은 int32 로 저장합니다.
     * @details 타입마다 다른 처리(문자열 변환 · 파싱 · 기본값 쓰기 · 명령줄 인자)는 `GlobalVariableManager.cpp` 의 처리 테이블에 타입당 한 행으로
     *          모여 있습니다. 새 타입은 여기 열거자 하나와 그 테이블의 행 하나, 에디터 위젯 테이블(`GlobalVariablesPanel.cpp`)의 행 하나를 더합니다.
     *          행 수는 `Count` 와 `static_assert` 로 맞춥니다.
     */
    enum class GlobalVariableType : uint8
    {
        Boolean,
        Int32,
        Float,
        String,
        Enum,
        /** @brief 열거자 수입니다. 변수의 타입이 아니며 `registerVariable` 이 거절합니다. */
        Count
    };

    /**
     * @brief C++ 타입 → 전역 변수 저장 타입입니다. 정의 매크로가 첫 인자(타입)로 고릅니다. 특수화가 없는 타입은 컴파일 오류입니다.
     * @details `StorageType` 은 등록 정보의 기본값(`std::variant`)에 담는 타입이고, `kTypeSize` 는 enum 을 쓸 때의 바이트 수입니다.
     */
    template <typename T, typename = void>
    struct GlobalVariableTraits;

    /** @brief bool 변수입니다. */
    template <>
    struct GlobalVariableTraits<bool>
    {
        using StorageType                             = bool;
        static constexpr GlobalVariableType kType     = GlobalVariableType::Boolean;
        static constexpr uint32             kTypeSize = 4u;
    };

    /** @brief int32 변수입니다. */
    template <>
    struct GlobalVariableTraits<int32>
    {
        using StorageType                             = int32;
        static constexpr GlobalVariableType kType     = GlobalVariableType::Int32;
        static constexpr uint32             kTypeSize = 4u;
    };

    /** @brief float32 변수입니다. */
    template <>
    struct GlobalVariableTraits<float32>
    {
        using StorageType                             = float32;
        static constexpr GlobalVariableType kType     = GlobalVariableType::Float;
        static constexpr uint32             kTypeSize = 4u;
    };

    /** @brief sw::string 변수입니다. */
    template <>
    struct GlobalVariableTraits<string>
    {
        using StorageType                             = string;
        static constexpr GlobalVariableType kType     = GlobalVariableType::String;
        static constexpr uint32             kTypeSize = 4u;
    };

    /** @brief enum 변수입니다. int32 로 읽고 쓰며, 실제 크기(`sizeof`)만큼만 씁니다. */
    template <typename T>
    struct GlobalVariableTraits<T, std::enable_if_t<std::is_enum_v<T>>>
    {
        static_assert( sizeof( T ) <= sizeof( int64 ), "global variable enum is wider than 8 bytes" );
        using StorageType                             = int32;
        static constexpr GlobalVariableType kType     = GlobalVariableType::Enum;
        static constexpr uint32             kTypeSize = static_cast<uint32>( sizeof( T ) );
    };

    /** @brief 전역 변수 하나의 메타데이터와 현재 값 포인터입니다. */
    struct SW_API GlobalVariableInfo
    {
        string                                     _name;
        GlobalVariableType                         _type = GlobalVariableType::Boolean;
        void*                                      _pData{ nullptr };
        std::variant<bool, int32, float32, string> _defaultValue;
        string                                     _description;
        string                                     _enumType;
        string                                     _moduleName;
        uint32                                     _typeSize{ 4 };
        /**
         * @brief 테스트용(`SW_TEST_GLOBAL_VARIABLE` · `SW_TEST_GLOBAL_VARIABLE_SHIPPED`)이면 true 입니다.
         * @details 에디터 목록과 프리셋에서 빠집니다. 실행 인자(`-gv_*`)와 `findVariable` 은 그대로 됩니다.
         */
        bool _bTestOnly{ false };

        /**
         * @brief enum 변수의 글 값(열거자 이름)을 읽는 파서입니다. 이 변수를 등록한 `GlobalVariableManager` 의 것을 가리킵니다(매니저가 소유하고, 변수보다 오래 삽니다).
         * @details enum 이 아니거나 매니저에 등록하지 않은 변수는 nullptr 이고, 그때 `setValueFromString` 은 숫자만 받습니다.
         */
        const GlobalVariableEnumTextParser* _pEnumTextParser{ nullptr };

        GlobalVariableChangedDelegate _onValueChanged;

        /** @brief Boolean 이면 *_pData 를, 아니면 false 를 반환합니다. */
        bool getValueAsBool() const;
        /** @brief Int32 · Enum 이면 *_pData 를, 아니면 0 을 반환합니다. */
        int32 getValueAsInt() const;
        /** @brief Float 이면 *_pData 를, 아니면 0 을 반환합니다. */
        float32 getValueAsFloat() const;
        /** @brief 타입에 맞게 문자열로 바꿉니다. */
        string getValueAsString() const;
        /**
         * @brief 타입을 표시 · 프리셋용 이름으로 반환합니다(`Bool` · `Int32` · `Float` · `String`). enum 변수는 enum 타입 이름입니다.
         * @details 에디터 패널과 프리셋 저장이 같은 이름을 쓰도록 여기 한 곳에서 정합니다.
         */
        string getTypeName() const;

        /** @brief *_pData 에 Boolean 값을 쓰고 콜백을 부릅니다. */
        bool setValueAsBool( bool val );
        /** @brief *_pData 에 Int32 · Enum 값을 쓰고 콜백을 부릅니다. */
        bool setValueAsInt( int32 val );
        /** @brief *_pData 에 Float 값을 쓰고 콜백을 부릅니다. */
        bool setValueAsFloat( float32 val );
        /** @brief *_pData 에 String 값을 쓰고 콜백을 부릅니다. */
        bool setValueAsString( string_view val );

        /** @brief 문자열을 파싱해 *_pData 에 쓰고 콜백을 부릅니다. */
        bool setValueFromString( string_view strValue );
        /**
         * @brief 명령줄에서 받은 글을 `setValueFromString` 처럼 쓰되, enum 변수는 숫자를 오류로 거절합니다(열거자 이름만 받는다).
         * @details 숫자는 열거자를 더하거나 순서를 바꾸면 뜻이 달라지고, 변수 크기를 넘으면 조용히 잘려 다른 열거자가 된다. 에디터 · 프리셋 ·
         *          사용자 설정이 주고받는 안쪽 글은 `setValueFromString` 이라 숫자를 계속 받는다.
         */
        bool setValueFromCommandLine( string_view strValue );
        /** @brief *_pData 를 _defaultValue 로 되돌립니다. */
        void resetToDefault();
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) GlobalVariableManager. 등록, 커맨드라인 동기화, 모듈 단위 해제를 맡는다.
    // ------------------------------------------------------------------------------
    /** @brief 이름에서 Info 로 가는 맵을 소유하고, 모듈을 핫 리로드할 때 그 모듈의 변수를 해제합니다. */
    class SW_API GlobalVariableManager
    {
    public:
        /** @brief 빈 맵으로 둡니다. */
        GlobalVariableManager() = default;
        /** @brief 맵만 해제합니다. 변수가 가리키는 사용자 데이터는 건드리지 않습니다. */
        virtual ~GlobalVariableManager() = default;
        /** @brief 복사를 금지합니다. */
        GlobalVariableManager( const GlobalVariableManager& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        GlobalVariableManager& operator=( const GlobalVariableManager& ) = delete;

        /** @brief 모든 변수를 기본값으로 되돌립니다. */
        void shutdown()
        {
            resetAllToDefault();
            _pCmdLineManager = nullptr;
        }

        /** @brief 커맨드라인 매니저에 변수들을 인자로 등록합니다. */
        void registerToCommandLine( class CommandLineManager* pCmdLineManager );

        /**
         * @brief 커맨드라인 인자 값으로 전역 변수들을 갱신합니다.
         * @details enum 변수는 열거자 이름만 받는다(숫자는 `applyPendingEnumText` 가 오류로 알린다). 이름은 파서가 등록된 뒤에 적용하므로
         *          그때까지 받아만 둔다. 명령줄은 리플렉션 등록보다 먼저 파싱되기 때문이다.
         */
        void updateFromCommandLine( const CommandLineManager* pCmdLineManager );

        /**
         * @brief enum 변수의 문자열 값(열거자 이름)을 정수로 바꾸는 파서를 등록합니다. 빈 델리게이트를 주면 해제합니다.
         * @details Core 는 리플렉션을 모르므로 enum 테이블을 가진 쪽(Engine 의 `engine::bindGlobalVariableEnumNames`)이 등록한다. 등록하기 전에는 enum
         *          변수가 숫자만 받는다. 이 매니저의 변수에만 닿고(변수가 `_pEnumTextParser` 로 가리킨다), 기동과 종료 단계에서만 바꾼다.
         */
        void setEnumTextParser( const GlobalVariableEnumTextParser& parser );

        /**
         * @brief 명령줄에서 받아 둔 enum 변수 값(열거자 이름)을 지금 적용합니다. 파서를 건 뒤에 부릅니다.
         * @return 모르는 열거자나 숫자가 하나라도 있으면 false 입니다(오류로 알리고, 그 값은 적용하지 않습니다).
         */
        [[nodiscard]] bool applyPendingEnumText();

        /** @brief 새 전역 변수를 등록합니다. @p bTestOnly 는 테스트용 매크로로 선언한 변수면 true 입니다. */
        bool registerVariable( string_view name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, string_view description, string_view enumType = "", string_view moduleName = "", uint32 typeSize = 4, bool bTestOnly = false );

        /** @brief pHead 로 시작하는 연결 리스트(한 모듈의 변수들)를 등록합니다. */
        void registerPendingVariables( string_view moduleName, const struct GlobalVariableRegistrar* pHead );

        /** @brief 특정 모듈 이름으로 등록된 변수들을 해제합니다. */
        void unregisterVariablesByModule( string_view moduleName );

        /** @brief 문자열을 파싱해 변수 값을 설정합니다. */
        bool setValueFromString( string_view name, string_view strValue );

        /** @brief 특정 변수를 기본값으로 되돌립니다. */
        bool resetToDefault( string_view name );

        /** @brief 모든 변수를 기본값으로 되돌립니다. */
        void resetAllToDefault();

        /**
         * @brief 이름으로 전역 변수 정보를 찾습니다.
         * @details **반환한 포인터는 그 변수가 등록 해제될 때까지 유효합니다.** 다른 변수를 등록하거나 해제해도 옮겨지지
         *          않습니다. 그래서 패널처럼 "이름을 훑으며 포인터를 모아 두었다가 한 번에 그리는" 방식이 안전합니다.
         * @note 이 보장은 맵이 주는 것이 아닙니다. `sw::unordered_map` 은 밀집 배열이라 삽입하면 재할당으로
         *       **모든** 원소가, 삭제하면 swap-and-pop 으로 **마지막 원소가** 옮겨 갑니다. 그래서 값을 `unique_ptr` 로 보관합니다.
         *       맵이 흔들려도 가리키는 객체는 제자리에 있습니다(값으로 담고 그 주소를 내주면 안 됩니다).
         */
        GlobalVariableInfo* findVariable( string_view name );

        /**
         * @brief 등록된 변수의 포인터를 @p outListVariable 에 채웁니다(먼저 비운다, 순서 없음, 스레드 안전).
         * @details 포인터는 `findVariable` 과 같이 그 변수가 해제될 때까지 유효합니다. 들고 있는 쪽은 `getRegistrationGeneration` 이 바뀌면 다시 채웁니다.
         */
        void collectVariables( vector<GlobalVariableInfo*>& outListVariable ) const;

        /**
         * @brief 등록 세대입니다. 변수를 등록하거나 해제(모듈 언로드)할 때마다 오릅니다.
         * @details 목록을 캐시하는 쪽(전역 변수 패널)이 이것만 비교해 매 프레임 이름을 복사하지 않는다. 값 변경으로는 오르지 않는다.
         */
        uint64 getRegistrationGeneration() const { return _registrationGeneration.load( std::memory_order_acquire ); }

        /** @brief 등록된 변수 수를 반환합니다(스레드 안전). */
        uint32 getVariableCount() const;

    private:
        mutable std::shared_mutex _mutex;
        /**
         * @brief 이름 → 변수 맵입니다. **값이 `unique_ptr` 인 이유는 주소 안정성**입니다(`findVariable` 참고).
         */
        unordered_map<string, unique_ptr<GlobalVariableInfo>> _mapVariable;
        /** @brief `_mapVariable` 에 넣거나 뺄 때마다 오른다(`getRegistrationGeneration`). 쓰기는 배타 잠금 안에서만 한다. */
        atomic<uint64> _registrationGeneration{ 1 };
        /**
         * @brief `registerToCommandLine` 이 넘겨준 파서입니다. 나중에 등록되는 변수의 보류값을 여기서 꺼냅니다.
         * @details 소유하지 않습니다. 둘 다 `Engine` 이 들고 있고, 선언 순서상 이 매니저가 먼저 파괴됩니다. 모듈은
         *          `CommandLineManager` 를 서비스로 받을 수 없으므로(HostOnly) 보류값을 꺼내는 경로는 이 포인터뿐입니다.
         */
        class CommandLineManager* _pCmdLineManager{ nullptr };
        /** @brief 명령줄에서 글로 받아 파서를 기다리는 enum 변수 값입니다(이름 → 글). `applyPendingEnumText` 가 비웁니다. */
        unordered_map<string, string> _mapPendingEnumText;
        /** @brief enum 변수의 글 값 파서입니다(`setEnumTextParser`). 등록한 enum 변수가 이 주소를 가리키므로 매니저는 옮겨지지 않습니다(복사 금지). */
        GlobalVariableEnumTextParser _enumTextParser;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) GlobalVariableRegistrar. 정적 초기화 때 하나뿐인 전역 리스트에 연결된다(타입 등록자와 같다).
    // ------------------------------------------------------------------------------
    /**
     * @brief 정적 객체가 전역 리스트(`getHead()`)에 자신을 붙입니다. 어느 이미지(Engine, 모듈, App, 테스트)에서 정의해도 같습니다.
     * @details 리스트를 가져와 등록하는 쪽이 이미지마다 정해져 있어서, 정의하는 쪽은 아무것도 고를 필요가 없다.
     *          - Engine(과 함께 링크된 App, 테스트, 배포본의 모든 것): 기동할 때 `Engine` 와 테스트 main 이 "Engine" 으로 등록하고 비운다.
     *          - 핫 리로드 모듈(EditorModule, SWGame 등): 로드 직후 `LiveReloadManager` 가 타입 등록자와 함께 가져와 **모듈 이름으로** 등록하고
     *            (`engine::registerModuleTypes`), 언로드하기 전에 그 이름으로 해제한다(`engine::unregisterModuleTypes`). 모듈 코드는 등록과 해제를
     *            부르지 않는다.
     *          주의: 헤더 매크로로 모듈 로컬 헤드를 바꿔 끼우는 방식은 그 헤더를 include 하지 않은 .cpp 의 변수를 조용히 Engine 리스트에
     *          붙여, 모듈이 언로드된 뒤 매니저가 언맵된 주소를 가리키게 한다.
     */
    struct SW_API GlobalVariableRegistrar
    {
        string                                     _name;
        GlobalVariableType                         _type;
        void*                                      _pData;
        std::variant<bool, int32, float32, string> _defaultValue;
        string                                     _description;
        string                                     _enumType;
        string                                     _moduleName;
        uint32                                     _typeSize;
        bool                                       _bTestOnly;
        GlobalVariableRegistrar*                   _pNext;

        /** @brief `getHead()` 리스트의 앞에 연결합니다. */
        GlobalVariableRegistrar( const utf8* pName, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, const utf8* pDescription, const utf8* pEnumType = "", const utf8* pModuleName = "", uint32 typeSize = 4, bool bTestOnly = false );

        /**
         * @brief 정의 매크로가 부르는 오버로드입니다. 저장 타입과 크기는 `GlobalVariableTraits<T>` 가, 기본값은 지금 @p variable 의 값이 정합니다.
         * @details 매크로가 변수를 기본값으로 초기화한 바로 다음 줄에서 만들어지므로(같은 TU 의 정적 초기화는 선언 순서다) 읽는 값이 곧 기본값이다.
         *          @p pTypeName 은 매크로 첫 인자의 문자열(`#type`)이고 enum 일 때만 남긴다. 에디터와 명령줄이 그 이름으로 리플렉션 enum 테이블을 찾는다.
         */
        template <typename T>
        GlobalVariableRegistrar( const utf8* pName, T& variable, const utf8* pTypeName, const utf8* pDescription, bool bTestOnly )
            : GlobalVariableRegistrar( pName, GlobalVariableTraits<T>::kType, &variable,
                                       static_cast<typename GlobalVariableTraits<T>::StorageType>( variable ), pDescription,
                                       GlobalVariableTraits<T>::kType == GlobalVariableType::Enum ? pTypeName : "", "",
                                       GlobalVariableTraits<T>::kTypeSize, bTestOnly )
        {
        }

        /** @brief 아직 아무도 가져가지 않은 등록자들의 리스트 헤드입니다(Engine.dll 이 가집니다). 가져가서 등록한 쪽이 nullptr 로 비웁니다. */
        static GlobalVariableRegistrar*& getHead();
    };
} // namespace sw

// ------------------------------------------------------------------------------
// 4) 정의와 참조 매크로. 첫 인자가 타입이고, 종류는 매크로 이름이 정한다.
// ------------------------------------------------------------------------------
// `SW_GLOBAL_VARIABLE`              일반: 에디터 목록 · 프리셋 · 실행 인자 모두에 나온다. 런타임에 바꿔 볼 설정(`gv_viewMode` …).
// `SW_TEST_GLOBAL_VARIABLE`         테스트용: 벤치 · 자동화 · 진단 스위치. 에디터 목록 · 프리셋에서 빠지고(`_bTestOnly`), **Shipping 에서는
//                                   등록되지 않아** 기본값으로만 읽힌다(실행 인자로도 에디터로도 못 바꾼다).
// `SW_TEST_GLOBAL_VARIABLE_SHIPPED` 테스트용인데 Shipping 에도 등록된다. 배포 실행 파일을 스크립트가 조종하는 스위치(`gv_profileFrames` ·
//                                   `gv_screenshot*` · `gv_crashTest` …)만 이것으로 둔다.
// `SW_EXTERN_GLOBAL_VARIABLE`       다른 TU 에서 참조한다. 종류와 상관없이 같다. 타입이 정의와 어긋나면 `CheckGlobalVariableKinds` 게이트가 막는다.
//
//     SW_GLOBAL_VARIABLE( int32, gv_viewMode, 0, "…" );
//     SW_GLOBAL_VARIABLE( RHIBackend, gv_rhiBackend, RHIBackend::DirectX12, "…" );   // enum — `#type` 이 리플렉션 enum 이름이다
//     SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMeshes, 0, "…" );
//     SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_profileFrames, 0, "…" );
//     SW_EXTERN_GLOBAL_VARIABLE( int32, gv_viewMode );
//
// 타입은 `bool`, `int32`, `float32`, `sw::string`, enum 이다(`GlobalVariableTraits`). enum 은 리플렉션에 등록된 이름 그대로 적는다
// (`#type` 이 그 문자열이 된다. `sw::RHIBackend` 처럼 한정하면 찾지 못한다). 기본값은 변수 초기화에만 쓰고, 등록자는 그 변수에서 읽는다.
// 종류 · 모드를 고르는 설정은 숫자 코드(`int32` 0 · 1 · 2)로 두지 않고 enum 으로 둔다: 명령줄은 열거자 이름(`-gv_rhiBackend=Vulkan`)만 받고 숫자는 오류다.
// Shipping 에서 빠진 테스트용 변수를 `const` 로 만들지 않는다: 같은 TU 에서 컴파일 시간 상수가 되면 그 값을 루프 상한으로 쓰는 벤치
// 코드가 Shipping 에서만 `-Wtautological-unsigned-zero-compare` 경고를 낸다. 등록만 빼고 보통 변수로 둔다.
//
// `name` 은 **선언자 이름**이면서 `#name`(문자열화)과 `sw_reg_##name`(토큰 붙이기)으로도 쓰이고, `type` 은 선언 타입이면서 `#type` 이다.
// 괄호를 씌우면 깨진다. 값 인자(`defaultValue`)는 괄호로 감싸 두었다.
// NOLINTBEGIN(bugprone-macro-parentheses)

/** @brief 전역 변수를 정의하고 등록 리스트에 매답니다: `( type, name, defaultValue, desc )`. */
#define SW_GLOBAL_VARIABLE( type, name, defaultValue, desc )      \
    extern type                          name;                    \
    type                                 name = ( defaultValue ); \
    static ::sw::GlobalVariableRegistrar sw_reg_##name( #name, name, #type, desc, false )

/** @brief 테스트용 전역 변수인데 Shipping 에도 등록합니다: `( type, name, defaultValue, desc )`. */
#define SW_TEST_GLOBAL_VARIABLE_SHIPPED( type, name, defaultValue, desc ) \
    extern type                          name;                            \
    type                                 name = ( defaultValue );         \
    static ::sw::GlobalVariableRegistrar sw_reg_##name( #name, name, #type, desc, true )

/** @brief 테스트용 전역 변수입니다. Shipping 에서는 등록하지 않고 기본값으로 초기화한 변수만 남깁니다: `( type, name, defaultValue, desc )`. */
#if defined( SW_SHIPPING )
    #define SW_TEST_GLOBAL_VARIABLE( type, name, defaultValue, desc ) \
        extern type name;                                             \
        type        name = ( defaultValue )
#else
    #define SW_TEST_GLOBAL_VARIABLE( type, name, defaultValue, desc ) SW_TEST_GLOBAL_VARIABLE_SHIPPED( type, name, defaultValue, desc )
#endif

// NOLINTEND(bugprone-macro-parentheses)

/** @brief 다른 TU 에서 전역 변수를 참조합니다: `( type, name )`. 정의의 종류(일반 · 테스트용)와 상관없이 같습니다. */
#define SW_EXTERN_GLOBAL_VARIABLE( type, name ) extern type name
