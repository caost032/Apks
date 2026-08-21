import 'dart:async';
import 'dart:ui' as ui;
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../engine/game_runtime.dart';
import '../engine/game_snapshot.dart';
import '../input/input_router.dart';
import '../native/odg_bindings.dart';
import '../render/raster_budget.dart';
import '../platform/android_host.dart';
import '../profile/game_profile.dart';
import '../skins/avatar_skin_service.dart';
import 'design_system.dart';
import 'game/hotbar.dart';
import 'controls/control_editor.dart';
import 'crafting/station_panel.dart';
import 'map/domain_map.dart';
import 'music/music_panel.dart';
import 'inventory/inventory_panel.dart';

final class GameScreen extends StatefulWidget {
  const GameScreen({
    required this.runtime,
    required this.input,
    super.key,
  });

  final GameRuntime runtime;
  final MultiTouchInputRouter input;

  @override
  State<GameScreen> createState() => _GameScreenState();
}

final class _GameScreenState extends State<GameScreen>
    with WidgetsBindingObserver {
  late final Listenable _animation;
  late final AvatarSkinService _skins;
  final AndroidHost _android = AndroidHost.instance;
  GameProfile _profile = const GameProfile();
  Timer? _autosaveTimer;
  bool _menuOpen = true;
  bool _settingsOpen = false;
  bool _inventoryOpen = false;
  bool _controlEditorOpen = false;
  List<WorldSlot> _worlds = const <WorldSlot>[];
  WorldSlot? _activeWorld;
  String _menuPage = 'root';
  String? _worldError;
  bool _hostStateReady = false;
  bool _gameMusicPauseApplied = false;
  String _settingsPage = 'root';
  bool _moveOnLeft = true;
  int _theme = odgVisualThemeNeonTides;
  Size? _queuedSize;
  EdgeInsets? _queuedPadding;
  bool _layoutCallbackScheduled = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _animation = Listenable.merge(<Listenable>[widget.runtime, widget.input]);
    _skins = AvatarSkinService(widget.runtime);
    widget.runtime.setTheme(_theme);
    unawaited(_restoreHostState());
    _autosaveTimer = Timer.periodic(const Duration(seconds: 30), (_) {
      if (widget.runtime.started && !_menuOpen) unawaited(_saveWorld());
    });
  }

  Future<void> _restoreHostState() async {
    final GameProfile loaded = GameProfile.decode(await _android.loadSettings());
    widget.runtime.setCameraMode(loaded.cameraMode);
    widget.runtime.setMusicReactivity(loaded.musicReactivity);
    widget.input.setLookSensitivity(loaded.cameraSensitivity);
    await _skins.restore();
    List<WorldSlot> worlds = const <WorldSlot>[];
    String? error;
    try {
      worlds = await _android.listWorlds();
    } catch (e) {
      error = 'No se pudo leer el catálogo de mundos: $e';
    }
    if (!mounted) return;
    setState(() {
      _profile = loaded;
      _worlds = worlds;
      _worldError = error;
      _hostStateReady = true;
    });
  }

  Future<void> _persistProfile() async {
    await _android.saveSettings(_profile.encode());
  }

  Future<bool> _saveWorld({bool refreshCatalog = false}) async {
    final WorldSlot? world = _activeWorld;
    if (!widget.runtime.started || world == null) return false;
    try {
      final int apiVersion = widget.runtime.api.apiVersion();
      final int ffiAbiVersion = odgFfiAbiVersion;
      final int saveSchemaVersion = widget.runtime.api.saveSchemaVersion();
      await _android.saveWorldSlot(
        world: world,
        bytes: widget.runtime.saveBlob(),
        apiVersion: apiVersion,
        ffiAbiVersion: ffiAbiVersion,
        saveSchemaVersion: saveSchemaVersion,
      );
      if (_activeWorld?.id == world.id) {
        _activeWorld = world.copyWith(
          updatedAtMs: DateTime.now().millisecondsSinceEpoch,
          apiVersion: apiVersion,
          ffiAbiVersion: ffiAbiVersion,
          saveSchemaVersion: saveSchemaVersion,
          corrupt: false,
        );
      }
      if (refreshCatalog) {
        final List<WorldSlot> worlds = await _android.listWorlds();
        if (mounted) setState(() => _worlds = worlds);
      }
      return true;
    } catch (e) {
      if (mounted) setState(() => _worldError = 'No se pudo guardar “${world.name}”: $e');
      return false;
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    widget.runtime.setLifecycleActive(state == AppLifecycleState.resumed);
    if (state != AppLifecycleState.resumed) unawaited(_saveWorld());
  }

  @override
  void dispose() {
    _autosaveTimer?.cancel();
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  void _queueLayout(Size size, EdgeInsets padding) {
    if (_queuedSize == size && _queuedPadding == padding) return;
    _queuedSize = size;
    _queuedPadding = padding;
    if (_layoutCallbackScheduled) return;
    _layoutCallbackScheduled = true;
    SchedulerBinding.instance.addPostFrameCallback((Duration _) {
      _layoutCallbackScheduled = false;
      if (!mounted) return;
      final Size? queuedSize = _queuedSize;
      final EdgeInsets? queuedPadding = _queuedPadding;
      if (queuedSize == null || queuedPadding == null) return;
      widget.input.updateLayout(
        ControlLayout.forViewport(
          queuedSize,
          queuedPadding,
          moveOnLeft: _moveOnLeft,
          profile: _profile.controlsFor(queuedSize.width > queuedSize.height),
        ),
      );
      widget.runtime.setViewport(queuedSize);
    });
  }

  void _pauseGameMusicIfNeeded() {
    if (!_profile.pauseMusicWithGame || _gameMusicPauseApplied) return;
    _gameMusicPauseApplied = true;
    unawaited(_android.musicCommand('gamePause'));
  }

  void _resumeGameMusicIfNeeded() {
    if (!_gameMusicPauseApplied) return;
    _gameMusicPauseApplied = false;
    unawaited(_android.musicCommand('gameResume'));
  }

  void _setPauseMusicWithGame(bool value) {
    setState(() => _profile = _profile.copyWith(pauseMusicWithGame: value));
    if (_settingsOpen) {
      if (value) _pauseGameMusicIfNeeded();
      else _resumeGameMusicIfNeeded();
    }
    unawaited(_persistProfile());
  }

  String _newWorldId(int seed) {
    final int now = DateTime.now().microsecondsSinceEpoch;
    final int tail = seed & 0xFFFFF;
    return 'w_${now.toRadixString(36)}_${tail.toRadixString(36)}';
  }

  Future<void> _createWorld(String rawName, String rawSeed) async {
    final String name = rawName.trim().isEmpty ? 'Nuevo mundo' : rawName.trim();
    final int? requestedSeed = int.tryParse(rawSeed.trim());
    final int seed = (requestedSeed == null || requestedSeed == 0)
        ? widget.runtime.generateWorldSeed()
        : requestedSeed.abs();
    final int now = DateTime.now().millisecondsSinceEpoch;
    final WorldSlot world = WorldSlot(
      id: _newWorldId(seed),
      name: name.length > 48 ? name.substring(0, 48) : name,
      seed: seed,
      createdAtMs: now,
      updatedAtMs: now,
      apiVersion: widget.runtime.api.apiVersion(),
      ffiAbiVersion: odgFfiAbiVersion,
      saveSchemaVersion: widget.runtime.api.saveSchemaVersion(),
    );
    _resumeGameMusicIfNeeded();
    widget.runtime.setHostPaused(false);
    widget.runtime.startNewWorld(seed);
    if (!mounted) return;
    setState(() {
      _activeWorld = world;
      _settingsOpen = false;
      _inventoryOpen = false;
      _controlEditorOpen = false;
      _menuOpen = false;
      _worldError = null;
    });
    await _saveWorld(refreshCatalog: true);
  }

  Future<void> _continueWorld(WorldSlot world) async {
    final int api = widget.runtime.api.apiVersion();
    final int saveSchema = widget.runtime.api.saveSchemaVersion();
    final bool schemaSupported = widget.runtime.api.saveSchemaSupported(world.saveSchemaVersion) != 0;
    if (!world.structurallyLoadable || !schemaSupported) {
      if (mounted) setState(() {
        _worldError = world.legacy
            ? '“${world.name}” es un save legado. Se conserva intacto; necesita una migración explícita antes de abrirlo.'
            : world.corrupt
                ? '“${world.name}” no pasó la verificación de integridad. Se conserva intacto.'
                : '“${world.name}” usa save ${world.saveSchemaVersion}; este motor (API $api) soporta actualmente save $saveSchema. API/ABI del creador (${world.apiVersion}/${world.ffiAbiVersion}) son solo procedencia y no bloquean el mundo. No se modificó.';
      });
      return;
    }
    try {
      final Uint8List? bytes = await _android.loadWorldSlot(world.id);
      if (bytes == null || bytes.isEmpty) throw StateError('save ausente o vacío');
      widget.runtime.loadBlob(bytes);
      widget.runtime.setCameraMode(_profile.cameraMode);
      widget.runtime.setMusicReactivity(_profile.musicReactivity);
      _resumeGameMusicIfNeeded();
      widget.runtime.setHostPaused(false);
      widget.runtime.resumeLoadedWorld();
      if (mounted) setState(() {
        _activeWorld = world;
        _settingsOpen = false;
        _inventoryOpen = false;
        _controlEditorOpen = false;
        _menuOpen = false;
        _worldError = null;
      });
    } catch (e) {
      if (mounted) setState(() => _worldError = 'No se pudo abrir “${world.name}”. El save se conserva intacto: $e');
    }
  }

  Future<void> _deleteWorld(WorldSlot world) async {
    final bool confirmed = await showDialog<bool>(
      context: context,
      builder: (BuildContext context) => AlertDialog(
        title: const Text('ELIMINAR MUNDO'),
        content: Text('¿Eliminar “${world.name}”? Esta acción borra su save local.'),
        actions: <Widget>[
          TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('CANCELAR')),
          TextButton(onPressed: () => Navigator.pop(context, true), child: const Text('ELIMINAR')),
        ],
      ),
    ) ?? false;
    if (!confirmed) return;
    try {
      await _android.deleteWorldSlot(world.id);
      final List<WorldSlot> worlds = await _android.listWorlds();
      if (!mounted) return;
      setState(() {
        _worlds = worlds;
        if (_activeWorld?.id == world.id) _activeWorld = null;
        _worldError = null;
      });
    } catch (e) {
      if (mounted) setState(() => _worldError = 'No se pudo eliminar “${world.name}”: $e');
    }
  }

  Future<void> _showCreateWorldDialog() async {
    final TextEditingController name = TextEditingController(text: 'Nuevo mundo');
    final TextEditingController seed = TextEditingController();
    final bool create = await showDialog<bool>(
      context: context,
      builder: (BuildContext context) => AlertDialog(
        title: const Text('CREAR MUNDO'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: <Widget>[
            TextField(controller: name, maxLength: 48, decoration: const InputDecoration(labelText: 'Nombre del mundo')),
            TextField(
              controller: seed,
              keyboardType: TextInputType.number,
              decoration: const InputDecoration(labelText: 'Seed (opcional)', hintText: 'Vacío = aleatoria'),
            ),
          ],
        ),
        actions: <Widget>[
          TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('CANCELAR')),
          TextButton(onPressed: () => Navigator.pop(context, true), child: const Text('CREAR')),
        ],
      ),
    ) ?? false;
    final String nameValue = name.text;
    final String seedValue = seed.text;
    name.dispose();
    seed.dispose();
    if (create) await _createWorld(nameValue, seedValue);
  }

  void _restartCurrentWorld() {
    final WorldSlot? world = _activeWorld;
    if (world == null) return;
    widget.runtime.startNewWorld(world.seed);
    unawaited(_saveWorld());
    _closeSettings();
  }

  void _openSettings() {
    widget.input.clear();
    widget.runtime.setHostPaused(true);
    setState(() { _inventoryOpen = false; _settingsPage = 'root'; _settingsOpen = true; });
    _pauseGameMusicIfNeeded();
  }

  void _closeSettings() {
    _resumeGameMusicIfNeeded();
    widget.runtime.setHostPaused(false);
    setState(() { _settingsOpen = false; _settingsPage = 'root'; _controlEditorOpen = false; });
    unawaited(_persistProfile());
  }

  void _openControlEditor() {
    widget.input.clear();
    setState(() => _controlEditorOpen = true);
  }

  void _closeControlEditor() {
    setState(() => _controlEditorOpen = false);
    unawaited(_persistProfile());
    final Size? size=_queuedSize;final EdgeInsets? padding=_queuedPadding;
    if(size!=null&&padding!=null)widget.input.updateLayout(ControlLayout.forViewport(size,padding,profile:_profile.controlsFor(size.width>size.height)));
  }

  void _updateControls(ControlProfile controls) {
    final bool landscape=(_queuedSize?.width??1)>(_queuedSize?.height??2);
    setState(() => _profile = landscape ? _profile.copyWith(landscapeControls: controls) : _profile.copyWith(portraitControls: controls));
  }

  void _setCameraMode(int mode) {
    widget.runtime.setCameraMode(mode);
    setState(() => _profile = _profile.copyWith(cameraMode: mode));
  }

  void _setCameraSensitivity(double value) {
    widget.input.setLookSensitivity(value);
    setState(() => _profile = _profile.copyWith(cameraSensitivity: value));
  }

  void _setMusicReactivity(double value) {
    widget.runtime.setMusicReactivity(value);
    setState(() => _profile = _profile.copyWith(musicReactivity: value));
  }

  void _openInventory() {
    if (_menuOpen || _settingsOpen) return;
    widget.input.clear();
    setState(() => _inventoryOpen = true);
  }

  void _closeInventory() {
    widget.input.clear();
    setState(() => _inventoryOpen = false);
  }

  void _returnToMenu() {
    if (widget.runtime.started && _activeWorld != null) {
      unawaited(_saveWorld(refreshCatalog: true));
    }
    widget.runtime.setHostPaused(false);
    widget.runtime.showMenu();
    setState(() {
      _settingsOpen = false;
      _inventoryOpen = false;
      _menuOpen = true;
      _menuPage = 'worlds';
    });
  }

  void _setMoveOnLeft(bool value) {
    if (_moveOnLeft == value) return;
    ControlProfile mirror(ControlProfile p) => ControlProfile(
      joystick:p.joystick.copyWith(x:1-p.joystick.x),
      interact:p.interact.copyWith(x:1-p.interact.x),
      jump:p.jump.copyWith(x:1-p.jump.x),
      dash:p.dash.copyWith(x:1-p.dash.x),
      drop:p.drop.copyWith(x:1-p.drop.x),hotbar:p.hotbar,
    );
    setState(() { _moveOnLeft = value; _profile=_profile.copyWith(landscapeControls:mirror(_profile.landscapeControls),portraitControls:mirror(_profile.portraitControls)); });
    final Size? size = _queuedSize; final EdgeInsets? padding = _queuedPadding;
    if (size != null && padding != null) { widget.input.clear(); widget.input.updateLayout(ControlLayout.forViewport(size,padding,profile:_profile.controlsFor(size.width>size.height))); }
    unawaited(_persistProfile());
  }

  void _setTheme(int value) {
    setState(() => _theme = value);
    widget.runtime.setTheme(value);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: LayoutBuilder(
        builder: (BuildContext context, BoxConstraints constraints) {
          final Size viewport = constraints.biggest;
          final EdgeInsets padding = MediaQuery.paddingOf(context);
          _queueLayout(viewport, padding);
          return AnimatedBuilder(
            animation: _animation,
            builder: (BuildContext context, Widget? child) {
              return _buildFrame(context, viewport, padding);
            },
          );
        },
      ),
    );
  }

  Widget _buildFrame(
    BuildContext context,
    Size viewport,
    EdgeInsets padding,
  ) {
    final GameSnapshot snapshot = widget.runtime.snapshot;
    final bool landscape = viewport.width > viewport.height;
    final ControlProfile controlProfile = _profile.controlsFor(landscape);
    final ControlLayout controls = ControlLayout.forViewport(
      viewport, padding, moveOnLeft: _moveOnLeft, profile: controlProfile,
    );
    GameArtifact? openedArtifact;
    if (snapshot.openedArtifactId != 0xFFFFFFFF) {
      for (final GameArtifact artifact in snapshot.artifacts) {
        if (artifact.itemType != odgItemTurret && artifact.artifactId == snapshot.openedArtifactId) { openedArtifact = artifact; break; }
      }
    }
    final bool stationOpen = openedArtifact != null;
    final bool interactive = widget.runtime.started &&
        !_menuOpen && !_settingsOpen && !_inventoryOpen && !_controlEditorOpen && !stationOpen &&
        snapshot.playerAlive && !snapshot.matchOver;
    final bool actionEnabled = interactive && snapshot.interaction.valid;
    final bool dropEnabled = interactive && snapshot.dropActionAvailable;
    widget.input.setActionsEnabled(
      action: actionEnabled,
      movement: interactive,
      drop: dropEnabled,
    );

    return Listener(
      behavior: HitTestBehavior.opaque,
      onPointerDown: interactive
          ? (PointerDownEvent event) {
              widget.input.pointerDown(event.pointer, event.localPosition);
            }
          : null,
      onPointerMove: interactive
          ? (PointerMoveEvent event) {
              widget.input.pointerMove(event.pointer, event.localPosition);
            }
          : null,
      onPointerUp: interactive
          ? (PointerUpEvent event) => widget.input.pointerUp(event.pointer)
          : null,
      onPointerCancel: interactive
          ? (PointerCancelEvent event) => widget.input.pointerUp(event.pointer)
          : null,
      child: Stack(
        fit: StackFit.expand,
        children: <Widget>[
          _NativeFrame(image: widget.runtime.image),
          if (!_menuOpen) _buildHud(snapshot, padding, viewport, controlProfile),
          if (interactive)
            IgnorePointer(
              child: CustomPaint(
                painter: _ControlPainter(
                  layout: controls,
                  moveVector: widget.input.moveVector,
                  actionEnabled: actionEnabled,
                  actionPressed: widget.input.actionPressed,
                  movementActionsEnabled: interactive,
                  jumpPressed: widget.input.jumpPressed,
                  dashPressed: widget.input.dashPressed,
                  actionLabel: snapshot.contextualActionLabel,
                  holdProgress: snapshot.interaction.requiresHold
                      ? snapshot.interaction.progress
                      : 0,
                  dropEnabled: dropEnabled,
                  dropPressed: widget.input.dropPressed,
                ),
              ),
            ),
          if (_menuOpen && _menuPage == 'root')
            _MainMenu(
              quality: widget.runtime.quality,
              theme: _theme,
              ready: _hostStateReady,
              worldCount: _worlds.length,
              onQualityChanged: widget.runtime.setQuality,
              onThemeChanged: _setTheme,
              onPlay: () => setState(() { _menuPage = 'worlds'; _worldError = null; }),
            ),
          if (_menuOpen && _menuPage == 'worlds')
            _WorldBrowser(
              worlds: _worlds,
              ready: _hostStateReady,
              currentApi: widget.runtime.api.apiVersion(),
              currentAbi: odgFfiAbiVersion,
              currentSaveSchema: widget.runtime.api.saveSchemaVersion(),
              saveSchemaSupported: (int schema) => widget.runtime.api.saveSchemaSupported(schema) != 0,
              error: _worldError,
              onBack: () => setState(() { _menuPage = 'root'; _worldError = null; }),
              onCreate: () => unawaited(_showCreateWorldDialog()),
              onPlay: (WorldSlot world) => unawaited(_continueWorld(world)),
              onDelete: (WorldSlot world) => unawaited(_deleteWorld(world)),
            ),
          if (_inventoryOpen) InventoryPanel(runtime: widget.runtime, skins: _skins, onClose: _closeInventory),
          if (stationOpen && !_settingsOpen && !_inventoryOpen && !_menuOpen)
            StationPanel(runtime: widget.runtime, artifact: openedArtifact!, onClose: widget.runtime.closeArtifact),
          if (_settingsOpen && !_controlEditorOpen)
            _SettingsPanel(
              runtime: widget.runtime, quality: widget.runtime.quality, theme: _theme, moveOnLeft: _moveOnLeft,
              profile: _profile, page: _settingsPage,
              onPageChanged: (String value) => setState(() => _settingsPage = value),
              onQualityChanged: widget.runtime.setQuality, onThemeChanged: _setTheme, onMoveSideChanged: _setMoveOnLeft,
              onCameraModeChanged: _setCameraMode, onCameraSensitivityChanged: _setCameraSensitivity,
              onMusicReactivityChanged: _setMusicReactivity,
              onPauseMusicChanged: _setPauseMusicWithGame,
              onEditControls: _openControlEditor, onResume: _closeSettings, onRestart: _restartCurrentWorld, onExit: _returnToMenu,
            ),
          if (_controlEditorOpen)
            ControlEditor(profile: controlProfile, onChanged: _updateControls, onClose: _closeControlEditor),
        ],
      ),
    );
  }

  Widget _buildHud(
    GameSnapshot snapshot, EdgeInsets padding, Size viewport, ControlProfile controls,
  ) {
    final bool landscape = viewport.width > viewport.height;
    final double top = padding.top + 12;
    final Rect safe=Rect.fromLTRB(padding.left,padding.top,viewport.width-padding.right,viewport.height-padding.bottom);
    final double hotbarWidth=(landscape?390.0:350.0)*controls.hotbar.scale;
    final double hotbarHeight=58*controls.hotbar.scale;
    final double hotbarX=safe.left+safe.width*controls.hotbar.x-hotbarWidth/2;
    final double hotbarY=safe.top+safe.height*controls.hotbar.y-hotbarHeight/2;
    final bool building=snapshot.inventory.selected.typeId==odgItemBuildingBlock &&
        snapshot.inventory.selected.quantity>0;
    final double shapeWidth=(landscape?292.0:280.0)*controls.hotbar.scale;
    final double shapeHeight=44*controls.hotbar.scale;
    final double shapeX=(safe.left+safe.width*controls.hotbar.x-shapeWidth/2)
        .clamp(safe.left,safe.right-shapeWidth).toDouble();
    final double shapeY=(hotbarY-shapeHeight-8)
        .clamp(safe.top,safe.bottom-shapeHeight).toDouble();
    return Stack(children:<Widget>[
      Positioned(left:padding.left+14,top:top,child:_TerritoryReadout(snapshot:snapshot)),
      Positioned(right:padding.right+14,top:top,child:Row(crossAxisAlignment:CrossAxisAlignment.start,children:<Widget>[
        if(landscape)...<Widget>[_Leaderboard(snapshot:snapshot),const SizedBox(width:10)],
        _SquareIconButton(icon:Icons.pause_rounded,tooltip:'Pausa',onPressed:_openSettings),
      ])),
      if(!landscape)Positioned(right:padding.right+14,top:top+52,child:_Leaderboard(snapshot:snapshot)),
      Positioned(left:padding.left+14,top:top+74,width:landscape?104:92,height:landscape?104:92,child:IgnorePointer(child:Opacity(opacity:.80,child:DomainMap(runtime:widget.runtime,compact:true)))),
      Positioned(left:hotbarX.clamp(safe.left,safe.right-hotbarWidth).toDouble(),top:hotbarY.clamp(safe.top,safe.bottom-hotbarHeight).toDouble(),width:hotbarWidth,height:hotbarHeight,child:Opacity(
        opacity:controls.hotbar.opacity,child:GameHotbar(inventory:snapshot.inventory,onSelect:widget.runtime.selectSlot,onInventory:_openInventory),
      )),
      if(building)Positioned(left:shapeX,top:shapeY,width:shapeWidth,height:shapeHeight,child:Opacity(
        opacity:controls.hotbar.opacity,
        child:_ConstructionShapeBar(selected:widget.runtime.constructionShape,onSelect:widget.runtime.setConstructionShape),
      )),
      Positioned(left:padding.left+18,right:padding.right+18,top:(hotbarY-(building?86:34)).clamp(safe.top,safe.bottom-28).toDouble(),child:Center(child:_StatusLine(snapshot:snapshot))),
    ]);
  }

}

