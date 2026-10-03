/**
 * @file GlobalVariableManager.h
 * @brief 인게임 치트 · 디버그 변수 · 환경 설정 같은 전역 변수를 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"

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
    // 1) GlobalVariableType / GlobalVariableInfo — 이름 · 타입 · 기본값 · 콜백
    // ------------------------------------------------------------------------------
    /** @brief 전역 변수의 저장 타입입니다. Enum 은 int32 로 저장합니다. */
    enum class GlobalVariableType : uint8
    {
        Boolean,
        Int32,
        Float,
        String,
        Enum
    };

    /**
     * @brief 테스트용 전역 변수(`SW_TEST_GLOBAL_VARIABLE_*`)를 Shipping 에도 남길지입니다. 매크로의 마지막 선택 인자
     *        `SW_KEEP_IN_SHIPPING` 으로만 씁니다.
     * @details 생략하면 Shipping 에서 빠집니다. 빠진 변수는 등록되지 않아 실행 인자로도 에디터로도 바꿀 수 없고, 기본값으로만
     *          읽힙니다. 배포 실행 파일을 스크립트가 조종하는 스위치(Shipping 검증이 쓰는 `-gv_profileFrames` 같은 것)만 남깁니다.
     * @note 빠진 변수를 `const` 로 만들지 않는 이유: 같은 TU 안에서 컴파일 시간 상수가 되어, 그 값을 루프 상한으로 쓰는 벤치 코드가
     *       Shipping 에서만 `-Wtautological-unsigned-zero-compare`(`step < 0` 은 늘 거짓) 경고를 냈습니다. 스위치를 하나 더할 때마다
     *       Shipping 빌드에서만 드러나는 함정이 되므로, 등록만 빼고 보통 변수로 둡니다.
     */
    enum class GlobalVariableShipping : uint8
    {
        Drop,
        Keep
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
         * @brief 테스트용(`SW_TEST_GLOBAL_VARIABLE_*`)이면 true 입니다.
         * @details 에디터 목록과 프리셋에서 빠집니다. 실행 인자(`-gv_*`)와 `findVariable` 은 그대로 됩니다.
         */
        bool _bTestOnly{ false };

        GlobalVariableChangedDelegate _onValueChanged;

        /** @brief Boolean 이면 *_pData 를, 아니면 false 를 반환합니다. */
        bool getValueAsBool() const;
        /** @brief Int32 · Enum 이면 *_pData 를, 아니면 0 을 반환합니다. */
        int32 getValueAsInt() const;
        /** @brief Float 이면 *_pData 를, 아니면 0 을 반환합니다. */
        float32 getValueAsFloat() const;
        /** @brief 타입에 맞게 문자열로 바꿉니다. */
        string getValueAsString() const;

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
        /** @brief *_pData 를 _defaultValue 로 되돌립니다. */
        void resetToDefault();
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) GlobalVariableManager — 등록 · 커맨드라인 동기화 · 모듈 단위 해제
    // ------------------------------------------------------------------------------
    /** @brief 이름 → Info 맵을 소유하고, 모듈을 핫 리로드할 때 그 모듈의 변수를 떼어 냅니다. */
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
         * @details enum 변수는 숫자 또는 열거자 이름을 받습니다. 이름은 파서가 걸린 뒤에 적용하므로(`applyPendingEnumText`) 그때까지 받아만 둡니다 —
         *          명령줄은 리플렉션 등록보다 먼저 파싱됩니다.
         */
        void updateFromCommandLine( const CommandLineManager* pCmdLineManager );

        /**
         * @brief enum 변수의 글 값(열거자 이름)을 정수로 바꾸는 파서를 겁니다. 빈 델리게이트를 주면 뗍니다.
         * @details Core 는 리플렉션을 모르므로 enum 표를 든 쪽(Engine — `engine::bindGlobalVariableEnumNames`)이 겁니다. 걸기 전에는 enum
         *          변수가 숫자만 받습니다. 프로세스에 하나이고 기동 · 종료 단계에서만 바꿉니다.
         */
        static void setEnumTextParser( const GlobalVariableEnumTextParser& parser );

        /**
         * @brief 명령줄에서 숫자가 아닌 글로 받아 둔 enum 변수 값을 지금 적용합니다. 파서를 건 뒤에 부릅니다.
         * @return 모르는 열거자가 하나라도 있으면 false 입니다(오류로 알리고, 그 값은 적용하지 않습니다).
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
         * @note 이 보장이 맵에서 나오지 않는다는 점이 중요합니다. `sw::unordered_map` 은 밀집 배열이라 삽입하면 재할당으로
         *       **모든** 원소가, 삭제하면 swap-and-pop 으로 **마지막 원소가** 옮겨 갑니다. 그래서 값을 `unique_ptr` 로 들고
         *       있습니다. 맵이 흔들려도 가리키는 객체는 제자리에 있습니다(값으로 담고 그 주소를 내주면 안 됩니다).
         */
        GlobalVariableInfo* findVariable( string_view name );

        /** @brief 등록된 변수 이름 목록의 스냅샷을 반환합니다(스레드 안전). */
        vector<string> collectVariableNames() const;

        /** @brief 등록된 변수 수를 반환합니다(스레드 안전). */
        uint32 getVariableCount() const;

    private:
        mutable std::shared_mutex _mutex;
        /**
         * @brief 이름 → 변수 맵입니다. **값이 `unique_ptr` 인 이유는 주소 안정성**입니다(`findVariable` 참고).
         */
        unordered_map<string, unique_ptr<GlobalVariableInfo>> _mapVariable;
        /**
         * @brief `registerToCommandLine` 이 넘겨준 파서입니다. 나중에 등록되는 변수의 보류값을 여기서 꺼냅니다.
         * @details 소유하지 않습니다. 둘 다 `EngineLoop` 이 들고 있고, 선언 순서상 이 매니저가 먼저 파괴됩니다. 모듈은
         *          `CommandLineManager` 를 서비스로 받을 수 없으므로(HostOnly) 보류값을 꺼내는 경로는 이 포인터뿐입니다.
         */
        class CommandLineManager* _pCmdLineManager{ nullptr };
        /** @brief 명령줄에서 글로 받아 파서를 기다리는 enum 변수 값입니다(이름 → 글). `applyPendingEnumText` 가 비웁니다. */
        unordered_map<string, string> _mapPendingEnumText;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) GlobalVariableRegistrar — 정적 초기화 때 하나뿐인 전역 리스트에 연결된다(타입 등록자와 같다)
    // ------------------------------------------------------------------------------
    /**
     * @brief 정적 객체가 전역 리스트(`getHead()`)에 자신을 붙입니다. 어느 이미지(Engine · 모듈 · App · 테스트)에서 정의해도 같습니다.
     * @details 리스트를 떼어 등록하는 쪽이 이미지마다 정해져 있어서, 정의하는 쪽은 아무것도 고를 필요가 없습니다.
     *          - Engine(과 함께 링크된 App · 테스트 · 배포본의 모든 것): 기동할 때 `EngineLoop` · 테스트 main 이 "Engine" 으로 등록하고 비운다.
     *          - 핫 리로드 모듈(EditorModule · SWGame …): 로드 직후 `LiveReloadManager` 가 타입 등록자와 함께 떼어 **모듈 이름으로** 등록하고
     *            (`engine::registerModuleTypes`), 내리기 전에 그 이름으로 걷는다(`engine::unregisterModuleTypes`). 모듈 코드는 등록 · 해제를
     *            부르지 않는다.
     *          주의: 헤더 매크로로 모듈 로컬 헤드를 갈아 끼우는 방식은 그 헤더를 include 하지 않은 .cpp 의 변수를 조용히 Engine 리스트에
     *          붙여, 모듈이 내려간 뒤 매니저가 언맵된 주소를 가리키게 한다.
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

        /** @brief 아직 아무도 떼어 가지 않은 등록자들의 리스트 헤드입니다(Engine.dll 이 가진다). 떼어 등록한 쪽이 nullptr 로 비웁니다. */
        static GlobalVariableRegistrar*& getHead();
    };
} // namespace sw

