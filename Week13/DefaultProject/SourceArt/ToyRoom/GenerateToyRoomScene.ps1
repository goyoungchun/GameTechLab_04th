# ToyRoom.scene 생성기.
# 장난감 방(하늘색 구름 벽지, 나무 바닥, 침대, 책상, 장난감들) + 우디(AWoodyToyActor) + 사람(AToyRoomHumanActor)
# + 카메라(AToyRoomDirector)를 배치한다. 기존 씬의 액터를 템플릿으로 복제해 엔진의 씬 형식을 그대로 따른다.
# 사용: powershell -ExecutionPolicy Bypass -File GenerateToyRoomScene.ps1
# 단위는 m. 바닥 윗면이 Z=0, 방은 X,Y ∈ [-4, 4], 문은 동쪽 벽(X=+4)에 있다.

$ErrorActionPreference = 'Stop'
$ProjectDir = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$ScenesDir = Join-Path $ProjectDir 'Content\Scenes'
$OutPath = Join-Path $ScenesDir 'ToyRoom.scene'

function Read-Scene($Name) {
    $Text = [IO.File]::ReadAllText((Join-Path $ScenesDir $Name), [Text.Encoding]::UTF8)
    return $Text | ConvertFrom-Json
}
function Copy-Json($Object) { return ($Object | ConvertTo-Json -Depth 100 -Compress) | ConvertFrom-Json }

$Playground = Read-Scene 'KurisuPlayground.scene'
$Outdoor = Read-Scene 'Cloth_Outdoor.scene'
$PhysX = Read-Scene 'PhysXTest.scene'

$TplMesh = $Playground.World.PersistentLevel.Actors | Where-Object { $_.Class -eq 'AStaticMeshActor' -and $_.Name -eq 'Floor' } | Select-Object -First 1
$TplAmbient = $Playground.World.PersistentLevel.Actors | Where-Object { $_.Class -eq 'AAmbientLightActor' } | Select-Object -First 1
$TplSun = $Playground.World.PersistentLevel.Actors | Where-Object { $_.Class -eq 'ADirectionalLightActor' } | Select-Object -First 1
$TplPoint = $Outdoor.World.PersistentLevel.Actors | Where-Object { $_.Class -eq 'APointLightActor' } | Select-Object -First 1
$TplCharacter = $PhysX.World.PersistentLevel.Actors | Where-Object { $_.Class -eq 'AKurisuCharacter' } | Select-Object -First 1
$TplSkel = $TplCharacter.Components | Where-Object { $_.Class -eq 'USkeletalMeshComponent' } | Select-Object -First 1
foreach ($T in @($TplMesh, $TplAmbient, $TplSun, $TplPoint, $TplSkel)) { if (-not $T) { throw 'Template actor not found' } }

$Actors = New-Object System.Collections.ArrayList
$Used = @{}
function Get-UniqueName($Base) {
    if (-not $Used.ContainsKey($Base)) { $Used[$Base] = 0; return $Base }
    $Used[$Base]++
    return "{0}_{1}" -f $Base, $Used[$Base]
}

function Set-Transform($Props, $Loc, $Rot, $Scale) {
    $Props.Transform.Location.X = [double]$Loc[0]; $Props.Transform.Location.Y = [double]$Loc[1]; $Props.Transform.Location.Z = [double]$Loc[2]
    $Props.Transform.Rotation.Pitch = [double]$Rot[0]; $Props.Transform.Rotation.Yaw = [double]$Rot[1]; $Props.Transform.Rotation.Roll = [double]$Rot[2]
    $Props.Transform.Scale.X = [double]$Scale[0]; $Props.Transform.Scale.Y = [double]$Scale[1]; $Props.Transform.Scale.Z = [double]$Scale[2]
}

# 색 머티리얼 인스턴스 (DefaultMaterial 기반)
function New-ColorMaterial($Color, [double]$Shininess = 12, [string]$Shading = 'DefaultLit', [double]$Spec = 0.06) {
    $Material = Copy-Json $TplMesh.Components[1].OverrideMaterials[0]
    $Material.Instance.BaseColor.X = [double]$Color[0]; $Material.Instance.BaseColor.Y = [double]$Color[1]; $Material.Instance.BaseColor.Z = [double]$Color[2]; $Material.Instance.BaseColor.W = 1.0
    $Material.Instance.Shininess = $Shininess
    $Material.Instance.ShadingModel = $Shading
    $Material.Instance.SpecularColor.X = $Spec; $Material.Instance.SpecularColor.Y = $Spec; $Material.Instance.SpecularColor.Z = $Spec
    return $Material
}