final class _ConstructionShapeBar extends StatelessWidget {
  const _ConstructionShapeBar({required this.selected,required this.onSelect});

  final int selected;
  final ValueChanged<int> onSelect;

  @override
  Widget build(BuildContext context) {
    const List<(int,IconData,String)> choices=<(int,IconData,String)>[
      (odgConstructionShapeFloor,Icons.grid_4x4_rounded,'SUELO'),
      (odgConstructionShapeWall,Icons.view_week_rounded,'MURO'),
      (odgConstructionShapeDoorway,Icons.door_front_door_outlined,'VANO'),
      (odgConstructionShapeRoof,Icons.roofing_rounded,'TECHO'),
    ];
    return DecoratedBox(
      decoration:BoxDecoration(
        color:const Color(0xDF0C1116),
        border:Border.all(color:OdparDesign.panelEdge),
        borderRadius:BorderRadius.circular(7),
      ),
      child:Padding(
        padding:const EdgeInsets.all(3),
        child:Row(children:<Widget>[
          for(final (int shape,IconData icon,String label) in choices)
            Expanded(child:Padding(
              padding:const EdgeInsets.symmetric(horizontal:2),
              child:Semantics(
                button:true,
                selected:selected==shape,
                label:'Construcción $label',
                child:GestureDetector(
                  behavior:HitTestBehavior.opaque,
                  onTap:()=>onSelect(shape),
                  child:AnimatedContainer(
                    duration:const Duration(milliseconds:90),
                    decoration:BoxDecoration(
                      color:selected==shape?const Color(0xFF172923):Colors.transparent,
                      border:Border.all(color:selected==shape?OdparDesign.accent:Colors.transparent),
                      borderRadius:BorderRadius.circular(5),
                    ),
                    child:Column(mainAxisAlignment:MainAxisAlignment.center,children:<Widget>[
                      Icon(icon,size:15,color:selected==shape?OdparDesign.accent:OdparDesign.textMuted),
                      const SizedBox(height:1),
                      Text(label,style:TextStyle(
                        fontSize:7.5,fontWeight:FontWeight.w700,letterSpacing:.65,
                        color:selected==shape?OdparDesign.text:OdparDesign.textMuted,
                      )),
                    ]),
                  ),
                ),
              ),
            )),
        ]),
      ),
    );
  }
}