// ------------------------------------------------------------------------------
// 4) 정의 · 참조 매크로 — 일반(`SW_GLOBAL_VARIABLE_*`)과 테스트용(`SW_TEST_GLOBAL_VARIABLE_*`)
// ------------------------------------------------------------------------------
// 일반: 에디터 목록 · 프리셋 · 실행 인자 모두에 나온다. 런타임에 바꿔 볼 설정(`gv_viewMode`, `gv_rhiBackend` …)이다.
// 테스트용: 벤치 · 자동화 · 진단 스위치다. 에디터 목록과 프리셋에서 빠지고(`_bTestOnly`), **Shipping 에서도 빠진다** — 그 빌드에서는
//          등록되지 않아 기본값으로만 읽힌다(GlobalVariableShipping 참고). 배포 실행 파일을 스크립트가 조종해야 하는 것만 마지막 인자로
//          `SW_KEEP_IN_SHIPPING` 을 준다. 다른 TU 에서 참조할 때도 `SW_EXTERN_TEST_*` 에 같은 인자를 준다 — 참조만 보고도 배포 빌드에서
//          값을 바꿀 수 있는지 알 수 있게 하려는 것이다(어긋나면 `CheckGlobalVariableKinds` 게이트가 막는다).
//
//     SW_TEST_GLOBAL_VARIABLE_INT( gv_benchMeshes, 0, "…" );                            // Shipping 에서 빠진다
//     SW_TEST_GLOBAL_VARIABLE_INT( gv_profileFrames, 0, "…", SW_KEEP_IN_SHIPPING );    // Shipping 에 남는다
//
// 선택 인자는 **C++17 에서도** 되도록 `__VA_OPT__`(C++20) 대신 인자 개수로 고른다(`…_PICK*_IMPL`). 끝에 붙인 `~` 는 가변 인자가
// 비지 않게 하는 자리다(C++17 은 빈 가변 인자를 허용하지 않는다). MSVC 계열 전처리기는 `__VA_ARGS__` 를 다른 매크로에 한 덩어리로
// 넘기므로 `SW_GLOBAL_VARIABLE_EXPAND_IMPL` 로 한 번 더 펼친다.
//
// 아래 정의들에서 `name` 은 **선언자 이름**이면서 `#name`(문자열화)과 `sw_reg_##name`(토큰 붙이기)으로도 쓰인다.
// 셋 다 괄호를 씌우면 깨진다. 값 인자(`defaultVal`)는 괄호와 static_cast 로 이미 감싸 두었다.
// NOLINTBEGIN(bugprone-macro-parentheses)