# 기본 도형 하나. Cube: 크기 = Scale, Sphere: 반지름 = Scale, Cylinder: (반지름, 반지름, 높이), Torus: 큰 반지름 1, 관 0.25
function Add-Shape {
    param([string]$Name, [string]$Mesh, $Loc, $Scale, $Color, $Rot = @(0, 0, 0), [double]$Shininess = 12,
        [string]$Shading = 'DefaultLit', [string]$ParentActor = '', [bool]$Collision = $true, [bool]$Shadow = $true)
    $Actor = Copy-Json $TplMesh
    $Actor.Name = Get-UniqueName $Name
    Set-Transform $Actor.Components[0].Properties $Loc $Rot $Scale
    $MeshComp = $Actor.Components[1]
    $MeshComp.Properties.StaticMesh = $Mesh
    $MeshComp.Properties.bCastShadow = $Shadow
    $MeshComp.OverrideMaterials = @(New-ColorMaterial $Color $Shininess $Shading)
    if (-not $Collision) { $MeshComp.Properties.BodyInstance.CollisionEnabled = 'NoCollision' }
    if ($ParentActor) { $Actor.ParentActor = $ParentActor; $Actor.ParentComponent = 'DefaultSceneRoot' }
    [void]$Actors.Add($Actor)
    return $Actor
}
function Add-Box($Name, $Min, $Max, $Color, [double]$Shininess = 12) {
    $Center = @((($Min[0] + $Max[0]) / 2), (($Min[1] + $Max[1]) / 2), (($Min[2] + $Max[2]) / 2))
    $Size = @(($Max[0] - $Min[0]), ($Max[1] - $Min[1]), ($Max[2] - $Min[2]))
    return Add-Shape -Name $Name -Mesh 'Cube' -Loc $Center -Scale $Size -Color $Color -Shininess $Shininess
}

# ---- 색 ----
$WallBlue = @(0.40, 0.64, 0.92)
$CloudWhite = @(0.97, 0.98, 1.0)
$Trim = @(0.96, 0.95, 0.90)
$Woods = @(@(0.62, 0.40, 0.22), @(0.68, 0.45, 0.25), @(0.57, 0.36, 0.20), @(0.65, 0.43, 0.24))
$DarkWood = @(0.42, 0.25, 0.13)
$Red = @(0.85, 0.12, 0.10); $Blue = @(0.12, 0.32, 0.80); $Yellow = @(0.98, 0.80, 0.10)
$Green = @(0.18, 0.65, 0.25); $Orange = @(0.98, 0.50, 0.08); $Purple = @(0.50, 0.22, 0.70)

$Half = 4.0; $WallH = 3.0; $WallT = 0.15
$DoorY0 = 1.6; $DoorY1 = 2.6; $DoorH = 2.1

# ---- 바닥: 나무 판자 ----
$PlankW = 0.25
$Count = [int](2 * $Half / $PlankW)
for ($i = 0; $i -lt $Count; $i++) {
    $Y0 = -$Half + $i * $PlankW
    $null = Add-Box 'FloorPlank' @(-$Half, ($Y0 + 0.004), -0.1) @($Half, ($Y0 + $PlankW - 0.004), 0.0) $Woods[$i % 4] 20
}
# 판자 사이 줄눈 (어두운 바닥판)
$null = Add-Box 'FloorBase' @(-$Half, -$Half, -0.12) @($Half, $Half, -0.004) $DarkWood

# ---- 벽 ----
$null = Add-Box 'WallWest' @((-$Half - $WallT), -$Half, -0.1) @(-$Half, $Half, $WallH) $WallBlue
$null = Add-Box 'WallSouth' @(-$Half, (-$Half - $WallT), -0.1) @($Half, -$Half, $WallH) $WallBlue
$null = Add-Box 'WallNorth' @(-$Half, $Half, -0.1) @($Half, ($Half + $WallT), $WallH) $WallBlue
$null = Add-Box 'WallEast' @($Half, -$Half, -0.1) @(($Half + $WallT), $DoorY0, $WallH) $WallBlue
$null = Add-Box 'WallEast' @($Half, $DoorY1, -0.1) @(($Half + $WallT), $Half, $WallH) $WallBlue
$null = Add-Box 'WallEastLintel' @($Half, $DoorY0, $DoorH) @(($Half + $WallT), $DoorY1, $WallH) $WallBlue

# 걸레받이
$B = 0.12; $BT = 0.02
$null = Add-Box 'Baseboard' @(-$Half, -$Half, 0) @((-$Half + $BT), $Half, $B) $Trim
$null = Add-Box 'Baseboard' @(-$Half, -$Half, 0) @($Half, (-$Half + $BT), $B) $Trim
$null = Add-Box 'Baseboard' @(-$Half, ($Half - $BT), 0) @($Half, $Half, $B) $Trim
$null = Add-Box 'Baseboard' @(($Half - $BT), -$Half, 0) @($Half, $DoorY0, $B) $Trim
$null = Add-Box 'Baseboard' @(($Half - $BT), $DoorY1, 0) @($Half, $Half, $B) $Trim
# 천장 몰딩
$null = Add-Box 'CrownMolding' @(-$Half, -$Half, ($WallH - 0.08)) @((-$Half + 0.03), $Half, $WallH) $Trim
$null = Add-Box 'CrownMolding' @(-$Half, -$Half, ($WallH - 0.08)) @($Half, (-$Half + 0.03), $WallH) $Trim
$null = Add-Box 'CrownMolding' @(-$Half, ($Half - 0.03), ($WallH - 0.08)) @($Half, $Half, $WallH) $Trim

