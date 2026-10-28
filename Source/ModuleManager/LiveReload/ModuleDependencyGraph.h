/**
 * @file ModuleManager/LiveReload/ModuleDependencyGraph.h
 * @brief 핫 리로드가 다루는 모듈의 의존 그래프입니다. 연쇄 교체 대상(의존하는 쪽까지)과 교체 순서(위상 정렬)를 계산합니다.
 *
 * @note 이름과 의존 목록만 다루는 순수한 계산이라 DLL 없이 테스트합니다(`Test/SmokeTest/TestModuleDependencyGraph.cpp`).
 *       핫 리로드(`LiveReloadManager`)만 쓰고, Shipping 빌드에서 함께 빠집니다(`Source/ModuleManager/CMakeLists.txt` 의 제외 목록).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 모듈 이름 → 그 모듈이 의존하는 모듈 이름 목록입니다. 그래프에 든 모듈만 계산에 들어갑니다.
     * @details 그래프에 없는 의존(Engine · 공용 GameFramework 처럼 이 매니저가 교체하지 않는 것)은 간선으로 세지 않습니다.
     *          `LiveReloadManager` 는 모듈 맵과 같은 이름 집합을 이 그래프에 둡니다(콜백만 먼저 등록한 모듈도 이름은 들어 있습니다).
     */
    class ModuleDependencyGraph final
    {
    public:
        /** @brief 빈 그래프로 시작합니다. */
        ModuleDependencyGraph();

        /** @brief @p moduleName 이 없으면 의존 없이 더합니다. 이미 있으면 그대로 둡니다. */
        void ensureModule( string_view moduleName );
        /** @brief @p moduleName 의 의존 목록을 바꿉니다. 없으면 더합니다. */
        void setDependsOn( string_view moduleName, const vector<string>& listDependsOn );
        /** @brief @p moduleName 을 뺍니다. 다른 모듈의 의존 목록에 남은 이름은 그래프 밖 의존이 됩니다. */
        void removeModule( string_view moduleName );
        /** @brief 모든 모듈을 뺍니다. */
        void clear();

        /** @brief @p moduleName 이 그래프에 있으면 true 입니다. */
        bool hasModule( string_view moduleName ) const;
        /** @brief @p moduleName 이 의존하는 모듈 이름입니다. 그래프에 없으면 빈 목록입니다. */
        const vector<string>& getDependsOn( string_view moduleName ) const;
        /** @brief 그래프에 든 모듈 이름을 모두 @p outListModuleName 에 덧붙입니다(순서는 정하지 않습니다). */
        void getModuleNames( vector<string>& outListModuleName ) const;

        /**
         * @brief @p root 와 그것에 (간접으로라도) 의존하는 모듈 이름을 @p outListUnique 에 중복 없이 덧붙입니다.
         * @details @p outListUnique 에 이미 든 이름은 다시 넣지 않습니다. 그래프에 없는 @p root 는 아무것도 더하지 않습니다.
         */
        void collectDependentClosure( string_view root, vector<string>& outListUnique ) const;
        /**
         * @brief @p listModuleName 가운데 그래프에 든 모듈을 의존 순서(의존되는 쪽 먼저)로 정렬합니다.
         * @details 같은 단계 안에서는 이름순이라 입력 순서와 무관하게 결과가 같습니다.
         * @return 순환이 있으면 순환에 든 모듈을 로그로 남기고 false 입니다(@p outListOrdered 는 빕니다).
         */
        bool sortByDependency( const vector<string>& listModuleName, vector<string>& outListOrdered ) const;

    private:
        unordered_map<string, vector<string>> _mapDependsOn; ///< 모듈 이름 → 의존 모듈 이름
    };
} // namespace sw