final class _NativeFrame extends StatelessWidget {
  const _NativeFrame({required this.image});

  final ui.Image? image;

  @override
  Widget build(BuildContext context) {
    final ui.Image? frame = image;
    if (frame == null) {
      return const DecoratedBox(
        decoration: BoxDecoration(
          gradient: RadialGradient(
            radius: 1.1,
            colors: <Color>[Color(0xFF172028), OdparDesign.voidBlack],
          ),
        ),
      );
    }
    return RawImage(
      image: frame,
      fit: BoxFit.cover,
      filterQuality: FilterQuality.medium,
    );
  }
}

final class _TerritoryReadout extends StatelessWidget {
  const _TerritoryReadout({required this.snapshot});

  final GameSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    return OdparPanel(
      surfaceColor: const Color(0xB30A0F14),
      edgeColor: const Color(0x80505C66),
      padding: const EdgeInsets.fromLTRB(12, 9, 12, 8),
      child: SizedBox(
        width: 158,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Row(
              children: <Widget>[
                OdparMetric(
                  label: 'DOMINIO',
                  value: '${snapshot.territoryPercent.toStringAsFixed(1)}%',
                  emphasized: true,
                ),
                const Spacer(),
                OdparMetric(
                  label: 'EN RED',
                  value: '${snapshot.aliveCount}',
                ),
              ],
            ),
            const SizedBox(height: 8),
            ClipRRect(
              borderRadius: BorderRadius.circular(2),
              child: LinearProgressIndicator(
                value: (snapshot.territoryPermille / 1000)
                    .clamp(0, 1)
                    .toDouble(),
                minHeight: 3,
                color: OdparDesign.accent,
                backgroundColor: const Color(0xFF28313B),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

final class _Leaderboard extends StatelessWidget {
  const _Leaderboard({required this.snapshot});

  final GameSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    return OdparPanel(
      surfaceColor: const Color(0xB30A0F14),
      edgeColor: const Color(0x80505C66),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
      child: SizedBox(
        width: 118,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: <Widget>[
            for (int index = 0; index < snapshot.leaders.length; index += 1)
              _LeaderRow(
                rank: index + 1,
                leader: snapshot.leaders[index],
                percent:
                    snapshot.leaderPercent(snapshot.leaders[index].score),
              ),
          ],
        ),
      ),
    );
  }
}

final class _LeaderRow extends StatelessWidget {
  const _LeaderRow({
    required this.rank,
    required this.leader,
    required this.percent,
  });

  final int rank;
  final GameLeader leader;
  final double percent;

  @override
  Widget build(BuildContext context) {
    final String name = leader.isPlayer
        ? 'TÚ'
        : GameRuntime.leaderNames[
            leader.nameCode % GameRuntime.leaderNames.length
          ];
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: <Widget>[
          SizedBox(
            width: 15,
            child: Text(
              '$rank',
              style: const TextStyle(
                color: OdparDesign.textMuted,
                fontSize: 10,
              ),
            ),
          ),
          Expanded(
            child: Text(
              name,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: leader.isPlayer
                    ? OdparDesign.accent
                    : OdparDesign.text,
                fontSize: 10,
                fontWeight: FontWeight.w600,
                letterSpacing: 0.8,
              ),
            ),
          ),
          Text(
            '${percent.toStringAsFixed(1)}%',
            style: const TextStyle(
              color: OdparDesign.textMuted,
              fontSize: 10,
              fontFeatures: <ui.FontFeature>[ui.FontFeature.tabularFigures()],
            ),
          ),
        ],
      ),
    );
  }
}