# ---- 구름 벽지: 납작한 구 여러 개를 벽 표면에 붙인다 ----
$Rng = New-Object System.Random 7
function Add-Cloud($Wall, [double]$U, [double]$Z, [double]$Size) {
    # Wall: 'W','S','N','E' / U: 벽을 따라가는 좌표
    $Puffs = @(@(0, 0, 1.0), @(-0.55, -0.12, 0.72), @(0.55, -0.10, 0.78), @(0.22, 0.22, 0.75), @(-0.25, 0.18, 0.62))
    foreach ($P in $Puffs) {
        $R = $Size * $P[2]
        $UU = $U + $P[0] * $Size; $ZZ = $Z + $P[1] * $Size
        $Depth = 0.035
        switch ($Wall) {
            'W' { $Loc = @(-$Half, $UU, $ZZ); $Scale = @($Depth, ($R * 1.25), ($R * 0.8)) }
            'S' { $Loc = @($UU, -$Half, $ZZ); $Scale = @(($R * 1.25), $Depth, ($R * 0.8)) }
            'N' { $Loc = @($UU, $Half, $ZZ); $Scale = @(($R * 1.25), $Depth, ($R * 0.8)) }
            'E' { $Loc = @($Half, $UU, $ZZ); $Scale = @($Depth, ($R * 1.25), ($R * 0.8)) }
        }
        $null = Add-Shape -Name 'Cloud' -Mesh 'Sphere' -Loc $Loc -Scale $Scale -Color $CloudWhite -Shininess 4 -Collision $false -Shadow $false
    }
}
$CloudSpots = @(
    @('W', -1.0, 2.3, 0.32), @('W', 1.9, 2.5, 0.28), @('W', 0.5, 1.8, 0.22), @('W', 3.0, 1.9, 0.26),
    @('S', -2.8, 2.4, 0.30), @('S', -0.6, 2.0, 0.26), @('S', 1.6, 2.5, 0.33), @('S', 3.2, 1.7, 0.22), @('S', 0.6, 1.3, 0.20),
    @('N', -3.2, 2.5, 0.26), @('N', 1.6, 2.55, 0.24), @('N', 2.9, 2.0, 0.30), @('N', -2.0, 1.3, 0.22),
    @('E', -3.0, 2.3, 0.30), @('E', -1.2, 1.6, 0.26), @('E', 0.4, 2.5, 0.24), @('E', 3.3, 2.0, 0.26)
)
foreach ($C in $CloudSpots) { Add-Cloud $C[0] $C[1] $C[2] $C[3] }

# ---- 문 (동쪽 벽). 루트가 경첩: 닫힌 문은 경첩에서 +Y로 뻗고, Yaw를 늘리면 방 안(-X)으로 열린다 ----
$HingeX = $Half - 0.035
$DoorActor = Add-Shape -Name 'DoorHinge' -Mesh 'Cube' -Loc @($HingeX, $DoorY0, 0) -Scale @(1, 1, 1) -Color @(0.93, 0.90, 0.82) -Shininess 16
$DoorMesh = $DoorActor.Components[1].Properties
$DoorMesh.Transform.Location.X = 0.0; $DoorMesh.Transform.Location.Y = (($DoorY1 - $DoorY0) / 2); $DoorMesh.Transform.Location.Z = ($DoorH / 2)
$DoorMesh.Transform.Scale.X = 0.045; $DoorMesh.Transform.Scale.Y = (($DoorY1 - $DoorY0) - 0.02); $DoorMesh.Transform.Scale.Z = ($DoorH - 0.01)
# 문 장식 패널과 손잡이 (문에 붙여 같이 돈다, 경첩 기준 로컬 좌표)
$null = Add-Shape -Name 'DoorPanel' -Mesh 'Cube' -Loc @(-0.025, 0.5, 1.5) -Scale @(0.012, 0.7, 0.6) -Color @(0.86, 0.82, 0.72) -ParentActor 'DoorHinge'
$null = Add-Shape -Name 'DoorPanel' -Mesh 'Cube' -Loc @(-0.025, 0.5, 0.55) -Scale @(0.012, 0.7, 0.75) -Color @(0.86, 0.82, 0.72) -ParentActor 'DoorHinge'
$null = Add-Shape -Name 'DoorKnob' -Mesh 'Sphere' -Loc @(-0.06, 0.88, 1.0) -Scale @(0.035, 0.035, 0.035) -Color @(0.95, 0.75, 0.25) -Shininess 64 -ParentActor 'DoorHinge'
$null = Add-Shape -Name 'DoorKnob' -Mesh 'Sphere' -Loc @(0.06, 0.88, 1.0) -Scale @(0.035, 0.035, 0.035) -Color @(0.95, 0.75, 0.25) -Shininess 64 -ParentActor 'DoorHinge'
# 문틀
$null = Add-Box 'DoorFrame' @(($Half - 0.03), ($DoorY0 - 0.08), 0) @(($Half + $WallT + 0.03), $DoorY0, ($DoorH + 0.08)) $Trim
$null = Add-Box 'DoorFrame' @(($Half - 0.03), $DoorY1, 0) @(($Half + $WallT + 0.03), ($DoorY1 + 0.08), ($DoorH + 0.08)) $Trim
$null = Add-Box 'DoorFrame' @(($Half - 0.03), ($DoorY0 - 0.08), $DoorH) @(($Half + $WallT + 0.03), ($DoorY1 + 0.08), ($DoorH + 0.08)) $Trim

