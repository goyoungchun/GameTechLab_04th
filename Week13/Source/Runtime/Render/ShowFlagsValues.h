// 표시(Show) 플래그 목록.
// 여기 한 줄을 추가하면 FEngineShowFlags 멤버, 에디터 Show 메뉴, editor.ini 저장/불러오기가 함께 생긴다.
// 이 파일은 SHOWFLAG 매크로를 정의한 뒤에만 포함한다. #pragma once를 두지 않는다.
//
// SHOWFLAG(이름, 게임 기본값, 에디터 기본값, 분류, 메뉴 아이콘, 메뉴 표시 이름)
//   분류가 Hidden인 플래그는 Show 메뉴에 나오지 않는다. 뷰 모드나 Render 메뉴, 코드가 정한다.

#ifndef SHOWFLAG
#error "SHOWFLAG(Name, bGameDefault, bEditorDefault, Category, Icon, DisplayName) must be defined before including ShowFlagsValues.h"
#endif

// ---------- Common ----------
SHOWFLAG(Bounds,          false, false, Common,    "Bounds",       "Bounding Boxes")
SHOWFLAG(Decals,          true,  true,  Common,    "Decals",       "Decals")
SHOWFLAG(Grid,            false, true,  Common,    "Grid",         "Grid")
SHOWFLAG(Particles,       true,  true,  Common,    "Particles",    "Particles")
SHOWFLAG(SkeletalMeshes,  true,  true,  Common,    "SkeletalMesh", "Skeletal Meshes")
SHOWFLAG(StaticMeshes,    true,  true,  Common,    "StaticMesh",   "Static Meshes")

// ---------- Physics ----------
SHOWFLAG(Collision,       false, true,  Physics,   "Bounds",       "Collision")
SHOWFLAG(PhysicsBodies,   false, false, Physics,   "SkeletalMesh", "Physics Bodies")
SHOWFLAG(Constraints,     false, false, Physics,   "Pivot",        "Constraints")

// ---------- Post Process ----------
SHOWFLAG(DepthOfField,    true,  true,  PostProcess, "Camera",     "Depth of Field")
SHOWFLAG(Vignette,        true,  true,  PostProcess, "Lighting",   "Vignette")
SHOWFLAG(CameraOverlay,   true,  true,  PostProcess, "Camera",     "Camera Fade / Letterbox")

// ---------- Developer ----------
SHOWFLAG(CascadeBounds,   false, false, Developer, "Cascade",      "Cascade Bounds")
SHOWFLAG(ObjectUUID,      false, false, Developer, "Pivot",        "Object UUID")
SHOWFLAG(RootBone,        false, false, Developer, "SkeletalMesh", "Root Bone")

// ---------- Hidden: 뷰 모드·Render 메뉴·코드가 정한다 ----------
SHOWFLAG(Fog,                   true,  true,  Hidden, nullptr, "Fog")
SHOWFLAG(AntiAliasing,          false, false, Hidden, nullptr, "Anti-Aliasing (FXAA)")
SHOWFLAG(VisualizeSceneDepth,   false, false, Hidden, nullptr, "Visualize Scene Depth")
SHOWFLAG(VisualizeCascades,     false, false, Hidden, nullptr, "Visualize Shadow Cascades")
SHOWFLAG(VisualizeWorldNormal,  false, false, Hidden, nullptr, "Visualize World Normal")
SHOWFLAG(VisualizeLightCulling, false, false, Hidden, nullptr, "Visualize Light Culling")