/** @brief 테스트용 매크로의 마지막 선택 인자입니다. 주면 그 변수가 Shipping 에도 남습니다. */
#define SW_KEEP_IN_SHIPPING ::sw::GlobalVariableShipping::Keep

#define SW_GLOBAL_VARIABLE_EXPAND_IMPL( x )                              x
#define SW_GLOBAL_VARIABLE_PICK3_IMPL( a1, a2, picked, ... )             picked
#define SW_GLOBAL_VARIABLE_PICK4_IMPL( a1, a2, a3, picked, ... )         picked
#define SW_GLOBAL_VARIABLE_PICK5_IMPL( a1, a2, a3, a4, picked, ... )     picked
#define SW_GLOBAL_VARIABLE_PICK6_IMPL( a1, a2, a3, a4, a5, picked, ... ) picked
#define SW_GLOBAL_VARIABLE_ASSERT_KEEP_IMPL( keep ) \
    static_assert( ( keep ) == ::sw::GlobalVariableShipping::Keep, "the optional last argument must be SW_KEEP_IN_SHIPPING" )

// 스칼라 네 종류의 저장 타입과, 기본값을 그 타입으로 바꾸는 식. 종류 이름은 GlobalVariableType 의 열거자와 같다.
#define SW_GLOBAL_VARIABLE_STORAGE_Boolean_IMPL          bool
#define SW_GLOBAL_VARIABLE_STORAGE_Int32_IMPL            int32
#define SW_GLOBAL_VARIABLE_STORAGE_Float_IMPL            float32
#define SW_GLOBAL_VARIABLE_STORAGE_String_IMPL           sw::string
#define SW_GLOBAL_VARIABLE_CONVERT_Boolean_IMPL( value ) static_cast<bool>( value )
#define SW_GLOBAL_VARIABLE_CONVERT_Int32_IMPL( value )   static_cast<int32>( value )
#define SW_GLOBAL_VARIABLE_CONVERT_Float_IMPL( value )   static_cast<float32>( value )
#define SW_GLOBAL_VARIABLE_CONVERT_String_IMPL( value ) \
    sw::string { ( value ) }