# ---- 문 밖 복도 ----
$HallX1 = $Half + 4.0
$null = Add-Box 'HallFloor' @(($Half + $WallT), 0.6, -0.1) @($HallX1, 3.6, 0.0) @(0.55, 0.36, 0.22)
$null = Add-Box 'HallWall' @(($Half + $WallT), 0.45, -0.1) @($HallX1, 0.6, $WallH) @(0.93, 0.87, 0.74)
$null = Add-Box 'HallWall' @(($Half + $WallT), 3.6, -0.1) @($HallX1, 3.75, $WallH) @(0.93, 0.87, 0.74)
$null = Add-Box 'HallWall' @($HallX1, 0.45, -0.1) @(($HallX1 + 0.15), 3.75, $WallH) @(0.90, 0.84, 0.70)
$null = Add-Box 'HallCeiling' @(($Half + $WallT), 0.45, $WallH) @(($HallX1 + 0.15), 3.75, ($WallH + 0.1)) @(0.95, 0.93, 0.88)

# ---- 둥근 러그 (우디가 도는 곳) ----
$RugC = @(-0.2, 0.8)
$null = Add-Shape -Name 'RugOuter' -Mesh 'Cylinder' -Loc @($RugC[0], $RugC[1], 0.004) -Scale @(1.75, 1.75, 0.008) -Color @(0.15, 0.30, 0.62) -Shininess 2
$null = Add-Shape -Name 'RugRing' -Mesh 'Cylinder' -Loc @($RugC[0], $RugC[1], 0.0085) -Scale @(1.45, 1.45, 0.009) -Color @(0.92, 0.78, 0.22) -Shininess 2
$null = Add-Shape -Name 'RugRing' -Mesh 'Cylinder' -Loc @($RugC[0], $RugC[1], 0.0095) -Scale @(1.3, 1.3, 0.011) -Color @(0.75, 0.18, 0.14) -Shininess 2
$null = Add-Shape -Name 'RugCenter' -Mesh 'Cylinder' -Loc @($RugC[0], $RugC[1], 0.0105) -Scale @(1.05, 1.05, 0.012) -Color @(0.20, 0.42, 0.75) -Shininess 2

# ---- 침대 (서쪽 벽, 남쪽 구석) ----
$BX0 = -3.95; $BX1 = -2.0; $BY0 = -3.9; $BY1 = -2.75
$null = Add-Box 'BedFrame' @($BX0, $BY0, 0.12) @($BX1, $BY1, 0.40) $Woods[0] 20
foreach ($LX in @(($BX0 + 0.03), ($BX1 - 0.09))) { foreach ($LY in @(($BY0 + 0.03), ($BY1 - 0.09))) {
    $null = Add-Box 'BedLeg' @($LX, $LY, 0) @(($LX + 0.06), ($LY + 0.06), 0.12) $DarkWood } }
$null = Add-Box 'BedHeadboard' @(($BX0 - 0.02), ($BY0 - 0.02), 0) @(($BX0 + 0.07), ($BY1 + 0.02), 1.05) $Woods[2] 20
$null = Add-Box 'BedFootboard' @(($BX1 - 0.07), ($BY0 - 0.02), 0) @(($BX1 + 0.02), ($BY1 + 0.02), 0.65) $Woods[2] 20
$null = Add-Shape -Name 'BedPost' -Mesh 'Sphere' -Loc @(($BX1 - 0.025), ($BY0 - 0.0), 0.68) -Scale @(0.05, 0.05, 0.05) -Color $Woods[1]
$null = Add-Shape -Name 'BedPost' -Mesh 'Sphere' -Loc @(($BX1 - 0.025), ($BY1 + 0.0), 0.68) -Scale @(0.05, 0.05, 0.05) -Color $Woods[1]
$null = Add-Box 'Mattress' @(($BX0 + 0.07), ($BY0 + 0.02), 0.40) @(($BX1 - 0.07), ($BY1 - 0.02), 0.56) $Trim 4
$null = Add-Box 'Blanket' @(($BX0 + 0.55), ($BY0 - 0.01), 0.40) @(($BX1 - 0.06), ($BY1 + 0.01), 0.59) $Red 6
$null = Add-Box 'BlanketStripe' @(($BX0 + 0.85), ($BY0 - 0.015), 0.40) @(($BX0 + 1.0), ($BY1 + 0.015), 0.595) $Yellow 6
$null = Add-Box 'BlanketStripe' @(($BX0 + 1.25), ($BY0 - 0.015), 0.40) @(($BX0 + 1.4), ($BY1 + 0.015), 0.595) $Yellow 6
$null = Add-Shape -Name 'Pillow' -Mesh 'Sphere' -Loc @(($BX0 + 0.3), (($BY0 + $BY1) / 2), 0.62) -Scale @(0.18, 0.4, 0.08) -Color @(0.98, 0.98, 0.95) -Shininess 4

# ---- 장난감 상자 (남쪽 벽) ----
$null = Add-Box 'ToyChest' @(-1.4, -3.95, 0) @(-0.3, -3.35, 0.55) @(0.20, 0.38, 0.72) 16
$null = Add-Box 'ToyChestLid' @(-1.43, -3.97, 0.55) @(-0.27, -3.32, 0.62) @(0.16, 0.30, 0.60) 16
$null = Add-Box 'ToyChestTrim' @(-1.42, -3.36, 0.22) @(-0.28, -3.33, 0.30) $Yellow
$null = Add-Shape -Name 'ToyChestStar' -Mesh 'Cylinder' -Loc @(-0.85, -3.33, 0.38) -Scale @(0.09, 0.09, 0.01) -Rot @(0, 0, 90) -Color $Yellow

