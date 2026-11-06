/**
 * @file DataLayersModel.h
 * @brief 데이터 레이어 창의 모델 — 레이어 목록과 켜고 끄기입니다. ImGui 를 모릅니다.
 * @details 플레이 중에는 런타임과 같은 길로 올리고 내린다(`SceneDataLayerSubsystem`). 편집 중에 끄면 오브젝트를 내리지 않고 에디터에서만 숨긴다 — 내리면 저장하지 않은 편집을 잃는다.
 *          언리얼 에디터의 Data Layer 눈 아이콘도 편집 중에는 보이기만 바꾸고 런타임 상태와 따로다(기억에 의존). 켜면 아직 짓지 않은 묶음을 짓는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class Scene;
} // namespace sw

namespace sw::editor
{
    /** @brief 레이어 하나의 표 줄입니다. */
    struct DataLayerRow
    {
        string _name;
        uint32 _objectCount{ 0 };             ///< 지금 지어진 오브젝트 수
        uint8  _bShown{ SW_FALSE };           ///< 켜져 있고 숨기지 않았다
        uint8  _bInitiallyActive{ SW_FALSE }; ///< 씬이 열릴 때 켜진다(저장되는 값)
    };

    /** @brief 데이터 레이어 창의 상태와 명령입니다. */
    class DataLayersModel
    {
    public:
        DataLayersModel();

        /** @brief 활성 씬이 바뀌었으면 숨긴 레이어 기록을 버립니다(다른 씬의 이름이다). */
        void discardIfSceneChanged( uint64 sceneGeneration );
        /** @brief @p scene 의 줄을 채웁니다(비우고 채웁니다). */
        void collectRows( const Scene& scene, vector<DataLayerRow>& outListRow ) const;
        /** @brief 레이어를 보이거나 감춥니다(파일 머리말의 규칙). */
        void setLayerShown( Scene& scene, const string& name, bool bShown );
        /** @brief 씬이 열릴 때 레이어가 켜질지 정합니다. 다음 저장에 적힙니다. */
        static void setInitiallyActive( Scene& scene, const string& name, bool bInitiallyActive );

    private:
        unordered_set<string> _uniqueHiddenLayer; ///< 편집 중에 감춘 레이어
        uint64                _sceneGeneration;
    };
} // namespace sw::editor