/** @brief 변수를 정의하고 등록 리스트에 매다는 몸통입니다. 일반 · 테스트용이 함께 씁니다. */
#define SW_GLOBAL_VARIABLE_REGISTERED_IMPL( storageType, name, initValue, type, registrarDefault, desc, enumTypeName, typeSize, bTestOnly ) \
    extern storageType                   name;                                                                                              \
    storageType                          name = ( initValue );                                                                              \
    static ::sw::GlobalVariableRegistrar sw_reg_##name( #name, type, &name, registrarDefault, desc, enumTypeName, "", typeSize, bTestOnly )

/** @brief Shipping 에서 빠진 테스트용 변수입니다. 등록하지 않고 기본값으로 초기화한 변수만 남깁니다. */
#define SW_GLOBAL_VARIABLE_UNREGISTERED_IMPL( storageType, name, initValue ) \
    extern storageType name;                                                 \
    storageType        name = ( initValue )

#define SW_GLOBAL_VARIABLE_SCALAR_IMPL( kind, name, defaultVal, desc, bTestOnly )                                               \
    SW_GLOBAL_VARIABLE_REGISTERED_IMPL( SW_GLOBAL_VARIABLE_STORAGE_##kind##_IMPL, name, defaultVal,                             \
                                        ::sw::GlobalVariableType::kind, SW_GLOBAL_VARIABLE_CONVERT_##kind##_IMPL( defaultVal ), \
                                        desc, "", 4u, bTestOnly )

#define SW_GLOBAL_VARIABLE_ENUM_IMPL( name, enumType, defaultVal, desc, bTestOnly )                 \
    SW_GLOBAL_VARIABLE_REGISTERED_IMPL( enumType, name, defaultVal, ::sw::GlobalVariableType::Enum, \
                                        static_cast<int32>( defaultVal ), desc, #enumType,          \
                                        static_cast<uint32>( sizeof( enumType ) ), bTestOnly )

/** @brief bool 전역 변수를 정의하고 등록 리스트에 매답니다. */
#define SW_GLOBAL_VARIABLE_BOOL( name, defaultVal, desc ) SW_GLOBAL_VARIABLE_SCALAR_IMPL( Boolean, name, defaultVal, desc, false )
/** @brief int32 전역 변수를 정의하고 등록 리스트에 매답니다. */
#define SW_GLOBAL_VARIABLE_INT( name, defaultVal, desc ) SW_GLOBAL_VARIABLE_SCALAR_IMPL( Int32, name, defaultVal, desc, false )
/** @brief float32 전역 변수를 정의하고 등록 리스트에 매답니다. */
#define SW_GLOBAL_VARIABLE_FLOAT( name, defaultVal, desc ) SW_GLOBAL_VARIABLE_SCALAR_IMPL( Float, name, defaultVal, desc, false )
/** @brief sw::string 전역 변수를 정의하고 등록 리스트에 매답니다. */
#define SW_GLOBAL_VARIABLE_STRING( name, defaultVal, desc ) SW_GLOBAL_VARIABLE_SCALAR_IMPL( String, name, defaultVal, desc, false )
/** @brief enum 전역 변수를 정의하고 등록 리스트에 매답니다. */
#define SW_GLOBAL_VARIABLE_ENUM( name, enumType, defaultVal, desc ) SW_GLOBAL_VARIABLE_ENUM_IMPL( name, enumType, defaultVal, desc, false )

// 테스트용 정의: 인자 넷(마지막이 SW_KEEP_IN_SHIPPING)이면 KEEP, 셋이면 DROP 을 고른다.
#define SW_GLOBAL_VARIABLE_TEST_KEEP_IMPL( kind, name, defaultVal, desc, keep ) \
    SW_GLOBAL_VARIABLE_ASSERT_KEEP_IMPL( keep );                                \
    SW_GLOBAL_VARIABLE_SCALAR_IMPL( kind, name, defaultVal, desc, true )