# ---- 책상과 의자, 스탠드 (북쪽 벽) ----
$DX0 = -0.9; $DX1 = 0.7; $DY0 = 3.3; $DY1 = 3.95
$null = Add-Box 'DeskTop' @($DX0, $DY0, 0.72) @($DX1, $DY1, 0.76) $Woods[1] 24
foreach ($LX in @(($DX0 + 0.03), ($DX1 - 0.08))) { foreach ($LY in @(($DY0 + 0.03), ($DY1 - 0.08))) {
    $null = Add-Box 'DeskLeg' @($LX, $LY, 0) @(($LX + 0.05), ($LY + 0.05), 0.72) $Woods[2] } }
$null = Add-Box 'DeskDrawer' @(($DX1 - 0.5), $DY0, 0.5) @(($DX1 - 0.05), ($DY1 - 0.05), 0.72) $Woods[3] 20
$null = Add-Shape -Name 'DrawerKnob' -Mesh 'Sphere' -Loc @(($DX1 - 0.275), ($DY0 - 0.015), 0.61) -Scale @(0.02, 0.02, 0.02) -Color $Yellow
$null = Add-Box 'ChairSeat' @(-0.35, 2.75, 0.42) @(0.1, 3.2, 0.47) $Red 20
foreach ($LX in @(-0.33, 0.06)) { foreach ($LY in @(2.77, 3.16)) { $null = Add-Box 'ChairLeg' @($LX, $LY, 0) @(($LX + 0.03), ($LY + 0.03), 0.42) $Red } }
$null = Add-Box 'ChairBack' @(-0.35, 2.73, 0.47) @(0.1, 2.78, 0.9) $Red 20
$null = Add-Shape -Name 'LampBase' -Mesh 'Cylinder' -Loc @(0.45, 3.75, 0.775) -Scale @(0.08, 0.08, 0.03) -Color $Green 32
$null = Add-Shape -Name 'LampPole' -Mesh 'Cylinder' -Loc @(0.45, 3.75, 0.95) -Scale @(0.012, 0.012, 0.35) -Color @(0.8, 0.8, 0.8) 32
$null = Add-Shape -Name 'LampShade' -Mesh 'Cone' -Loc @(0.45, 3.75, 1.05) -Scale @(0.13, 0.13, 0.16) -Color $Green 32
$null = Add-Shape -Name 'LampBulb' -Mesh 'Sphere' -Loc @(0.45, 3.75, 1.06) -Scale @(0.04, 0.04, 0.04) -Color @(1.0, 0.95, 0.75) -Shading 'Unlit' -Shadow $false
# 책상 위 소품: 연필꽂이와 책
$null = Add-Shape -Name 'PencilCup' -Mesh 'Cylinder' -Loc @(-0.6, 3.75, 0.82) -Scale @(0.04, 0.04, 0.12) -Color $Blue
foreach ($k in 0..2) { $null = Add-Shape -Name 'Pencil' -Mesh 'Cylinder' -Loc @((-0.61 + 0.01 * $k), (3.75 + 0.01 * ($k - 1)), 0.9) -Scale @(0.006, 0.006, 0.2) -Rot @(0, 0, (8 * ($k - 1))) -Color @($Red, $Yellow, $Green)[$k] }
$null = Add-Box 'DeskBook' @(-0.35, 3.55, 0.76) @(0.05, 3.85, 0.8) $Purple
$null = Add-Box 'DeskBook' @(-0.32, 3.57, 0.8) @(0.02, 3.83, 0.835) $Orange

# ---- 창문 (북쪽 벽, 책상 위) ----
$null = Add-Box 'WindowSky' @(-0.75, ($Half - 0.01), 1.25) @(0.55, $Half, 2.35) @(0.72, 0.86, 1.0)
$WindowSky = $Actors[$Actors.Count - 1]; $WindowSky.Components[1].OverrideMaterials = @(New-ColorMaterial @(0.75, 0.88, 1.0) 1 'Unlit')
$null = Add-Box 'WindowFrame' @(-0.82, ($Half - 0.05), 1.18) @(0.62, $Half, 1.25) $Trim
$null = Add-Box 'WindowFrame' @(-0.82, ($Half - 0.05), 2.35) @(0.62, $Half, 2.42) $Trim
$null = Add-Box 'WindowFrame' @(-0.82, ($Half - 0.05), 1.18) @(-0.75, $Half, 2.42) $Trim
$null = Add-Box 'WindowFrame' @(0.55, ($Half - 0.05), 1.18) @(0.62, $Half, 2.42) $Trim
$null = Add-Box 'WindowFrame' @(-0.13, ($Half - 0.04), 1.25) @(-0.07, $Half, 2.35) $Trim
$null = Add-Box 'WindowFrame' @(-0.75, ($Half - 0.04), 1.77) @(0.55, $Half, 1.83) $Trim
$null = Add-Box 'WindowSill' @(-0.9, ($Half - 0.12), 1.13) @(0.7, $Half, 1.18) $Trim
$null = Add-Box 'Curtain' @(-1.15, ($Half - 0.08), 1.0) @(-0.85, ($Half - 0.03), 2.6) @(0.98, 0.85, 0.30) 4
$null = Add-Box 'Curtain' @(0.65, ($Half - 0.08), 1.0) @(0.95, ($Half - 0.03), 2.6) @(0.98, 0.85, 0.30) 4
$null = Add-Shape -Name 'CurtainRod' -Mesh 'Cylinder' -Loc @(-0.1, ($Half - 0.06), 2.62) -Scale @(0.015, 0.015, 2.2) -Rot @(90, 0, 0) -Color $DarkWood