final class _StatusLine extends StatelessWidget {
  const _StatusLine({required this.snapshot});

  final GameSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    final GameConstruction? targetedConstruction = snapshot.targetedConstruction;
    String text = snapshot.statusLabel;
    Color color = OdparDesign.textMuted;
    if (!snapshot.playerAlive) {
      text = 'FUERA DE RED · SIMULACIÓN EN CURSO';
      color = OdparDesign.danger;
    } else if (snapshot.trailActive) {
      color = OdparDesign.amber;
    } else if (targetedConstruction != null) {
      final GameConstruction structure = targetedConstruction;
      final String reason = snapshot.interaction.messageLabel;
      text = '${structure.shapeLabel}  ${structure.healthPercent}%  ·  ${snapshot.interaction.label}';
      if (!snapshot.interaction.valid && reason.isNotEmpty) text = '$text  ·  $reason';
      color = structure.healthFraction <= 0.35 ||
              snapshot.interaction.action == odgInteractionAttackConstruction
          ? OdparDesign.danger
          : snapshot.interaction.action == odgInteractionRepairConstruction
              ? OdparDesign.amber
              : OdparDesign.accent;
    } else if (!snapshot.interaction.valid &&
        snapshot.interaction.action != odgInteractionNone &&
        snapshot.interaction.messageLabel.isNotEmpty) {
      text = '${snapshot.interaction.label}  ·  ${snapshot.interaction.messageLabel}';
      color = OdparDesign.amber;
    } else if (snapshot.nearbyTurretVisible) {
      text = 'TORRETA  ${snapshot.nearbyTurretAmmo}'
          '/${snapshot.nearbyTurretMaxAmmo}  ·  ${snapshot.statusLabel}';
      color = OdparDesign.accent;
    }
    return DecoratedBox(
      decoration: BoxDecoration(
        color: const Color(0xA80A0F14),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: const Color(0x75505C66)),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 6),
        child: Text(
          text,
          textAlign: TextAlign.center,
          style: TextStyle(
            color: color,
            fontSize: 9,
            fontWeight: FontWeight.w700,
            letterSpacing: 1.0,
          ),
        ),
      ),
    );
  }
}

final class _SquareIconButton extends StatelessWidget {
  const _SquareIconButton({
    required this.icon,
    required this.tooltip,
    required this.onPressed,
  });

  final IconData icon;
  final String tooltip;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 38,
      height: 38,
      child: IconButton(
        tooltip: tooltip,
        style: IconButton.styleFrom(
          backgroundColor: OdparDesign.panel,
          side: const BorderSide(color: OdparDesign.panelEdge),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
        ),
        onPressed: onPressed,
        icon: Icon(icon, size: 18),
      ),
    );
  }
}