#define SW_GLOBAL_VARIABLE_TEST_KEEP_ENUM_IMPL( name, enumType, defaultVal, desc, keep ) \
    SW_GLOBAL_VARIABLE_ASSERT_KEEP_IMPL( keep );                                         \
    SW_GLOBAL_VARIABLE_ENUM_IMPL( name, enumType, defaultVal, desc, true )
#if defined( SW_SHIPPING )
    #define SW_GLOBAL_VARIABLE_TEST_DROP_IMPL( kind, name, defaultVal, desc ) \
        SW_GLOBAL_VARIABLE_UNREGISTERED_IMPL( SW_GLOBAL_VARIABLE_STORAGE_##kind##_IMPL, name, SW_GLOBAL_VARIABLE_CONVERT_##kind##_IMPL( defaultVal ) )
    #define SW_GLOBAL_VARIABLE_TEST_DROP_ENUM_IMPL( name, enumType, defaultVal, desc ) SW_GLOBAL_VARIABLE_UNREGISTERED_IMPL( enumType, name, defaultVal )
#else
    #define SW_GLOBAL_VARIABLE_TEST_DROP_IMPL( kind, name, defaultVal, desc ) SW_GLOBAL_VARIABLE_SCALAR_IMPL( kind, name, defaultVal, desc, true )
    #define SW_GLOBAL_VARIABLE_TEST_DROP_ENUM_IMPL( name, enumType, defaultVal, desc ) \
        SW_GLOBAL_VARIABLE_ENUM_IMPL( name, enumType, defaultVal, desc, true )
#endif
#define SW_GLOBAL_VARIABLE_TEST_SCALAR_IMPL( kind, ... )                                           \
    SW_GLOBAL_VARIABLE_EXPAND_IMPL( SW_GLOBAL_VARIABLE_EXPAND_IMPL( SW_GLOBAL_VARIABLE_PICK5_IMPL( \
        __VA_ARGS__, SW_GLOBAL_VARIABLE_TEST_KEEP_IMPL, SW_GLOBAL_VARIABLE_TEST_DROP_IMPL, ~) )( kind, __VA_ARGS__ ) )

/** @brief 테스트용 bool 전역 변수입니다: `( name, defaultVal, desc [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_TEST_GLOBAL_VARIABLE_BOOL( ... ) SW_GLOBAL_VARIABLE_TEST_SCALAR_IMPL( Boolean, __VA_ARGS__ )
/** @brief 테스트용 int32 전역 변수입니다: `( name, defaultVal, desc [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_TEST_GLOBAL_VARIABLE_INT( ... ) SW_GLOBAL_VARIABLE_TEST_SCALAR_IMPL( Int32, __VA_ARGS__ )
/** @brief 테스트용 float32 전역 변수입니다: `( name, defaultVal, desc [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_TEST_GLOBAL_VARIABLE_FLOAT( ... ) SW_GLOBAL_VARIABLE_TEST_SCALAR_IMPL( Float, __VA_ARGS__ )
/** @brief 테스트용 sw::string 전역 변수입니다: `( name, defaultVal, desc [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_TEST_GLOBAL_VARIABLE_STRING( ... ) SW_GLOBAL_VARIABLE_TEST_SCALAR_IMPL( String, __VA_ARGS__ )
/** @brief 테스트용 enum 전역 변수입니다: `( name, enumType, defaultVal, desc [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_TEST_GLOBAL_VARIABLE_ENUM( ... )                                                        \
    SW_GLOBAL_VARIABLE_EXPAND_IMPL( SW_GLOBAL_VARIABLE_EXPAND_IMPL( SW_GLOBAL_VARIABLE_PICK6_IMPL( \
        __VA_ARGS__, SW_GLOBAL_VARIABLE_TEST_KEEP_ENUM_IMPL, SW_GLOBAL_VARIABLE_TEST_DROP_ENUM_IMPL, ~) )( __VA_ARGS__ ) )

// NOLINTEND(bugprone-macro-parentheses)

/** @brief 다른 TU 에서 bool 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_BOOL( name ) extern bool name
/** @brief 다른 TU 에서 int32 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_INT( name ) extern int32 name
/** @brief 다른 TU 에서 float32 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_FLOAT( name ) extern float32 name
/** @brief 다른 TU 에서 sw::string 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_STRING( name ) extern sw::string name
/** @brief 다른 TU 에서 enum 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_ENUM( name, enumType ) extern enumType name

// 테스트용 참조: 정의와 같은 인자를 준다. 선언 모양은 빌드와 상관없이 같다(빠진 변수도 등록만 빠진 보통 변수다).
#define SW_GLOBAL_VARIABLE_EXTERN_TEST_DROP_IMPL( storageType, name ) extern storageType name
#define SW_GLOBAL_VARIABLE_EXTERN_TEST_KEEP_IMPL( storageType, name, keep ) \
    SW_GLOBAL_VARIABLE_ASSERT_KEEP_IMPL( keep );                            \
    extern storageType name
#define SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_KEEP_IMPL( kind, name, keep ) \
    SW_GLOBAL_VARIABLE_EXTERN_TEST_KEEP_IMPL( SW_GLOBAL_VARIABLE_STORAGE_##kind##_IMPL, name, keep )
#define SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_DROP_IMPL( kind, name ) \
    SW_GLOBAL_VARIABLE_EXTERN_TEST_DROP_IMPL( SW_GLOBAL_VARIABLE_STORAGE_##kind##_IMPL, name )
#define SW_GLOBAL_VARIABLE_EXTERN_TEST_ENUM_KEEP_IMPL( name, enumType, keep ) SW_GLOBAL_VARIABLE_EXTERN_TEST_KEEP_IMPL( enumType, name, keep )
#define SW_GLOBAL_VARIABLE_EXTERN_TEST_ENUM_DROP_IMPL( name, enumType )       SW_GLOBAL_VARIABLE_EXTERN_TEST_DROP_IMPL( enumType, name )
#define SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_IMPL( kind, ... )                                    \
    SW_GLOBAL_VARIABLE_EXPAND_IMPL( SW_GLOBAL_VARIABLE_EXPAND_IMPL( SW_GLOBAL_VARIABLE_PICK3_IMPL( \
        __VA_ARGS__, SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_KEEP_IMPL, SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_DROP_IMPL, ~) )( kind, __VA_ARGS__ ) )

/** @brief 다른 TU 에서 테스트용 bool 전역 변수를 참조합니다: `( name [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_EXTERN_TEST_GLOBAL_VARIABLE_BOOL( ... ) SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_IMPL( Boolean, __VA_ARGS__ )
/** @brief 다른 TU 에서 테스트용 int32 전역 변수를 참조합니다: `( name [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_EXTERN_TEST_GLOBAL_VARIABLE_INT( ... ) SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_IMPL( Int32, __VA_ARGS__ )
/** @brief 다른 TU 에서 테스트용 float32 전역 변수를 참조합니다: `( name [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_EXTERN_TEST_GLOBAL_VARIABLE_FLOAT( ... ) SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_IMPL( Float, __VA_ARGS__ )
/** @brief 다른 TU 에서 테스트용 sw::string 전역 변수를 참조합니다: `( name [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_EXTERN_TEST_GLOBAL_VARIABLE_STRING( ... ) SW_GLOBAL_VARIABLE_EXTERN_TEST_SCALAR_IMPL( String, __VA_ARGS__ )
/** @brief 다른 TU 에서 테스트용 enum 전역 변수를 참조합니다: `( name, enumType [, SW_KEEP_IN_SHIPPING] )`. */
#define SW_EXTERN_TEST_GLOBAL_VARIABLE_ENUM( ... )                                                 \
    SW_GLOBAL_VARIABLE_EXPAND_IMPL( SW_GLOBAL_VARIABLE_EXPAND_IMPL( SW_GLOBAL_VARIABLE_PICK4_IMPL( \
        __VA_ARGS__, SW_GLOBAL_VARIABLE_EXTERN_TEST_ENUM_KEEP_IMPL, SW_GLOBAL_VARIABLE_EXTERN_TEST_ENUM_DROP_IMPL, ~) )( __VA_ARGS__ ) )