# ---- 책장 (북쪽 벽, 서쪽) ----
$SX0 = -3.9; $SX1 = -2.7; $SY0 = 3.6; $SY1 = 3.95; $SH = 1.7
$null = Add-Box 'ShelfSide' @($SX0, $SY0, 0) @(($SX0 + 0.04), $SY1, $SH) $Woods[0]
$null = Add-Box 'ShelfSide' @(($SX1 - 0.04), $SY0, 0) @($SX1, $SY1, $SH) $Woods[0]
$null = Add-Box 'ShelfBack' @($SX0, ($SY1 - 0.02), 0) @($SX1, $SY1, $SH) $Woods[2]
$BookColors = @($Red, $Blue, $Yellow, $Green, $Orange, $Purple, @(0.9, 0.9, 0.85))
foreach ($s in 0..3) {
    $Z = 0.02 + $s * 0.42
    $null = Add-Box 'Shelf' @($SX0, $SY0, $Z) @($SX1, $SY1, ($Z + 0.03)) $Woods[1]
    if ($s -eq 3) { continue }
    $X = $SX0 + 0.06
    $b = 0
    while ($X -lt ($SX1 - 0.12)) {
        $W = 0.04 + 0.04 * $Rng.NextDouble(); $H = 0.22 + 0.12 * $Rng.NextDouble()
        $null = Add-Box 'Book' @($X, ($SY0 + 0.04), ($Z + 0.03)) @(($X + $W), ($SY1 - 0.03), ($Z + 0.03 + $H)) $BookColors[($s * 3 + $b) % $BookColors.Count] 10
        $X += $W + 0.006; $b++
    }
}
$null = Add-Box 'ShelfTop' @(($SX0 - 0.02), ($SY0 - 0.02), $SH) @(($SX1 + 0.02), $SY1, ($SH + 0.04)) $Woods[1]
# 책장 위 저금통 돼지
$PigC = @(-3.3, 3.78, ($SH + 0.04))
$Pink = @(0.98, 0.62, 0.70)
$null = Add-Shape -Name 'PiggyBody' -Mesh 'Sphere' -Loc @($PigC[0], $PigC[1], ($PigC[2] + 0.13)) -Scale @(0.17, 0.12, 0.11) -Color $Pink 40
$null = Add-Shape -Name 'PiggySnout' -Mesh 'Cylinder' -Loc @(($PigC[0] + 0.17), $PigC[1], ($PigC[2] + 0.14)) -Scale @(0.04, 0.04, 0.03) -Rot @(90, 0, 0) -Color @(0.95, 0.52, 0.62) 40
foreach ($LX in @(-0.08, 0.08)) { foreach ($LY in @(-0.06, 0.06)) {
    $null = Add-Shape -Name 'PiggyLeg' -Mesh 'Cylinder' -Loc @(($PigC[0] + $LX), ($PigC[1] + $LY), ($PigC[2] + 0.03)) -Scale @(0.025, 0.025, 0.06) -Color $Pink 40 } }

# ---- 장난감들 ----
# 큰 공: 노란 공에 파란 띠, 빨간 별
$BallC = @(2.5, -2.7, 0.24)
$null = Add-Shape -Name 'Ball' -Mesh 'Sphere' -Loc $BallC -Scale @(0.24, 0.24, 0.24) -Color $Yellow 48
$null = Add-Shape -Name 'BallStripe' -Mesh 'Torus' -Loc $BallC -Scale @(0.215, 0.215, 0.12) -Rot @(0, 0, 20) -Color $Blue 48
$null = Add-Shape -Name 'BallStar' -Mesh 'Cylinder' -Loc @(($BallC[0] - 0.06), ($BallC[1] + 0.12), ($BallC[2] + 0.18)) -Scale @(0.07, 0.07, 0.03) -Rot @(-30, 0, 30) -Color $Red 48

# 글자 블록 피라미드 (남동쪽)
$Blk = 0.16
$BlockColors = @($Red, $Blue, $Green, $Yellow, $Orange, $Purple)
$Bi = 0
$PyrC = @(1.4, -3.3)
foreach ($Row in 0..2) {
    $N = 3 - $Row
    for ($k = 0; $k -lt $N; $k++) {
        $X = $PyrC[0] + ($k - ($N - 1) / 2.0) * ($Blk + 0.005)
        $Z = $Row * $Blk + $Blk / 2
        $null = Add-Shape -Name 'Block' -Mesh 'Cube' -Loc @($X, $PyrC[1], $Z) -Scale @($Blk, $Blk, $Blk) -Rot @(0, (($k * 7 + $Row * 5) % 9 - 4), 0) -Color $BlockColors[$Bi % 6] 16
        # 블록 앞면 글자 판
        $null = Add-Shape -Name 'BlockLetter' -Mesh 'Cube' -Loc @($X, ($PyrC[1] + $Blk / 2), $Z) -Scale @(($Blk * 0.62), 0.006, ($Blk * 0.62)) -Color @(0.98, 0.96, 0.88) -Collision $false
        $Bi++
    }
}
# 흩어진 블록
foreach ($S in @(@(2.0, -2.1, 25), @(-2.2, 2.4, 40), @(2.7, 1.0, 10), @(-2.6, -1.6, 55))) {
    $null = Add-Shape -Name 'Block' -Mesh 'Cube' -Loc @($S[0], $S[1], ($Blk / 2)) -Scale @($Blk, $Blk, $Blk) -Rot @(0, $S[2], 0) -Color $BlockColors[$Bi % 6] 16
    $Bi++
}

