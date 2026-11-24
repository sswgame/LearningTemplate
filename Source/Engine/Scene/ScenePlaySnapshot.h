/**
 * @file Engine/Scene/ScenePlaySnapshot.h
 * @brief 플레이(PIE)를 시작할 때 찍은 편집 씬의 롤백 스냅샷입니다 — Stop 이 이것으로 편집 씬을 되돌린다.
 * @details 편집기(`EditorPlaySession`)가 찍고 되돌리지만 형은 엔진 것이다. 에디터 모듈을 리로드하는 동안 스냅샷은 씬 관리자가 맡는다
 *          (`SceneManager::holdPlaySnapshot`) — 조립 기록(`Scene::AssemblySnapshot`)은 바이트로 옮기기에 크고, 엔진 라이브러리는 리로드하지 않으니
 *          형의 모양이 옛 이미지와 함께 바뀌지 않는다(바이트만 받는 `IModuleStateStore` 와 다른 까닭). 언리얼이 PIE 동안 편집 월드를 엔진 쪽에 두는 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    /**
     * @struct ScenePlaySnapshot
     * @brief 롤백할 오브젝트 상태 · 씬 조립 기록 · 씬 정체(세대 · 이름 · 소스 경로)와 Keep Simulation Changes 로 남길 상태를 담습니다.
     */
    struct ScenePlaySnapshot
    {
        /**
         * @brief 롤백용 오브젝트 스냅샷 하나입니다.
         * @details 런타임 id(`_identity`)도 함께 저장한다. 정지할 때 상태를 되돌리면서 컴포넌트를 다시 만드는데, 원래 id 를
         *          복원해야 플레이 전에 보관하던 핸들(선택, 씬의 활성 카메라)이 그대로 이어진다.
         */
        struct ObjectSnapshot
        {
            ObjectIdentity _identity;
            string         _name;
            vector<uint8>  _bytes;
            string         _prefabPath; ///< 씬이 이 오브젝트에 연결한 프리팹 경로(`Scene::getEntityPrefabPath`). 씬을 다시 만들 때 복원한다
            /**
             * @brief 중첩 프리팹 부품 표시(`PrefabInstanceRegistry`)입니다. `_bNestedPart` 가 false 면 부품이 아닙니다.
             * @details 레지스트리는 놓인 오브젝트의 표시를 제거하므로, 플레이 중에 지워진 부품을 원래 id 로 복원할 때 이 값으로 다시 기록한다.
             */
            PrefabNestedPart _nestedPart;
            bool             _bNestedPart{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            /**
             * @brief 프로퍼티 밖에 있는 저장하지 않은 지형 편집(`TerrainComponent::writeState`)입니다. 없으면 비어 있습니다.
             * @details 되돌리기는 컴포넌트를 다시 만들고 지형이 디스크 에셋을 다시 읽으므로, 플레이 전에 칠한 것은 이것으로만 돌아온다.
             */
            vector<uint8> _listTerrainEdit;
        };

        vector<ObjectSnapshot> _listSnapshot;
        /** @brief 오브젝트 상태에 없는 씬 조립 기록(인스턴스 · 데이터 레이어 · 상속)입니다. 씬을 다시 만들어도 저장에서 풀리지 않게 함께 되돌립니다. */
        Scene::AssemblySnapshot _sceneAssembly;
        /** @brief Keep Simulation Changes 로 저장한 오브젝트의 플레이 중 상태입니다. Stop 이 씬을 되돌린 뒤 편집 씬에 적용합니다(`EditorPlaySession::keepObjectState`). */
        vector<ObjectSnapshot> _listKept;
        /**
         * @brief 스냅샷을 찍을 때의 활성 씬입니다. 씬 세대(`SceneManager::getSceneGeneration`), 이름, 소스 경로를 저장합니다.
         * @details Stop 할 때 활성 씬의 세대가 다르면 플레이 중에 씬이 바뀐 것이다. 게임 코드가 다음 레벨을 열었을 때가 그렇다.
         *          이때는 편집하던 씬을 저장해 둔 이름과 소스 경로로 다시 만든 뒤 되돌린다(`EditorPlaySession::restoreSnapshot`).
         */
        uint64 _sceneGeneration;
        string _sceneName;
        string _sceneSourcePath;
        uint8  _bHasSnapshot         : 1; ///< 스냅샷을 찍었다
        uint8  _bStartedWithoutScene : 1; ///< 활성 씬 없이 Play 를 시작했다 — Stop 은 플레이가 연 씬을 모두 내려 씬 없는 상태로 되돌린다

        ScenePlaySnapshot()
            : _listSnapshot{}
            , _sceneAssembly{}
            , _listKept{}
            , _sceneGeneration{ 0 }
            , _sceneName{}
            , _sceneSourcePath{}
            , _bHasSnapshot{ SW_FALSE }
            , _bStartedWithoutScene{ SW_FALSE }
        {
        }
    };
} // namespace sw
