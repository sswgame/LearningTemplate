/**
 * @file SceneObjectReference.h
 * @brief 씬 · 프리팹 파일을 넘는 오브젝트 참조(언리얼 `FSoftObjectPath` 의 "패키지 경로 + 하위 오브젝트 경로" 자리)입니다.
 * @details 파일 안의 참조는 엔티티의 파일 id 로 잇는다(`ObjectStateBatch`). 파일을 넘는 참조는 (씬 경로, 파일 id) 두 칸이다.
 *          오브젝트마다 영속 GUID 를 새로 싣지 않은 이유: 파일 id 가 이미 그 파일 안에서 영속 키다 — 한 번 준 id 는 오브젝트가 사라져도 다시 주지 않는다
 *          (`Scene::computeNextFileID`). 그래서 (파일, 파일 id) 가 오브젝트를 영원히 하나만 가리키고, 씬 파일 형식을 바꾸지 않아도 된다.
 *          파일을 옮기면 경로는 이름 변경 리다이렉트가, 오브젝트를 다른 파일로 옮기는 일(잘라 붙이기)은 새 오브젝트로 본다 — 그때 GUID 가 필요해지면 다시 정한다.
 *
 *          해석은 결과를 형식으로 가른다(`SceneObjectResolveResult`). 대상 씬이 올라와 있지 않은 것(하위 레벨 · 스트리밍 셀이 내려감)은 정상 상태이고,
 *          씬은 있는데 파일 id 가 없는 것(지운 오브젝트)은 데이터 결함이다 — 부르는 쪽이 둘을 다르게 다룬다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObject;
    class Scene;
    class SceneManager;

    /** @brief `SceneObjectReferenceUtil::resolve` 의 결과입니다. */
    enum class SceneObjectResolveResult : uint8
    {
        Resolved = 0,   ///< 살아 있는 오브젝트를 찾았다
        Unset,          ///< 비어 있는 참조다(씬 경로나 파일 id 가 없다)
        SceneNotLoaded, ///< 대상 씬이 올라와 있지 않다(정상 — 하위 레벨 · 스트리밍 셀이 내려가 있다)
        ObjectMissing,  ///< 씬은 있는데 그 파일 id 의 오브젝트가 없다(지운 오브젝트, 또는 아직 내려간 셀의 오브젝트)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SceneObjectReference
     * @brief (씬 · 프리팹 리소스 경로, 그 파일의 엔티티 파일 id) 입니다. 컴포넌트의 `PROPERTY` 로 둡니다.
     */
    REFLECT()
    struct SceneObjectReference
    {
        REFLECT_BODY();

        /** @brief 대상 씬의 리소스 경로(`game/x/maps/a.scene.xml`)입니다. */
        PROPERTY( SkipIfEmpty )
        string _scenePath;

        /** @brief 대상 엔티티의 파일 id(씬 파일 `<entity id>`)입니다. 0 은 비어 있음입니다. */
        PROPERTY()
        uint64 _fileID{ 0 };

        bool isSet() const { return _scenePath.empty() == false && _fileID != 0; }
        bool operator==( const SceneObjectReference& other ) const { return _fileID == other._fileID && _scenePath == other._scenePath; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SceneObjectReferenceUtil
     * @brief 참조 만들기와 해석입니다.
     */
    struct SW_API SceneObjectReferenceUtil
    {
        /**
         * @brief @p scene 의 오브젝트 @p objectID 를 가리키는 참조를 만듭니다. 오브젝트에 파일 id 가 없으면 지금 줍니다(다음 저장이 그 id 를 씁니다).
         * @return 씬에 원본 경로가 없거나(저장한 적 없는 씬) 오브젝트가 없으면 false 이고 @p outReference 는 비웁니다.
         */
        [[nodiscard]] static bool makeReference( const Scene& scene, uint64 objectID, SceneObjectReference& outReference );
        /** @brief 올라와 있는 씬들(`SceneManager::getLoadedScenes`)에서 참조를 해석합니다. 찾으면 @p outObjectID 를 채웁니다. */
        [[nodiscard]] static SceneObjectResolveResult resolve( const SceneManager& sceneManager, const SceneObjectReference& reference, uint64& outObjectID );
        /** @brief 씬 하나에서 해석합니다. 씬의 원본 경로가 참조의 경로와 다르면 `SceneNotLoaded` 입니다. */
        [[nodiscard]] static SceneObjectResolveResult resolveInScene( const Scene& scene, const SceneObjectReference& reference, uint64& outObjectID );
        /** @brief 두 리소스 경로가 같은 파일인지 봅니다(구분자 · 대소문자를 맞춘다 — 리소스 id 는 대소문자를 가리지 않는다). */
        static bool isSameScenePath( string_view lhs, string_view rhs );
    };
} // namespace sw