# 쌓기 고리 장난감 (북서쪽)
$RingC = @(-2.4, 2.9)
$null = Add-Shape -Name 'RingBase' -Mesh 'Cylinder' -Loc @($RingC[0], $RingC[1], 0.03) -Scale @(0.17, 0.17, 0.06) -Color $Red 32
$null = Add-Shape -Name 'RingPole' -Mesh 'Cylinder' -Loc @($RingC[0], $RingC[1], 0.28) -Scale @(0.03, 0.03, 0.46) -Color $Yellow 32
$null = Add-Shape -Name 'RingTop' -Mesh 'Sphere' -Loc @($RingC[0], $RingC[1], 0.53) -Scale @(0.06, 0.06, 0.06) -Color $Red 32
$RingColors = @($Purple, $Blue, $Green, $Yellow, $Orange)
for ($k = 0; $k -lt 5; $k++) {
    $R = 0.15 - $k * 0.018
    $null = Add-Shape -Name 'StackRing' -Mesh 'Torus' -Loc @($RingC[0], $RingC[1], (0.095 + $k * 0.075)) -Scale @($R, $R, ($R * 1.1)) -Color $RingColors[$k] 40
}

# 장난감 자동차
$CarC = @(2.2, 0.0)
$null = Add-Box 'CarBody' @(($CarC[0] - 0.2), ($CarC[1] - 0.09), 0.05) @(($CarC[0] + 0.2), ($CarC[1] + 0.09), 0.14) $Red 48
$null = Add-Box 'CarCabin' @(($CarC[0] - 0.1), ($CarC[1] - 0.08), 0.14) @(($CarC[0] + 0.07), ($CarC[1] + 0.08), 0.22) @(0.7, 0.85, 0.95) 64
foreach ($WX in @(-0.13, 0.13)) { foreach ($WY in @(-0.095, 0.095)) {
    $null = Add-Shape -Name 'CarWheel' -Mesh 'Cylinder' -Loc @(($CarC[0] + $WX), ($CarC[1] + $WY), 0.05) -Scale @(0.05, 0.05, 0.03) -Rot @(0, 0, 90) -Color @(0.1, 0.1, 0.1) 8 } }

# 크레용
$CrayonColors = @($Red, $Blue, $Green, $Yellow, $Purple, $Orange)
for ($k = 0; $k -lt 6; $k++) {
    $null = Add-Shape -Name 'Crayon' -Mesh 'Cylinder' -Loc @((1.0 + 0.05 * $k), (2.75 + 0.03 * ($k % 2)), 0.012) -Scale @(0.012, 0.012, 0.11) -Rot @(90, (75 + 9 * $k), 0) -Color $CrayonColors[$k] 20
}

# 벽 장식: 별 모양 액자(노란 판 + 빨간 원)와 삼각 깃발
$null = Add-Box 'Poster' @(1.4, (-$Half + 0.0), 1.2) @(2.4, (-$Half + 0.015), 2.0) $Yellow 4
$null = Add-Shape -Name 'PosterStar' -Mesh 'Cylinder' -Loc @(1.9, (-$Half + 0.02), 1.6) -Scale @(0.26, 0.26, 0.01) -Rot @(0, 0, 90) -Color $Red 4
for ($k = 0; $k -lt 7; $k++) {
    $null = Add-Shape -Name 'Pennant' -Mesh 'Cone' -Loc @((-$Half + 0.02), (-2.0 + $k * 0.45), 2.55) -Scale @(0.15, 0.015, 0.28) -Rot @(180, 0, 0) -Color @($Red, $Yellow, $Blue, $Green)[$k % 4] 4 -Collision $false
}
$null = Add-Shape -Name 'PennantString' -Mesh 'Cylinder' -Loc @((-$Half + 0.02), -0.65, 2.56) -Scale @(0.006, 0.006, 3.2) -Rot @(0, 0, 90) -Color $Trim -Collision $false

# ---- 조명 ----
$Ambient = Copy-Json $TplAmbient
$Ambient.Name = 'AmbientLight'
($Ambient.Components | Where-Object { $_.Class -eq 'UAmbientLightComponent' }).Properties.Intensity = 0.8
[void]$Actors.Add($Ambient)

$Sun = Copy-Json $TplSun
$Sun.Name = 'SunLight'
$SunComp = $Sun.Components | Where-Object { $_.Class -eq 'UDirectionalLightComponent' }
$SunComp.Properties.Intensity = 1.1
$SunComp.Properties.LightColor = 4290040063   # 따뜻한 햇빛 (R255 G236 B180)
$SunComp.Properties.DynamicShadowDistanceMovableLight = 30.0
$SunComp.Properties.Transform.Rotation.Pitch = 62.0
$SunComp.Properties.Transform.Rotation.Yaw = -35.0
$SunComp.Properties.Transform.Rotation.Roll = 0.0
Set-Transform $Sun.Components[0].Properties @(0, 0, 6) @(0, 0, 0) @(1, 1, 1)
[void]$Actors.Add($Sun)

