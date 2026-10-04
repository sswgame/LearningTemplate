# -*- coding: utf-8 -*-
"""
SW Engine glTF 내보내기 — 고른 오브젝트 · 아마추어 · 애니메이션을 엔진 규약으로 `models_raw/<이름>.glb` 에 내보내고,
`SOCKET_` 엠프티를 메시 옆 `<이름>.sockets.xml` 초안으로 쓰고, `App --import-models` 를 띄워 `.mesh` 로 임포트한다.

설치: Blender → Edit → Preferences → Add-ons → Install… 에서 이 폴더를 zip 으로 묶어 고르거나, 이 폴더를 Blender 의
`scripts/addons/` 에 링크한다. 자세한 것은 Tools/DCC/Blender/README.md.

이 파일만 bpy 를 쓴다. 규약 · 소켓 XML · 임포트 명령은 bpy 없는 모듈(`Conventions` · `SocketXml` · `EngineImport`)에 있고 CI 가 시험한다.
"""

from __future__ import annotations

import os

import bpy
from bpy.props import BoolProperty, StringProperty
from bpy.types import AddonPreferences, Operator, Panel

from . import Conventions, EngineImport, SocketXml

bl_info = {
    "name": "SW Engine Exporter",
    "author": "SW Engine",
    "version": (1, 0, 0),
    "blender": (3, 6, 0),
    "location": "File > Export > SW Engine (.glb) · View3D > Sidebar > SW Engine",
    "description": "Export selection to glTF with SW Engine conventions, write a socket draft and import it into the engine",
    "category": "Import-Export",
}


class SwEngineExporterPreferences(AddonPreferences):
    bl_idname = __package__

    repository_root: StringProperty(name="Engine repository", subtype="DIR_PATH", description="Folder that holds Resource/ and build/")
    app_path: StringProperty(name="App executable", subtype="FILE_PATH", description="Optional: App.exe to run --import-models with")

    def draw(self, context):
        self.layout.prop(self, "repository_root")
        self.layout.prop(self, "app_path")


def getPreferencesInternal(context) -> SwEngineExporterPreferences:
    return context.preferences.addons[__package__].preferences


def toListMatrixInternal(matrix) -> list[list[float]]:
    return [[float(matrix[row][column]) for column in range(4)] for row in range(4)]


def collectSocketsInternal(listObject) -> list[SocketXml.SocketDraft]:
    """고른 오브젝트(와 그 자식) 가운데 소켓 엠프티를 엔진 공간 소켓으로 바꿉니다. 부모 본이 있으면 본 기준, 없으면 뿌리 기준입니다."""
    listSocket: list[SocketXml.SocketDraft] = []
    uniqueVisited: set[str] = set()
    listCandidate = list(listObject)
    for selected in listObject:
        listCandidate.extend(selected.children_recursive)
    for candidate in listCandidate:
        if candidate.name in uniqueVisited or candidate.type != "EMPTY":
            continue
        uniqueVisited.add(candidate.name)
        if not Conventions.isSocketObjectName(candidate.name, "sw_socket" in candidate.keys()):
            continue
        parentBone = ""
        localMatrix = candidate.matrix_world
        if candidate.parent is not None and candidate.parent_type == "BONE" and candidate.parent.type == "ARMATURE":
            bone = candidate.parent.data.bones[candidate.parent_bone]
            boneWorld = candidate.parent.matrix_world @ bone.matrix_local
            localMatrix = boneWorld.inverted() @ candidate.matrix_world
            parentBone = candidate.parent_bone
        kind = str(candidate.get("sw_socket_kind", "Attach"))
        preview = str(candidate.get("sw_socket_preview", ""))
        listSocket.append(SocketXml.makeSocketDraft(candidate.name, parentBone, toListMatrixInternal(localMatrix), kind, preview))
    return listSocket