final class _MainMenu extends StatelessWidget {
  const _MainMenu({
    required this.quality,
    required this.theme,
    required this.ready,
    required this.worldCount,
    required this.onQualityChanged,
    required this.onThemeChanged,
    required this.onPlay,
  });

  final RasterQuality quality;
  final int theme;
  final bool ready;
  final int worldCount;
  final ValueChanged<RasterQuality> onQualityChanged;
  final ValueChanged<int> onThemeChanged;
  final VoidCallback onPlay;

  @override
  Widget build(BuildContext context) => SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(18),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 520),
              child: OdparPanel(
                padding: const EdgeInsets.fromLTRB(24, 22, 24, 24),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: <Widget>[
                    const Text(
                      'ODPAR · BY KAOST032',
                      style: TextStyle(
                        color: OdparDesign.accent,
                        fontSize: 10,
                        fontWeight: FontWeight.w700,
                        letterSpacing: 2.6,
                      ),
                    ),
                    const SizedBox(height: 9),
                    Text('TERRITORIAL\nDOMAIN', style: Theme.of(context).textTheme.displaySmall),
                    const SizedBox(height: 15),
                    const Text(
                      'Explora, conquista, sobrevive y transforma un mundo persistente. '
                      'Flora, fauna, clima y otras naciones continúan obedeciendo al núcleo C11.',
                    ),
                    const SizedBox(height: 22),
                    if (!ready)
                      const Center(
                        child: Padding(
                          padding: EdgeInsets.all(8),
                          child: CircularProgressIndicator(strokeWidth: 1.5),
                        ),
                      )
                    else
                      OdparActionButton(
                        label: worldCount == 0 ? 'JUGAR · CREAR PRIMER MUNDO' : 'JUGAR · $worldCount MUNDO${worldCount == 1 ? '' : 'S'}',
                        onPressed: onPlay,
                        primary: true,
                      ),
                    const SizedBox(height: 20),
                    const _SectionLabel('RASTER'),
                    const SizedBox(height: 8),
                    _QualityStrip(value: quality, onChanged: onQualityChanged),
                    const SizedBox(height: 15),
                    const _SectionLabel('ATMÓSFERA'),
                    const SizedBox(height: 8),
                    _ThemeStrip(value: theme, onChanged: onThemeChanged),
                    const SizedBox(height: 14),
                    const Text(
                      'LOS MUNDOS TIENEN NOMBRE, SEED Y SAVE INDEPENDIENTE · NUNCA SE SOBRESCRIBE UN SAVE INCOMPATIBLE',
                      textAlign: TextAlign.center,
                      style: TextStyle(
                        color: OdparDesign.textMuted,
                        fontSize: 8,
                        fontWeight: FontWeight.w600,
                        letterSpacing: 1.0,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      );
}

final class _WorldBrowser extends StatelessWidget {
  const _WorldBrowser({
    required this.worlds,
    required this.ready,
    required this.currentApi,
    required this.currentAbi,
    required this.currentSaveSchema,
    required this.saveSchemaSupported,
    required this.error,
    required this.onBack,
    required this.onCreate,
    required this.onPlay,
    required this.onDelete,
  });

  final List<WorldSlot> worlds;
  final bool ready;
  final int currentApi;
  final int currentAbi;
  final int currentSaveSchema;
  final bool Function(int schema) saveSchemaSupported;
  final String? error;
  final VoidCallback onBack;
  final VoidCallback onCreate;
  final ValueChanged<WorldSlot> onPlay;
  final ValueChanged<WorldSlot> onDelete;

  String _date(int milliseconds) {
    if (milliseconds <= 0) return 'FECHA DESCONOCIDA';
    final DateTime d = DateTime.fromMillisecondsSinceEpoch(milliseconds);
    String two(int v) => v.toString().padLeft(2, '0');
    return '${d.year}-${two(d.month)}-${two(d.day)} ${two(d.hour)}:${two(d.minute)}';
  }

  @override
  Widget build(BuildContext context) => SafeArea(
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 650),
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: OdparPanel(
                padding: const EdgeInsets.all(18),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: <Widget>[
                    Row(
                      children: <Widget>[
                        IconButton(onPressed: onBack, icon: const Icon(Icons.arrow_back_rounded)),
                        const SizedBox(width: 6),
                        Expanded(child: Text('MUNDOS', style: Theme.of(context).textTheme.headlineSmall)),
                        Text(
                          '${worlds.length}',
                          style: const TextStyle(color: OdparDesign.textMuted, fontFeatures: <ui.FontFeature>[ui.FontFeature.tabularFigures()]),
                        ),
                      ],
                    ),
                    const Divider(height: 16, color: OdparDesign.panelEdge),
                    if (error != null) ...<Widget>[
                      DecoratedBox(
                        decoration: BoxDecoration(
                          color: const Color(0x331F0F0F),
                          border: Border.all(color: OdparDesign.danger),
                          borderRadius: BorderRadius.circular(5),
                        ),
                        child: Padding(
                          padding: const EdgeInsets.all(10),
                          child: Text(error!, style: const TextStyle(color: OdparDesign.danger, fontSize: 9, height: 1.4)),
                        ),
                      ),
                      const SizedBox(height: 10),
                    ],
                    Expanded(
                      child: !ready
                          ? const Center(child: CircularProgressIndicator(strokeWidth: 1.5))
                          : worlds.isEmpty
                              ? const Center(
                                  child: Text(
                                    'NO HAY MUNDOS TODAVÍA\n\nCrea uno para comenzar.',
                                    textAlign: TextAlign.center,
                                    style: TextStyle(color: OdparDesign.textMuted, fontSize: 10, height: 1.5),
                                  ),
                                )
                              : ListView.separated(
                                  itemCount: worlds.length,
                                  separatorBuilder: (_, __) => const SizedBox(height: 8),
                                  itemBuilder: (BuildContext context, int index) {
                                    final WorldSlot world = worlds[index];
                                    final bool compatible = world.structurallyLoadable &&
                                        saveSchemaSupported(world.saveSchemaVersion);
                                    return DecoratedBox(
                                      decoration: BoxDecoration(
                                        color: const Color(0xAA0E141A),
                                        border: Border.all(color: compatible ? OdparDesign.panelEdge : OdparDesign.danger),
                                        borderRadius: BorderRadius.circular(6),
                                      ),
                                      child: Padding(
                                        padding: const EdgeInsets.fromLTRB(12, 10, 8, 10),
                                        child: Row(
                                          children: <Widget>[
                                            Expanded(
                                              child: Column(
                                                crossAxisAlignment: CrossAxisAlignment.start,
                                                children: <Widget>[
                                                  Text(world.name, style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 12)),
                                                  const SizedBox(height: 4),
                                                  Text(
                                                    compatible
                                                        ? 'SEED ${world.seed} · ${_date(world.updatedAtMs)}'
                                                        : world.corrupt
                                                            ? 'SAVE DAÑADO · CHECKSUM NO COINCIDE'
                                                            : world.legacy
                                                                ? 'SAVE LEGADO · CONSERVADO SIN MODIFICAR'
                                                                : 'SAVE NO COMPATIBLE · ${world.saveSchemaVersion} · creado API ${world.apiVersion}/ABI ${world.ffiAbiVersion}',
                                                    style: TextStyle(
                                                      color: compatible ? OdparDesign.textMuted : OdparDesign.danger,
                                                      fontSize: 8,
                                                      letterSpacing: .5,
                                                    ),
                                                  ),
                                                ],
                                              ),
                                            ),
                                            const SizedBox(width: 8),
                                            IconButton(
                                              tooltip: compatible ? 'Jugar' : 'Requiere migración',
                                              onPressed: () => onPlay(world),
                                              icon: Icon(compatible ? Icons.play_arrow_rounded : Icons.lock_clock_outlined, size: 21),
                                            ),
                                            IconButton(
                                              tooltip: 'Eliminar',
                                              onPressed: () => onDelete(world),
                                              icon: const Icon(Icons.delete_outline_rounded, size: 19),
                                            ),
                                          ],
                                        ),
                                      ),
                                    );
                                  },
                                ),
                    ),
                    const SizedBox(height: 12),
                    OdparActionButton(label: 'CREAR MUNDO', onPressed: onCreate, primary: true),
                  ],
                ),
              ),
            ),
          ),
        ),
      );
}

