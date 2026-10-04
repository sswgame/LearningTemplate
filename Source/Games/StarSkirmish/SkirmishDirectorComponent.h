/**
 * @file SkirmishDirectorComponent.h
 * @brief StarSkirmish 의 판을 돌리는 컴포넌트 — 한 판(`SkirmishMatch`) · 고르기 · 명령 · 생산 · 건설 · 부대 입력 · 속도 · 알림, 그리고 절벽 · 유닛(프리팹) 스폰 지시입니다.
 *
 * @details 언리얼 GameMode/GameState 의 자리입니다. 씬에 하나 둡니다. 규칙(채취 · 생산 · 전투 · 안개 · AI)은 키트의 `RtsWorld` · `RtsAiController` 와
 *          `SkirmishMatch` 가 맡고, 여기는 사람의 입력과 무엇을 어디에 세우는지를 압니다. 유닛 모습은 뷰(`SkirmishUnitComponent`)가, 끌어 고르기 상자는
 *          `SkirmishDragComponent` 가 이 컴포넌트를 **읽기만** 해서 맞춥니다.
 *
 *          틱 규칙: 디렉터는 `TickGroup::PrePhysics` 에서 상태를 쓰고, 뷰 · 카메라 리그는 `PostUpdate` 에서 읽습니다(그룹은 차례로 돈다).
 *          스폰은 틱 안에서 할 수 없으므로 보이기 시작한 · 사라진 유닛 자리를 쌓아 두고 `executeOrDeferPostTick` 한 번으로 틱 뒤에 세우고 지웁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsCatalog.h"
#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsSelection.h"

#include "Games/StarSkirmish/SkirmishMatch.h"

namespace sw
{
    class Archive;
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class Material;
    class MaterialInstance;
    class OrthoCameraRigComponent;

    /**
     * @class SkirmishDirectorComponent
     * @brief 1 대 1 한 판입니다. 플레이가 시작되면 유닛 데이터를 읽고 판을 엽니다 — 사람(파랑) 대 컴퓨터, 자동 플레이면 컴퓨터 대 컴퓨터.
     * @details 판(월드 · AI 진행 · 고름 · 속도)은 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드 · 세이브를
     *          넘깁니다(`StarSkirmishGame`). 움직이던 유닛은 앞 명령의 길을 다시 구합니다. 세운 오브젝트는 핸들로 들고, 상태 저장 전에 걷습니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "Skirmish Director", Tooltip = "Runs the skirmish match, the human commands and spawns the unit views" )
    class SkirmishDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        SkirmishDirectorComponent();
        virtual ~SkirmishDirectorComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 세운 런타임 오브젝트를 모두 지웁니다(상태 저장 전). 판은 그대로이고 다음 틱이 그 상태대로 다시 세운다. */
        void despawnViews();
        /** @brief 판(월드 · AI 진행 · 고름 · 속도 · 멈춤)을 씁니다 — `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 의 바이트로 판을 되살립니다 — `ComponentStateStore::restore` 가 다시 만든 디렉터에 부릅니다.
         * @details 플레이 시작 전이면 들고 있다가 `onBeginPlay` 가 데이터를 읽은 뒤 적용합니다. 읽지 못하면 알리고 새 판으로 시작합니다.
         */
        void restoreState( vector<uint8>&& bytes );

        // ---- 뷰가 읽는 것(PostUpdate — 디렉터가 쓰지 않는 그룹) ----
        const RtsWorld& getWorld() const { return _match.getWorld(); }
        bool            isSelected( RtsUnitId unitId ) const { return _selection.isSelected( unitId ); }
        /** @brief 유닛 주인(또는 자원 종류) · 고름의 모습입니다. 아직 없으면 비어 있다. */
        const shared_ptr<MaterialInstance>& findUnitLook( const RtsUnit& unit, bool bSelected ) const;
        /** @brief 끌어 고르는 중이면 true 이고 땅 위 상자의 가운데 · 크기를 줍니다. */
        bool findDragBox( float3& outCenter, float3& outScale ) const;
        /**
         * @brief 핸들의 오브젝트에 붙은 디렉터입니다. 없으면 nullptr 입니다.
         * @details 뷰는 이것을 매 프레임 부르고 포인터를 들지 않습니다. 매니저 조회는 잠그지 않습니다(틱 중 여러 워커가 불러도 된다).
         */
        static const SkirmishDirectorComponent* resolveDirector( const GameObjectManager& manager, GameObjectHandle director );

    private:
        /** @brief 유닛 id 자리 하나의 모습입니다. 틱이 보일 유닛을 정하고 틱 뒤에 오브젝트를 맞춘다. */
        struct UnitSlot
        {
            RtsUnitId        _shownId{}; ///< 세웠거나 세울 유닛(무효면 없다)
            GameObjectHandle _object{};
            uint32           _stamp{ 0 };
        };

        /** @brief 색 하나의 머티리얼 인스턴스입니다(같은 색은 나눠 쓴다 — 배치 키가 인스턴스다). */
        struct ColorLook
        {
            shared_ptr<MaterialInstance> _instance{};
            float4                       _color{};
        };

    private:
        [[nodiscard]] bool loadData();
        /** @brief `writeState` 의 바이트를 읽습니다. 끝까지 맞지 않으면 false 입니다(판은 반쯤 바뀌었을 수 있다 — 부르는 쪽이 새 판으로 되돌린다). */
        [[nodiscard]] bool readState( Archive& archive );
        /** @brief 들고 있던 복원 바이트를 적용하고 모습을 다시 세우게 합니다. */
        void applyPendingState();
        /** @brief 쌓인 스폰 · 효과음을 틱 뒤 한 번으로 미룹니다(틱 밖이면 바로). */
        void scheduleFlush();
        /** @brief 쌓인 요청을 세웁니다. 틱 밖(게임 스레드)에서만 불린다. */
        void flushPending();
        void spawnCliffs( GameObjectManager& manager );
        void spawnUnit( GameObjectManager& manager, int32 slotIndex );
        void destroySpawned( GameObjectManager& manager, GameObjectHandle& inoutHandle );
        /** @brief 유닛 모습 인스턴스(편 · 자원 × 고름)를 만듭니다(뷰가 워커에서 고른다). */
        void                         prepareUnitLooks( Material* pMaterial );
        shared_ptr<MaterialInstance> acquireColorLook( Material* pMaterial, const float4& color );
        void                         playSound( const utf8* pPath );

        /** @brief 보일 유닛(안개 · 죽음)과 세운 모습을 견줘 바뀐 자리를 쌓습니다(PrePhysics). */
        void collectUnitChanges();
        void updateInput( const InputManager& input );
        void updateHumanCommands( const InputManager& input );
        void updateDrag( const InputManager& input, const float3& point, bool bPointValid );
        void issueRightClick( const float3& point, bool bQueue );
        void orderBuild( const utf8* pBuildingId, const float3& point );
        void trainFromPrimary( int32 productIndex );
        void handleEvents();
        /** @brief 마우스가 가리키는 땅(y = 0) 자리입니다. 맵 밖이면 false 입니다. */
        [[nodiscard]] bool       findGroundPoint( const InputManager& input, float3& outPoint ) const;
        OrthoCameraRigComponent* findCameraRig() const;
        bool                     isAutoPlayOn() const;
        GameObjectManager*       getObjectManager() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Unit Data", AssetPath, Tooltip = "Unit catalog XML" )
        string _unitDataPath;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _cliffPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _unitPrefab;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the OrthoCameraRigComponent the mouse is picked through" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Scene", DisplayName = "Watch Focus", Tooltip = "Camera focus when both players are AI (the whole map)", Meta = "Units=m" )
        float3 _watchFocus;
        PROPERTY( Category = "Scene", DisplayName = "Watch Ortho Height", Tooltip = "Camera height when both players are AI", Min = 0.1, Meta = "Units=m" )
        float32 _watchOrthoHeight;
        PROPERTY( Category = "Match", DisplayName = "Auto Play", Tooltip = "Both players are AI (-gv_skirmishAutoPlay=1 also turns it on)" )
        bool _bAutoPlay;

        RtsCatalog                           _catalog;
        SkirmishMatch                        _match;
        RtsSelection                         _selection;
        vector<RtsEvent>                     _listEvent;
        vector<UnitSlot>                     _listUnitSlot;
        vector<GameObjectHandle>             _listCliffObject;
        vector<int32>                        _listPendingUnit;  ///< 맞출 유닛 자리(틱 뒤)
        vector<const utf8*>                  _listPendingSound; ///< 낼 효과음(틱 뒤 — 오디오는 게임 스레드에서)
        vector<ColorLook>                    _listColorLook;
        vector<shared_ptr<MaterialInstance>> _listUnitLook;      ///< (편 · 자원 칸) × 2 + 고름
        vector<uint8>                        _pendingStateBytes; ///< 플레이 시작 전에 받은 복원 바이트(`restoreState`)
        float3                               _dragStart;
        float3                               _dragPoint;
        float32                              _timeScale;
        uint32                               _frameStamp;
        uint8                                _bHuman             : 1;
        uint8                                _bDragging          : 1;
        uint8                                _bDragPointValid    : 1;
        uint8                                _bAttackMovePending : 1;
        uint8                                _bPaused            : 1;
        uint8                                _bLoaded            : 1;
        uint8                                _bViewsSpawned      : 1; ///< 절벽 · 유닛 모습이 서 있다(걷으면 다음 틱이 다시 세운다)
        uint8                                _bFlushScheduled    : 1;
    };
} // namespace sw
