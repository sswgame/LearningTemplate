/**
 * @file PropertyShapeEdit.h
 * @brief 선언형 모양 손잡이의 Model 쪽입니다 — 컴포넌트 프로퍼티 메타(`Shape=` · `ShapeRange=`)를 읽어 모양 선 · 손잡이를 값으로 모으고,
 *        손잡이 도구(View `PropertyShapeTool.cpp`)가 보낸 명령(잡기 · 끌기 · 놓기)을 프로퍼티 편집 요청(`PropertyEditRequest`)으로 적용해 끌기 하나를 되돌리기 하나로 남깁니다.
 * @details 메타 문법은 기존 커스텀 메타(`Meta = "Key=Value, Key2=Value2"`)를 그대로 쓴다 — 파서 · 런타임 메타데이터는 바뀌지 않는다.
 *          - `PROPERTY( …, Meta = "Shape=Sphere" ) float32 _radius;`
 *          - `PROPERTY( …, Meta = "Shape=Box" ) float3 _halfExtents;`
 *          - `PROPERTY( …, Meta = "Shape=Cone, ShapeRange=_radius" ) float32 _outerConeAngle;`(반각 rad, 길이는 같은 타입의 float32 프로퍼티)
 *          - `PROPERTY( …, Meta = "Shape=Arrow" ) float3 _localDirection;`
 *          틀은 컴포넌트(씬 컴포넌트가 아니면 오브젝트의 주 씬 컴포넌트)의 월드 위치 · 회전이다. 콜라이더 손잡이(`EditorColliderHandleEdit`)와 같은 모양이고,
 *          쓰기는 인스펙터와 같은 편집 요청 길을 지나 허용 범위(`Min` · `Max`)와 알림(`onPropertyChanged`)이 같다. ImGui 를 모릅니다.
 *          층: 편집 요청이 인스펙터 Model(Panels 층)에 있어 이 기능도 Panels 층(씬 뷰 패널 곁)에 둔다 — 시각화 등록은 층을 가리지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Panels/Scene/Model/PropertyShape.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw
{
    struct ObjectSnapshot;
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class TypeRegistry;
} // namespace sw

namespace sw::editor
{
    struct EditorViewportVisualizerArgs;
    struct EditorVisualizerDrawList;

    /**
     * @struct PropertyShapeDecl
     * @brief 프로퍼티 하나의 모양 선언입니다(`Shape=` 메타를 읽은 것). 프로퍼티 포인터는 그 프레임에만 쓴다 — 핫 리로드가 타입을 다시 올리면 낡는다.
     */
    struct SW_EDITOR_API PropertyShapeDecl
    {
        const PropertyInfo* _pProperty{ nullptr };      ///< 값 프로퍼티(반지름 · 반 크기 · 반각 · 방향)
        const PropertyInfo* _pRangeProperty{ nullptr }; ///< `Cone` 의 길이 프로퍼티(`ShapeRange=`). 다른 모양은 nullptr
        PropertyShapeKind   _kind{ PropertyShapeKind::Sphere };

        /**
         * @brief 타입 @p type(상속 포함)의 모양 선언을 @p outListDecl 뒤에 붙입니다(비우지 않는다).
         * @return 선언이 틀리면(모르는 모양 · 값 형식이 맞지 않음 · 원뿔에 `ShapeRange` 가 없거나 그 프로퍼티가 float32 가 아님) 그 오류입니다 — 앞에서 읽은 선언은 남는다.
         *         폴백으로 그리지 않는다: 틀린 선언은 그 타입의 모양 전부를 막고 오류를 남긴다.
         */
        static expected<void> collect( const TypeInfo& type, vector<PropertyShapeDecl>& outListDecl );
        /** @brief 인스턴스 @p pInstance 의 지금 값으로 틀 @p frame 위의 모양 값을 만듭니다. */
        PropertyShapeValue makeValue( const void* pInstance, const PropertyShapeFrame& frame ) const;
    };

    /** @brief 손잡이 도구가 이번 프레임에 그릴 값입니다(`PropertyShapeHandleEdit::gather`). */
    struct PropertyShapeHandleViewState
    {
        GameObjectHandle            _object{};            ///< 손잡이의 오브젝트
        vector<PropertyShapeHandle> _listHandle;          ///< 선언마다의 손잡이(월드)
        bool                        _bAvailable{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 대상이 하나이고 편집이 허용된다
    };

    /** @brief 손잡이 도구가 보내는 명령의 종류입니다. */
    enum class PropertyShapeHandleCommandKind : uint8
    {
        Capture, ///< 손잡이를 눌렀다 — 대상의 시작 전 스냅샷을 찍는다(되돌리기의 "전")
        Drag,    ///< 끄는 중이다 — 마우스 레이가 가리키는 자리로 프로퍼티를 고친다
        End,     ///< 놓았거나(`_bCommit`) 끊겼다 — 바뀌었고 편집이 허용되면 되돌리기를 남기고 닫는다
    };

    /** @brief 손잡이 명령 하나입니다(값만 든다). */
    struct PropertyShapeHandleCommand
    {
        PropertyShapeHandle            _handle{};
        Ray                            _ray{};
        GameObjectHandle               _object{};
        PropertyShapeHandleCommandKind _kind{ PropertyShapeHandleCommandKind::End };
        bool                           _bCommit{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 놓기(End)가 되돌리기를 남긴다(끊긴 끌기는 버린다)
    };

    /**
     * @class PropertyShapeHandleDragTracker
     * @brief 손잡이 도구가 내는 명령의 순서(잡기 → 끌기들 → 놓기)를 정합니다. 마우스 상태만 받아 ImGui · 씬 없이 테스트한다.
     */
    class SW_EDITOR_API PropertyShapeHandleDragTracker
    {
    public:
        /**
         * @brief 끄는 중의 한 프레임입니다. 대상이 없어졌으면(@p bAvailable false) 커밋 없이, 단추를 놓았으면 커밋으로 닫는다. 아니면 @p pRay 가 있을 때 끌기를 낸다.
         * @return 끌기가 이어지면 true, 이 프레임에 닫았으면 false 입니다(끄지 않는 중이면 @p bAvailable).
         */
        bool step( bool bAvailable, bool bMouseDown, const Ray* pRay, vector<PropertyShapeHandleCommand>& outListCommand );
        /** @brief 손잡이 @p handle 을 눌렀습니다 — 잡기 명령을 내고 끌기를 시작한다. */
        void                       press( GameObjectHandle object, const PropertyShapeHandle& handle, vector<PropertyShapeHandleCommand>& outListCommand );
        bool                       isDragging() const { return _bDragging != 0; }
        GameObjectHandle           getObject() const { return _object; }
        const PropertyShapeHandle& getActive() const { return _active; }

    private:
        PropertyShapeHandle _active{};
        GameObjectHandle    _object{};
        uint8               _bDragging{ 0 }; ///< 끄는 중이다
    };

    /**
     * @class PropertyShapeHandleEdit
     * @brief 손잡이 끌기의 적용과 기록입니다(메인 씬 — 메인 선택의 오브젝트).
     */
    class SW_EDITOR_API PropertyShapeHandleEdit
    {
    public:
        PropertyShapeHandleEdit();
        ~PropertyShapeHandleEdit();
        PropertyShapeHandleEdit( const PropertyShapeHandleEdit& )            = delete;
        PropertyShapeHandleEdit& operator=( const PropertyShapeHandleEdit& ) = delete;

        /**
         * @brief 손잡이 대상(끄는 중이면 @p dragged, 아니면 메인 선택이 하나일 때 그 오브젝트)의 선언형 모양 손잡이를 @p outState 에 모읍니다.
         * @details 플레이 중이면 편집이 막혀 대상이 없다(`_bAvailable` false).
         */
        static void gather( GameObjectHandle dragged, PropertyShapeHandleViewState& outState );
        /**
         * @brief 컴포넌트 @p component 에 손잡이 @p handle 을 레이 @p ray 로 끈 값을 편집 요청으로 씁니다(허용 범위로 자르고 `onPropertyChanged` 로 알린다).
         * @return 값이 바뀌었으면 true 입니다(같은 값 · 풀리지 않는 프로퍼티는 false — 쓰지 않는다).
         */
        static bool applyDrag( Component& component, const PropertyShapeHandle& handle, const Ray& ray, const TypeRegistry& registry );
        /** @brief 명령들을 차례로 적용합니다. */
        void apply( const vector<PropertyShapeHandleCommand>& listCommand );
        /** @brief 끌기를 마치고 되돌리기를 남긴 횟수입니다(감시 값 `Editor.PropertyShapeCommitCount`). */
        uint32 getCommitCount() const { return _commitCount; }

    private:
        void capture( const PropertyShapeHandleCommand& command );
        void drag( const PropertyShapeHandleCommand& command );
        void end( bool bCommit );

    private:
        unique_ptr<ObjectSnapshot> _pBefore;     ///< 끌기 전 스냅샷 — 형식은 .cpp 만 본다
        string                     _undoLabel;   ///< 되돌리기 이름("Edit Radius")
        GameObjectHandle           _object;      ///< 스냅샷을 찍은 대상
        uint32                     _commitCount; ///< 되돌리기를 남긴 끌기 수
        uint8                      _bChanged;    ///< 잡은 뒤 값이 바뀌었다 — 바뀌지 않은 끌기는 기록하지 않는다
    };

    /**
     * @struct PropertyShapeHandleSession
     * @brief 손잡이 도구 한 벌(명령 순서 · 적용 · 이번 프레임 값)입니다. 도구는 시각화 등록 줄의 정적 함수라 에디터 범위의 상태 저장소가 소유한다(`get`).
     */
    struct SW_EDITOR_API PropertyShapeHandleSession
    {
        PropertyShapeHandleDragTracker     _tracker;
        PropertyShapeHandleEdit            _edit;
        PropertyShapeHandleViewState       _state;
        vector<PropertyShapeHandleCommand> _listCommand;           ///< 이번 프레임에 낸 명령(재사용 버퍼)
        vector<const TypeInfo*>            _listReportedType;      ///< 틀린 선언을 이미 알린 타입 — 프레임마다 같은 오류를 쌓지 않는다
        uint32                             _drawnHandleCount{ 0 }; ///< 지난 프레임에 그린 손잡이 수(감시 값 `Editor.PropertyShapeHandleCount`)

        /** @brief 지금 에디터 범위의 손잡이 도구입니다. */
        static PropertyShapeHandleSession& get();
        /** @brief 타입 @p type 의 선언을 @p outListDecl 에 모읍니다(먼저 비운다). 틀렸으면 비우고, 그 타입의 첫 오류만 로그로 남긴다. */
        void collectDeclarations( const TypeInfo& type, vector<PropertyShapeDecl>& outListDecl );
    };

    /** @brief 선택된 오브젝트들의 선언형 모양을 선 값으로 모읍니다(시각화 `property_shapes` 의 모으기 — 플레이 중에도 그린다). */
    struct PropertyShapeGatherer
    {
        static void gather( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList );
    };
} // namespace sw::editor