final class _SettingsPanel extends StatelessWidget {
  const _SettingsPanel({
    required this.runtime, required this.quality, required this.theme, required this.moveOnLeft,
    required this.profile, required this.page, required this.onPageChanged,
    required this.onQualityChanged, required this.onThemeChanged, required this.onMoveSideChanged,
    required this.onCameraModeChanged, required this.onCameraSensitivityChanged,
    required this.onMusicReactivityChanged, required this.onPauseMusicChanged,
    required this.onEditControls, required this.onResume, required this.onRestart, required this.onExit,
  });
  final GameRuntime runtime; final RasterQuality quality; final int theme; final bool moveOnLeft;
  final GameProfile profile; final String page; final ValueChanged<String> onPageChanged;
  final ValueChanged<RasterQuality> onQualityChanged; final ValueChanged<int> onThemeChanged; final ValueChanged<bool> onMoveSideChanged;
  final ValueChanged<int> onCameraModeChanged; final ValueChanged<double> onCameraSensitivityChanged;
  final ValueChanged<double> onMusicReactivityChanged; final ValueChanged<bool> onPauseMusicChanged;
  final VoidCallback onEditControls; final VoidCallback onResume; final VoidCallback onRestart; final VoidCallback onExit;

  @override Widget build(BuildContext context)=>ColoredBox(color:const Color(0x52000000),child:SafeArea(child:LayoutBuilder(
    builder:(BuildContext context,BoxConstraints constraints){
      final bool landscape=constraints.maxWidth>constraints.maxHeight;
      final double panelWidth=landscape?constraints.maxWidth.clamp(340.0,430.0).toDouble():(constraints.maxWidth*.86).clamp(290.0,440.0).toDouble();
      return Align(alignment:Alignment.centerRight,child:SizedBox(width:panelWidth,height:double.infinity,child:DecoratedBox(
        decoration:const BoxDecoration(color:Color(0xF20B0F13),border:Border(left:BorderSide(color:OdparDesign.panelEdge))),
        child:Column(children:<Widget>[
          Padding(padding:const EdgeInsets.fromLTRB(18,13,9,10),child:Row(children:<Widget>[
            if(page!='root')IconButton(onPressed:()=>onPageChanged('root'),icon:const Icon(Icons.arrow_back_rounded,size:19)),
            Expanded(child:Text(_pageTitle(),style:Theme.of(context).textTheme.headlineSmall)),
            _SquareIconButton(icon:Icons.close_rounded,tooltip:'Continuar',onPressed:onResume),
          ])),
          const Divider(height:1,color:OdparDesign.panelEdge),
          Expanded(child:_body(context)),
        ]),
      )));
    },
  )));

  String _pageTitle()=>switch(page){'controls'=>'CONTROLES','camera'=>'CÁMARA','music'=>'MÚSICA','graphics'=>'GRÁFICOS','info'=>'INFORMACIÓN',_=>'PAUSA'};

  Widget _body(BuildContext context)=>switch(page){
    'controls'=>ListView(padding:const EdgeInsets.all(18),children:<Widget>[
      const Text('El perfil se guarda respecto al safe area. Portrait y landscape son independientes.',style:TextStyle(fontSize:9,color:OdparDesign.textMuted,height:1.4)),
      const SizedBox(height:14),OdparActionButton(label:'EDITAR POSICIÓN / TAMAÑO / OPACIDAD',onPressed:onEditControls,primary:true),
      const SizedBox(height:16),const _SectionLabel('LADO DE MOVIMIENTO'),const SizedBox(height:8),
      _OptionStrip<bool>(values:const <bool>[true,false],value:moveOnLeft,labelFor:(bool v)=>v?'JOYSTICK IZQ.':'JOYSTICK DER.',onChanged:onMoveSideChanged),
      const SizedBox(height:12),const Text('Rangos: tamaño 0.70–1.50 · opacidad 0.20–1.00. El botón de pausa nunca forma parte del editor.',style:TextStyle(fontSize:8,color:OdparDesign.textMuted)),
    ]),
    'camera'=>ListView(padding:const EdgeInsets.all(18),children:<Widget>[
      _CameraPreviewCard(runtime:runtime,mode:profile.cameraMode,onModeChanged:onCameraModeChanged),
      const SizedBox(height:14),const _SectionLabel('DISTANCIA'),const SizedBox(height:8),
      _OptionStrip<int>(values:const <int>[odgCameraModeFirstPerson,odgCameraModeClose,odgCameraModeMedium,odgCameraModeFar],value:profile.cameraMode,labelFor:(int v)=>switch(v){0=>'1ª PERSONA',1=>'CERCA',2=>'MEDIA',_=>'LEJOS'},onChanged:onCameraModeChanged),
      const SizedBox(height:18),Row(children:<Widget>[const Expanded(child:_SectionLabel('SENSIBILIDAD')),Text('${profile.cameraSensitivity.toStringAsFixed(2)}×',style:const TextStyle(color:OdparDesign.accent,fontSize:9))]),
      Slider(value:profile.cameraSensitivity,min:.40,max:2.00,divisions:32,onChanged:onCameraSensitivityChanged),
      const Text('Arrastra el preview para inspeccionar sin mover al jugador. Pinch ajusta la distancia. El movimiento temporal del preview se descarta; sólo modo y sensibilidad se guardan. ControlCamera permanece separado de FOV, dolly, bob y música.',style:TextStyle(fontSize:8,color:OdparDesign.textMuted,height:1.45)),
    ]),
    'music'=>MusicPanel(runtime:runtime,reactivity:profile.musicReactivity,onReactivityChanged:onMusicReactivityChanged,pauseWithGame:profile.pauseMusicWithGame,onPauseWithGameChanged:onPauseMusicChanged),
    'graphics'=>ListView(padding:const EdgeInsets.all(18),children:<Widget>[
      const _SectionLabel('RASTER'),const SizedBox(height:8),_QualityStrip(value:quality,onChanged:onQualityChanged),
      const SizedBox(height:16),const _SectionLabel('PALETA DEL MUNDO'),const SizedBox(height:8),_ThemeStrip(value:theme,onChanged:onThemeChanged),
      const SizedBox(height:12),const Text('La atmósfera, fog, cielo, territorio, trail y objetos 3D son responsabilidad del renderer C. Flutter no aplica maquillaje global al framebuffer.',style:TextStyle(fontSize:8,color:OdparDesign.textMuted,height:1.4)),
    ]),
    'info'=>ListView(padding:const EdgeInsets.all(18),children:<Widget>[
      const Text('ODPAR: Territorial Domain',style:TextStyle(fontSize:16,fontWeight:FontWeight.w700)),const SizedBox(height:4),Text('BY KAOST032 · API $odgApiVersion / FFI ABI $odgFfiAbiVersion',style:const TextStyle(color:OdparDesign.accent,fontSize:9,letterSpacing:1)),
      SizedBox(height:16),_SectionLabel('GAME CREDITS'),SizedBox(height:7),
      Text('Created & developed by kaost032',style:TextStyle(fontSize:11,fontWeight:FontWeight.w700)),SizedBox(height:6),
      Text('Gracias por apoyar ODPAR. Gracias a ese apoyo puedo seguir mejorando Territorial Domain y continuar creando y desarrollando más proyectos.',style:TextStyle(fontSize:9,color:OdparDesign.textMuted,height:1.55)),
      SizedBox(height:18),_SectionLabel('MUSIC CREDITS'),SizedBox(height:7),
      Text('FEATURED MUSIC — AFTERIMAGE 0.2',style:TextStyle(fontSize:11,fontWeight:FontWeight.w700)),SizedBox(height:3),
      Text('7 Original Tracks · 5 Instrumental Reworks\nMusic catalog by kaost032\nCatálogo incluido: propiedad/licencia de kaost032.',style:TextStyle(fontSize:9,color:OdparDesign.textMuted,height:1.55)),
      SizedBox(height:18),_SectionLabel('ARQUITECTURA'),SizedBox(height:7),
      Text('Flutter/Dart = host y UI\nFFI = contrato versionado\nC11 = autoridad del mundo/gameplay\nC renderer = mundo 3D y geometría\n120 Hz = simulación autoritativa',style:TextStyle(fontSize:9,color:OdparDesign.textMuted,height:1.65)),
    ]),
    _=>ListView(padding:const EdgeInsets.fromLTRB(18,16,18,22),children:<Widget>[
      _PauseNavRow(icon:Icons.gamepad_outlined,label:'CONTROLES',onTap:()=>onPageChanged('controls')),
      _PauseNavRow(icon:Icons.videocam_outlined,label:'CÁMARA',onTap:()=>onPageChanged('camera')),
      _PauseNavRow(icon:Icons.music_note_outlined,label:'MÚSICA',onTap:()=>onPageChanged('music')),
      _PauseNavRow(icon:Icons.tune_rounded,label:'GRÁFICOS',onTap:()=>onPageChanged('graphics')),
      _PauseNavRow(icon:Icons.info_outline_rounded,label:'INFORMACIÓN',onTap:()=>onPageChanged('info')),
      const SizedBox(height:18),OdparActionButton(label:'CONTINUAR',onPressed:onResume,primary:true),const SizedBox(height:9),
      OdparActionButton(label:'REINICIAR MUNDO',onPressed:onRestart),const SizedBox(height:7),TextButton(onPressed:onExit,child:const Text('SALIR AL CENTRO DE MANDO')),
    ]),
  };
}

