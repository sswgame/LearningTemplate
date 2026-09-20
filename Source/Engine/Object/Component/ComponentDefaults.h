/**
 * @file ComponentDefaults.h
 * @brief 게임 gamedata.xml `<Defaults>`를 Component PROPERTY에 주입합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include <shared_mutex>

namespace sw
{
    struct TypeInfo;

    class Component;

    /**
     * @class ComponentDefaults
     * @brief 게임 부트스트랩이 지정한 gamedata.xml의 `<Defaults>`를 리플렉션으로 주입하는 서비스 매니저입니다.
     */
    class SW_API ComponentDefaults
    {
    public:
        ComponentDefaults();
        ~ComponentDefaults();

        ComponentDefaults( const ComponentDefaults& )            = delete;
        ComponentDefaults& operator=( const ComponentDefaults& ) = delete;

        /** @brief 인스턴스에 XML 기본값을 리플렉션으로 주입합니다. */
        void apply( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo = nullptr );
        /** @brief 컴포넌트 인스턴스에 XML 기본값을 리플렉션으로 주입합니다. */
        void apply( Component* pComp, const TypeInfo& typeInfo );

        /** @brief 게임 gamedata.xml 리소스 경로를 지정합니다. 비어 있으면 주입하지 않습니다. */
        void setPath( string_view path );

        /** @brief 현재 게임 gamedata.xml 리소스 경로를 반환합니다. */
        string getPath() const;

        /** @brief 캐시된 기본값 XML 문서를 다시 로드합니다. */
        void reload();

        /**
         * @brief 기본값 문서를 **실제로 열어 본 횟수**입니다. 진단·회귀 테스트용입니다.
         * @details 이 수는 `setPath`/`reload` 당 **1 이어야 한다.** 예전에는 로드가 실패하면
         *          "읽었다" 깃발이 false 로 남아 **컴포넌트를 만들 때마다 다시 열었다** — 기본값
         *          파일이 없는 게 정상인 게임에서 씬 로드가 `컴포넌트 수 x 파일 열기 실패` 가
         *          됐고, 엔티티 4000 개 씬의 로드 255 ms 중 207 ms 가 그것이었다.
         */
        uint32 getLoadAttemptCount() const { return _loadAttemptCount.load( std::memory_order_relaxed ); }

        // ----------------------------------------------------------------------
        // Static Facade (EngineServices 바인딩을 통해 위임)
        // ----------------------------------------------------------------------
        static void   applyDefaults( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo = nullptr );
        static void   applyDefaults( Component* pComp, const TypeInfo& typeInfo );
        static void   setDefaultsPath( string_view path );
        static string getDefaultsPath();
        static void   reloadDefaults();

    private:
        /** @brief 한 단계(타입 하나)의 `<Defaults>` 노드를 그 타입의 프로퍼티에 주입합니다. */
        static void applyNodeToProperties( void* pInstance, const TypeInfo& typeInfo, const XmlNode& compNode );

        void ensureDefaultsLoaded();

        /**
         * @brief 한 타입에 적용할 `<Defaults>` 단계들 — **뿌리 → 파생** 순서입니다.
         * @details 이 목록은 `TypeInfo` 와 기본값 문서만으로 정해지고 문서는 로드 중 바뀌지
         *          않는다. 그런데 예전에는 **컴포넌트를 만들 때마다** 다시 구했다 — 상속 체인을
         *          모으며 레지스트리를 N 번 조회하고, 단계마다 조회 이름 문자열 둘을 만들고,
         *          그 이름으로 XML 트리를 훑었다. 씬 하나에 컴포넌트가 4000 개면 그 전부가
         *          4000 번이다(실측 198 ms).
         */
        struct ResolvedDefaults
        {
            vector<pair<const TypeInfo*, XmlNode>> _listLevel;
        };

        /** @brief 타입별 해석 결과. 문서를 다시 읽으면(`reload`/`setPath`) 통째로 버립니다. */
        const ResolvedDefaults& resolveFor( const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo );
        /** @brief 문서를 다시 읽을 때 해석 결과를 버립니다 — `XmlNode` 가 그 문서를 가리킵니다. */
        void clearResolvedCache();

        XmlDocument _defaultsDoc;
        string      _customDefaultsPath;

        mutable std::shared_mutex                        _resolvedMutex;
        unordered_map<const TypeInfo*, ResolvedDefaults> _mapResolved;

        mutable mutex _defaultsMutex;
        /**
         * @brief 기본값 문서가 올라와 있는지. **원자적이어야 합니다.**
         * @details `ensureDefaultsLoaded` 는 락을 잡기 전에 이 값을 먼저 본다(이중 검사). 평범한
         *          `bool` 이면 그 읽기가 데이터 레이스이고, 더 나쁜 것은 **순서가 보장되지 않는
         *          다는 점**이다 — 한 스레드가 `true` 를 본 시점에 `_defaultsDoc` 은 아직 다 지어지지
         *          않았을 수 있다. release 로 쓰고 acquire 로 읽어 문서가 먼저 보이게 묶는다.
         */
        atomic<bool> _bDefaultsLoaded;
        /**
         * @brief 문서를 **읽어 보려고 시도했는지**. 성공 여부와 별개입니다.
         * @details 예전에는 성공 깃발 하나뿐이어서, 파일이 없거나 깨져 있으면 실패한 채로
         *          깃발이 false 로 남고 **다음 컴포넌트가 또 열었다.** 기본값 파일은 있어도
         *          되고 없어도 되는 것이라 없는 게 정상인데, 없을 때 씬 로드가
         *          `컴포넌트 수 x 파일 열기 실패` 가 됐다 — 엔티티 4000 개짜리 씬에서
         *          로드 255 ms 중 **207 ms** 가 이것이었다(2026-09-20 실측).
         *
         *          `reload()` · `setPath()` 는 이 깃발도 내려서 다시 시도하게 한다 —
         *          개발 중 파일을 만들어 넣는 길은 그대로 열려 있다.
         */
        atomic<bool> _bLoadAttempted;
        /** @brief 문서를 실제로 열어 본 횟수. `getLoadAttemptCount` 가 이것을 돌려줍니다. */
        atomic<uint32> _loadAttemptCount;
    };
} // namespace sw