class SW_OT_export_engine_gltf(Operator):
    """Export the selection to Resource/<pack>/models_raw as glTF with SW Engine conventions"""

    bl_idname = "sw_engine.export_gltf"
    bl_label = "Export to SW Engine"
    bl_options = {"REGISTER"}

    pack_name: StringProperty(name="Pack", default="empty", description="Resource pack: a game folder under Resource/game, or 'engine'")
    asset_name: StringProperty(name="Asset name", default="", description="Defaults to the active object's name")
    export_animations: BoolProperty(name="Animations", default=True)
    run_import: BoolProperty(name="Import into the engine", default=True, description="Run App --import-models after exporting")

    def invoke(self, context, event):
        if not self.asset_name and context.active_object is not None:
            self.asset_name = context.active_object.name
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        preferences = getPreferencesInternal(context)
        repositoryRoot = bpy.path.abspath(preferences.repository_root).rstrip("/\\")
        if not repositoryRoot or not os.path.isdir(os.path.join(repositoryRoot, "Resource")):
            self.report({"ERROR"}, "Set the engine repository (the folder with Resource/) in the add-on preferences")
            return {"CANCELLED"}
        listSelected = list(context.selected_objects)
        if not listSelected:
            self.report({"ERROR"}, "Select the objects to export")
            return {"CANCELLED"}
        try:
            paths = Conventions.makeExportPaths(repositoryRoot, self.pack_name, self.asset_name or listSelected[0].name)
            listSocket = collectSocketsInternal(listSelected)
        except ValueError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        os.makedirs(os.path.dirname(paths["source"]), exist_ok=True)
        # 축 · 단위: glTF 내보내기의 Y-up 변환을 쓰고 엔진 임포터가 X 를 뒤집는다(Conventions 머리말). 소켓 엠프티는 메시에 넣지 않는다.
        bpy.ops.export_scene.gltf(
            filepath=paths["source"],
            export_format="GLB",
            use_selection=True,
            export_yup=True,
            export_apply=True,
            export_animations=self.export_animations,
            export_skins=True,
            export_cameras=False,
            export_lights=False,
        )
        bWritten = SocketXml.writeSocketXmlIfMissing(paths["sockets"], listSocket)
        socketNote = f", {len(listSocket)} socket(s) -> {paths['sockets']}" if bWritten else (
            f", sockets kept ({paths['sockets']} exists)" if listSocket else "")
        self.report({"INFO"}, f"Exported {paths['source']}{socketNote}")

        if self.run_import:
            appPath = EngineImport.findAppExecutable(repositoryRoot, bpy.path.abspath(preferences.app_path) if preferences.app_path else "")
            if not appPath:
                self.report({"WARNING"}, "App was not found (build Ninja-Debug first) - run App --import-models yourself")
                return {"FINISHED"}
            process = EngineImport.startImport(appPath)
            output, _ = process.communicate()
            bClean, listLine = EngineImport.summarizeImportOutput(output)
            for line in listLine:
                self.report({"INFO"} if bClean else {"WARNING"}, line)
            if process.returncode != 0 or not bClean:
                self.report({"ERROR"}, f"App --import-models failed (exit {process.returncode}) - see the lines above")
                return {"CANCELLED"}
            self.report({"INFO"}, f"Imported: {paths['meshId']}")
        return {"FINISHED"}


class SW_PT_engine_exporter(Panel):
    bl_label = "SW Engine"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "SW Engine"

    def draw(self, context):
        self.layout.operator(SW_OT_export_engine_gltf.bl_idname, icon="EXPORT")
        self.layout.label(text="Sockets: empties named SOCKET_<Name>")


def drawExportMenuInternal(self, context):
    self.layout.operator(SW_OT_export_engine_gltf.bl_idname, text="SW Engine (.glb)")


_kClass = (SwEngineExporterPreferences, SW_OT_export_engine_gltf, SW_PT_engine_exporter)


def register():
    for cls in _kClass:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_export.append(drawExportMenuInternal)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(drawExportMenuInternal)
    for cls in reversed(_kClass):
        bpy.utils.unregister_class(cls)