final class _CameraPreviewCard extends StatefulWidget {
  const _CameraPreviewCard({required this.runtime,required this.mode,required this.onModeChanged});
  final GameRuntime runtime; final int mode; final ValueChanged<int> onModeChanged;
  @override State<_CameraPreviewCard> createState()=>_CameraPreviewCardState();
}

final class _CameraPreviewCardState extends State<_CameraPreviewCard>{
  ui.Image? _image; bool _busy=false; bool _again=false; int _yawQ16=0; int _pitchQ15=5200; int _gestureBaseMode=odgCameraModeMedium;
  @override void initState(){super.initState();_gestureBaseMode=widget.mode;unawaited(_render());}
  @override void didUpdateWidget(covariant _CameraPreviewCard oldWidget){super.didUpdateWidget(oldWidget);if(oldWidget.mode!=widget.mode)unawaited(_render());}
  @override void dispose(){_image?.dispose();super.dispose();}
  Future<void> _render()async{
    if(_busy){_again=true;return;}_busy=true;
    try{
      final ui.Image? next=await widget.runtime.renderCameraPreview(widget.mode,_yawQ16,_pitchQ15);
      if(!mounted){next?.dispose();return;}final ui.Image? old=_image;setState(()=>_image=next);old?.dispose();
    }finally{_busy=false;if(_again&&mounted){_again=false;unawaited(_render());}}
  }
  void _scaleStart(ScaleStartDetails d){_gestureBaseMode=widget.mode;}
  void _scaleUpdate(ScaleUpdateDetails d){
    _yawQ16=(_yawQ16-(d.focalPointDelta.dx*145).round())&0xffff;
    _pitchQ15=(_pitchQ15+(d.focalPointDelta.dy*150).round()).clamp(-4500,14500).toInt();
    if(d.pointerCount>=2){
      int next=_gestureBaseMode;
      if(d.scale>1.18)next=(_gestureBaseMode-1).clamp(odgCameraModeFirstPerson,odgCameraModeFar).toInt();
      else if(d.scale<.84)next=(_gestureBaseMode+1).clamp(odgCameraModeFirstPerson,odgCameraModeFar).toInt();
      if(next!=widget.mode)widget.onModeChanged(next);
    }
    unawaited(_render());
  }
  @override Widget build(BuildContext context)=>GestureDetector(
    behavior:HitTestBehavior.opaque,onScaleStart:_scaleStart,onScaleUpdate:_scaleUpdate,
    child:AspectRatio(aspectRatio:16/9,child:DecoratedBox(
      decoration:BoxDecoration(color:Colors.black,border:Border.all(color:OdparDesign.panelEdge),borderRadius:BorderRadius.circular(7)),
      child:ClipRRect(borderRadius:BorderRadius.circular(6),child:Stack(fit:StackFit.expand,children:<Widget>[
        if(_image!=null)RawImage(image:_image,fit:BoxFit.cover,filterQuality:FilterQuality.medium)else const Center(child:CircularProgressIndicator(strokeWidth:1.3)),
        const Positioned(left:8,bottom:7,child:DecoratedBox(decoration:BoxDecoration(color:Color(0xA80B0F13)),child:Padding(padding:EdgeInsets.symmetric(horizontal:6,vertical:4),child:Text('PREVIEW C · ARRASTRA / PINCH',style:TextStyle(fontSize:7,color:OdparDesign.textMuted,letterSpacing:.8))))),
      ])),
    )),
  );
}

final class _PauseNavRow extends StatelessWidget {
  const _PauseNavRow({required this.icon,required this.label,required this.onTap});
  final IconData icon;final String label;final VoidCallback onTap;
  @override Widget build(BuildContext context)=>InkWell(onTap:onTap,borderRadius:BorderRadius.circular(5),child:Container(
    height:44,margin:const EdgeInsets.only(bottom:6),padding:const EdgeInsets.symmetric(horizontal:12),decoration:BoxDecoration(color:const Color(0xFF11161C),borderRadius:BorderRadius.circular(5),border:Border.all(color:OdparDesign.panelEdge)),
    child:Row(children:<Widget>[Icon(icon,size:16,color:OdparDesign.textMuted),const SizedBox(width:10),Text(label,style:const TextStyle(color:OdparDesign.text,fontSize:9,fontWeight:FontWeight.w700,letterSpacing:1)),const Spacer(),const Icon(Icons.chevron_right_rounded,size:16,color:OdparDesign.textMuted)]),
  ));
}

final class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        color: OdparDesign.textMuted,
        fontSize: 9,
        fontWeight: FontWeight.w700,
        letterSpacing: 1.8,
      ),
    );
  }
}

final class _QualityStrip extends StatelessWidget {
  const _QualityStrip({required this.value, required this.onChanged});

  final RasterQuality value;
  final ValueChanged<RasterQuality> onChanged;

  @override
  Widget build(BuildContext context) {
    return _OptionStrip<RasterQuality>(
      values: RasterQuality.values,
      value: value,
      labelFor: (RasterQuality item) => item.label,
      onChanged: onChanged,
    );
  }
}

final class _ThemeStrip extends StatelessWidget {
  const _ThemeStrip({required this.value, required this.onChanged});

  static const List<String> _labels = <String>[
    'BASALTO',
    'CANOPIA',
    'COBRE',
    'MEDIANOCHE',
  ];

  final int value;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return _OptionStrip<int>(
      values: const <int>[0, 1, 2, 3],
      value: value,
      labelFor: (int item) => _labels[item],
      onChanged: onChanged,
    );
  }
}

final class _OptionStrip<T> extends StatelessWidget {
  const _OptionStrip({
    required this.values,
    required this.value,
    required this.labelFor,
    required this.onChanged,
  });

  final List<T> values;
  final T value;
  final String Function(T) labelFor;
  final ValueChanged<T> onChanged;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) {
        final int columns = constraints.maxWidth < 380 ? 2 : values.length;
        final double width =
            (constraints.maxWidth - (columns - 1) * 7) / columns;
        return Wrap(
          spacing: 7,
          runSpacing: 7,
          children: <Widget>[
            for (final T item in values)
              SizedBox(
                width: width,
                height: 36,
                child: InkWell(
                  borderRadius: BorderRadius.circular(4),
                  onTap: () => onChanged(item),
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      color: item == value
                          ? const Color(0xFF25332F)
                          : const Color(0xFF151A20),
                      borderRadius: BorderRadius.circular(4),
                      border: Border.all(
                        color: item == value
                            ? OdparDesign.accent
                            : OdparDesign.panelEdge,
                      ),
                    ),
                    child: Center(
                      child: Text(
                        labelFor(item),
                        maxLines: 1,
                        overflow: TextOverflow.fade,
                        style: TextStyle(
                          color: item == value
                              ? OdparDesign.accent
                              : OdparDesign.textMuted,
                          fontSize: 8,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 0.7,
                        ),
                      ),
                    ),
                  ),
                ),
              ),
          ],
        );
      },
    );
  }
}