function Add-PointLight($Name, $Loc, [double]$Intensity, [double]$Radius, [uint32]$Color) {
    $Light = Copy-Json $TplPoint
    $Light.Name = Get-UniqueName $Name
    Set-Transform $Light.Components[0].Properties $Loc @(0, 0, 0) @(1, 1, 1)
    $Comp = $Light.Components | Where-Object { $_.Class -eq 'UPointLightComponent' }
    $Comp.Properties.Intensity = $Intensity
    $Comp.Properties.AttenuationRadius = $Radius
    $Comp.Properties.LightColor = $Color
    Set-Transform $Comp.Properties @(0, 0, 0) @(0, 0, 0) @(1, 1, 1)
    [void]$Actors.Add($Light)
}
$Warm = [uint32]4289386239     # R255 G214 B170
Add-PointLight 'RoomLight' @(0.0, 0.0, 2.8) 3.0 9.0 $Warm
Add-PointLight 'LampLight' @(0.45, 3.6, 0.95) 1.6 2.5 $Warm
Add-PointLight 'HallLight' @(5.6, 2.1, 2.6) 3.0 5.0 ([uint32]4292927743)

# ---- 캐릭터 ----
function New-SkeletalComponent($MeshPath) {
    $Comp = Copy-Json $TplSkel
    $Comp.Properties.SkeletalMesh = $MeshPath
    $Comp.Properties.BodyInstance.bSimulatePhysics = $false
    $Comp.Properties.BodyInstance.Mobility = 'Movable'
    $Comp.Parent = 'DefaultSceneRoot'
    $Comp.BonePose = @()
    # Transform은 넣지 않는다: 액터 생성자가 스켈레톤 앞 방향과 MeshScale로 정한다
    $Comp.Properties.PSObject.Properties.Remove('Transform')
    return $Comp
}
function Add-Character($Class, $Name, $Loc, [double]$Yaw, $MeshPath, $Extra) {
    $Root = Copy-Json $TplMesh.Components[0]
    Set-Transform $Root.Properties $Loc @(0, $Yaw, 0) @(1, 1, 1)
    $Components = @($Root)
    if ($MeshPath) { $Components += New-SkeletalComponent $MeshPath }
    $Properties = [ordered]@{ ScriptPath = ''; bActorEnableCollision = $true }
    if ($Extra) { foreach ($K in $Extra.Keys) { $Properties[$K] = $Extra[$K] } }
    $Actor = [ordered]@{
        Class = $Class; Components = $Components; Name = $Name; ParentActor = ''; ParentComponent = ''
        Properties = $Properties; RootComponentName = 'DefaultSceneRoot'
    }
    [void]$Actors.Add($Actor)
}

Add-Character 'AWoodyToyActor' 'Woody' @(-0.2, 0.0, 0.012) 0.0 'FBX/woody_high/woody_high.fbx.bin' @{
    MeshScale = 0.003; WanderRadius = 1.0; SpeedScale = 1.0; TurnRate = 110.0; NoticeDistance = 8.0; LimpMomentum = 0.8; bRegenerateAnimations = $false }
Add-Character 'AToyRoomHumanActor' 'Human' @(5.6, 2.1, 0.0) 180.0 'FBX/All_Might/All_Might.fbx.bin' @{
    MeshScale = 0.003; EnterDelay = 9.0; DoorOpenSeconds = 1.4; DoorOpenAngle = 95.0; DoorActorName = 'DoorHinge'
    EntryPoint = [ordered]@{ X = 3.0; Y = 2.1; Z = 0.0 }; StopDistance = 0.9; SpeedScale = 1.0; bRegenerateAnimations = $false }

# 카메라 연출 (씬의 첫 Pawn → PlayerController가 빙의)
$CamRoot = Copy-Json $TplMesh.Components[0]
Set-Transform $CamRoot.Properties @(1.2, -1.2, 0.5) @(0, 135, 0) @(1, 1, 1)
$Cam = [ordered]@{
    Class = 'UCameraComponent'; DefaultSubobjectName = 'CameraComponent'; Name = 'CameraComponent'; Parent = 'DefaultSceneRoot'; Properties = [ordered]@{}
}
[void]$Actors.Add([ordered]@{
    Class = 'AToyRoomDirector'; Components = @($CamRoot, $Cam); Name = 'CameraDirector'; ParentActor = ''; ParentComponent = ''
    Properties = [ordered]@{ ScriptPath = ''; bActorEnableCollision = $false; CameraSmoothing = 2.2; OrbitRadius = 1.5; OrbitHeight = 0.45; OrbitSpeed = 12.0; FieldOfView = 55.0 }
    RootComponentName = 'DefaultSceneRoot'
})

# ---- 저장 ----
$Scene = [ordered]@{
    Version = 4
    World = [ordered]@{
        PersistentLevel = [ordered]@{ Actors = $Actors.ToArray(); Properties = [ordered]@{} }
        Properties = [ordered]@{}
    }
}
$Json = $Scene | ConvertTo-Json -Depth 100
[IO.File]::WriteAllText($OutPath, $Json, (New-Object Text.UTF8Encoding $false))
Write-Host ("Wrote {0} actors to {1}" -f $Actors.Count, $OutPath)