final class _ControlPainter extends CustomPainter {
  const _ControlPainter({
    required this.layout,
    required this.moveVector,
    required this.actionEnabled,
    required this.actionPressed,
    required this.movementActionsEnabled,
    required this.jumpPressed,
    required this.dashPressed,
    required this.actionLabel,
    required this.holdProgress,
    required this.dropEnabled,
    required this.dropPressed,
  });

  final ControlLayout layout;
  final Offset moveVector;
  final bool actionEnabled;
  final bool actionPressed;
  final bool movementActionsEnabled;
  final bool jumpPressed;
  final bool dashPressed;
  final String actionLabel;
  final double holdProgress;
  final bool dropEnabled;
  final bool dropPressed;

  @override
  void paint(Canvas canvas, Size size) {
    final Paint line = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.2
      ..color = OdparDesign.text.withValues(alpha: 0.18 * layout.joystickOpacity);
    final Paint fill = Paint()
      ..style = PaintingStyle.fill
      ..color = OdparDesign.voidBlack.withValues(alpha: 0.27 * layout.joystickOpacity);
    canvas.drawCircle(
      layout.joystickCenter,
      layout.joystickRadius,
      fill,
    );
    canvas.drawCircle(
      layout.joystickCenter,
      layout.joystickRadius,
      line,
    );
    final Offset knob = layout.joystickCenter +
        moveVector * (layout.joystickRadius * 0.62);
    canvas.drawCircle(
      knob,
      layout.joystickRadius * 0.29,
      Paint()
        ..color = OdparDesign.accent.withValues(alpha: 0.18 * layout.joystickOpacity)
        ..style = PaintingStyle.fill,
    );
    canvas.drawCircle(
      knob,
      layout.joystickRadius * 0.29,
      Paint()
        ..color = OdparDesign.accent.withValues(alpha: 0.58 * layout.joystickOpacity)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.3,
    );

    if (actionEnabled) {
      final Offset center = layout.actionRect.center;
      final double radius = layout.actionRect.shortestSide / 2;
      canvas.drawCircle(
        center,
        radius,
        Paint()
          ..color = OdparDesign.accent.withValues(
            alpha: (actionPressed ? 0.30 : 0.10) * layout.actionOpacity,
          ),
      );
      canvas.drawCircle(
        center,
        radius,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.3
          ..color = OdparDesign.accent.withValues(alpha: 0.68 * layout.actionOpacity),
      );
      if (holdProgress > 0) {
        canvas.drawArc(
          Rect.fromCircle(center: center, radius: radius - 4),
          -1.57079632679,
          6.28318530718 * holdProgress.clamp(0.0, 1.0),
          false,
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeCap = StrokeCap.round
            ..strokeWidth = 2.4
            ..color = OdparDesign.accent.withValues(alpha: layout.actionOpacity),
        );
      }
      _paintHand(canvas, center.translate(0, -7), OdparDesign.accent);
      _paintLabel(canvas, center.translate(0, 10), actionLabel, OdparDesign.accent);
    }
    if (movementActionsEnabled) {
      _paintMovementButton(
        canvas, layout.jumpRect, 'SALTO', jumpPressed,
        layout.jumpOpacity, OdparDesign.accent,
      );
      _paintMovementButton(
        canvas, layout.dashRect, 'DASH', dashPressed,
        layout.dashOpacity, OdparDesign.amber,
      );
    }
    if (dropEnabled) {
      final RRect shape = RRect.fromRectAndRadius(
        layout.dropRect,
        const Radius.circular(4),
      );
      canvas.drawRRect(
        shape,
        Paint()
          ..color = OdparDesign.amber.withValues(
            alpha: (dropPressed ? 0.28 : 0.09) * layout.dropOpacity,
          ),
      );
      canvas.drawRRect(
        shape,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1
          ..color = OdparDesign.amber.withValues(alpha: 0.62 * layout.dropOpacity),
      );
      _paintLabel(canvas, layout.dropRect.center, 'SOLTAR', OdparDesign.amber);
    }
  }

  void _paintMovementButton(
    Canvas canvas,
    Rect rect,
    String label,
    bool pressed,
    double opacity,
    Color color,
  ) {
    final Offset center = rect.center;
    final double radius = rect.shortestSide / 2;
    canvas.drawCircle(
      center,
      radius,
      Paint()..color = color.withValues(alpha: (pressed ? 0.27 : 0.085) * opacity),
    );
    canvas.drawCircle(
      center,
      radius,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.1
        ..color = color.withValues(alpha: 0.58 * opacity),
    );
    _paintLabel(canvas, center, label, color, maxWidth: rect.width);
  }

  void _paintHand(Canvas canvas, Offset center, Color color) {
    final Paint paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.3
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round
      ..color = color.withValues(alpha: 0.9);
    final Path path = Path()
      ..moveTo(center.dx - 5, center.dy + 3)
      ..lineTo(center.dx - 7, center.dy - 1)
      ..quadraticBezierTo(center.dx - 8, center.dy - 4, center.dx - 5, center.dy - 3)
      ..lineTo(center.dx - 3, center.dy)
      ..lineTo(center.dx - 3, center.dy - 7)
      ..quadraticBezierTo(center.dx - 3, center.dy - 9, center.dx - 1.5, center.dy - 9)
      ..lineTo(center.dx - 1.5, center.dy - 2)
      ..lineTo(center.dx, center.dy - 10)
      ..quadraticBezierTo(center.dx + 0.3, center.dy - 11.5, center.dx + 1.6, center.dy - 10)
      ..lineTo(center.dx + 1.6, center.dy - 2)
      ..lineTo(center.dx + 3.3, center.dy - 8)
      ..quadraticBezierTo(center.dx + 4, center.dy - 9, center.dx + 5, center.dy - 7.5)
      ..lineTo(center.dx + 4.3, center.dy - 1)
      ..quadraticBezierTo(center.dx + 8, center.dy - 3, center.dx + 8, center.dy + 1)
      ..quadraticBezierTo(center.dx + 7, center.dy + 8, center.dx, center.dy + 8)
      ..quadraticBezierTo(center.dx - 4, center.dy + 8, center.dx - 5, center.dy + 3);
    canvas.drawPath(path, paint);
  }

  void _paintLabel(Canvas canvas, Offset center, String text, Color color, {double? maxWidth}) {
    final TextPainter painter = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(
          color: color,
          fontSize: text.length > 8 ? 7 : 9,
          fontWeight: FontWeight.w700,
          letterSpacing: 0.8,
        ),
      ),
      maxLines: 1,
      textDirection: TextDirection.ltr,
    )..layout(maxWidth: (maxWidth ?? layout.actionRect.width) - 8);
    painter.paint(canvas, center - Offset(painter.width / 2, painter.height / 2));
  }

  @override
  bool shouldRepaint(_ControlPainter oldDelegate) {
    return oldDelegate.layout.joystickCenter != layout.joystickCenter ||
        oldDelegate.layout.joystickRadius != layout.joystickRadius ||
        oldDelegate.layout.joystickOpacity != layout.joystickOpacity ||
        oldDelegate.layout.actionRect != layout.actionRect ||
        oldDelegate.layout.jumpRect != layout.jumpRect ||
        oldDelegate.layout.dashRect != layout.dashRect ||
        oldDelegate.layout.dropRect != layout.dropRect ||
        oldDelegate.layout.actionOpacity != layout.actionOpacity ||
        oldDelegate.layout.jumpOpacity != layout.jumpOpacity ||
        oldDelegate.layout.dashOpacity != layout.dashOpacity ||
        oldDelegate.layout.dropOpacity != layout.dropOpacity ||
        oldDelegate.moveVector != moveVector ||
        oldDelegate.actionEnabled != actionEnabled ||
        oldDelegate.actionPressed != actionPressed ||
        oldDelegate.movementActionsEnabled != movementActionsEnabled ||
        oldDelegate.jumpPressed != jumpPressed ||
        oldDelegate.dashPressed != dashPressed ||
        oldDelegate.actionLabel != actionLabel ||
        oldDelegate.holdProgress != holdProgress ||
        oldDelegate.dropEnabled != dropEnabled ||
        oldDelegate.dropPressed != dropPressed;
  }
}
