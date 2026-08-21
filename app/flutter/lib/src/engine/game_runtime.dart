import 'dart:async';
import 'dart:ffi';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import '../input/input_router.dart';
import '../native/odg_bindings.dart';
import '../render/raster_budget.dart';
import 'game_snapshot.dart';

final class GameRuntime extends ChangeNotifier {
  GameRuntime._({
    required this.input,
    required this.api,
    required this.abi,
  })  : _frameCapacity = abi.maxRenderPixels * 4,
        _frameBuffer = calloc<Uint8>(abi.maxRenderPixels * 4),
        _statsBuffer = calloc<OdgGameStats>(),
        _inventoryBuffer = calloc<OdgInventorySnapshot>(),
        _interactionBuffer = calloc<OdgInteractionHint>(),
        _artifactBuffer = calloc<OdgArtifactSnapshot>(),
        _constructionBuffer = calloc<OdgConstructionSnapshot>(),
        _recipeBuffer = calloc<OdgRecipeDefinition>(),
        _storageBuffer = calloc<OdgStorageSnapshot>(),
        _repairBuffer = calloc<OdgRepairQuote>(),
        _commandBuffer = calloc<OdgCommand>(),
        _itemDefinitionBuffer = calloc<OdgItemDefinition>(),
        _required = calloc<Uint64>() {
    _ticker = Ticker(_onTick);
  }

  factory GameRuntime.open(MultiTouchInputRouter input) {
    final OdgNativeApi api = OdgNativeApi.open();
    final OdgAbiDescriptor abi = api.queryAndValidateAbi();
    final GameRuntime runtime = GameRuntime._(
      input: input,
      api: api,
      abi: abi,
    );
    runtime._initialize();
    return runtime;
  }

  static const List<String> leaderNames = <String>[
    'NOVA',
    'EMBER',
    'ORBIT',
    'KITE',
    'MOSS',
    'VANTA',
    'AURA',
    'HEX',
    'LYNX',
    'ONYX',
  ];

  final MultiTouchInputRouter input;
  final OdgNativeApi api;
  final OdgAbiDescriptor abi;
  final RasterGovernor _governor = RasterGovernor();
  final int _frameCapacity;
  final Pointer<Uint8> _frameBuffer;
  final Pointer<OdgGameStats> _statsBuffer;
  final Pointer<OdgInventorySnapshot> _inventoryBuffer;
  final Pointer<OdgInteractionHint> _interactionBuffer;
  final Pointer<OdgArtifactSnapshot> _artifactBuffer;
  final Pointer<OdgConstructionSnapshot> _constructionBuffer;
  final Pointer<OdgRecipeDefinition> _recipeBuffer;
  final Pointer<OdgStorageSnapshot> _storageBuffer;
  final Pointer<OdgRepairQuote> _repairBuffer;
  final Pointer<OdgCommand> _commandBuffer;
  final Pointer<OdgItemDefinition> _itemDefinitionBuffer;
  final Map<int, int> _itemCapabilityCache = <int, int>{};
  final Pointer<Uint64> _required;
  late final Ticker _ticker;

  ui.Image? _image;
  GameSnapshot _snapshot = const GameSnapshot.empty();
  RasterQuality _quality = RasterQuality.balanced;
  RenderSize _renderSize = const RenderSize(360, 640);
  Size _viewport = const Size(360, 640);
  bool _started = false;
  bool _lifecycleActive = true;
  bool _hostPaused = false;
  bool _decodePending = false;
  bool _disposed = false;
  int _constructionShape = odgConstructionShapeWall;
  Duration? _previousElapsed;

  ui.Image? get image => _image;
  GameSnapshot get snapshot => _snapshot;
  RasterQuality get quality => _quality;
  RenderSize get renderSize => _renderSize;
  bool get started => _started;
  int get constructionShape => _constructionShape;

  void _initialize() {
    _renderSize = RasterBudget.resolve(
      viewportAspect: _viewport.aspectRatio,
      abi: abi,
      quality: _quality,
    );
    final int status = api.init(
      _newSeed(),
      _renderSize.width,
      _renderSize.height,
    );
    if (status != odgStatusOk) {
      throw OdgNativeException('Engine initialization failed', status);
    }
    api.setVisualTheme(odgVisualThemeNeonTides);
    api.setPresentationMode(odgPresentationShowcase);
    _syncTicker();
  }

  int generateWorldSeed() => _newSeed();

  void startNewWorld(int seed) {
    input.clear();
    final int status = api.reset(seed == 0 ? 1 : seed);
    if (status != odgStatusOk) {
      _started = false;
      _previousElapsed = null;
      notifyListeners();
      throw OdgNativeException('World reset failed', status);
    }
    _constructionShape = odgConstructionShapeWall;
    api.setPresentationMode(odgPresentationGameplay);
    _started = true;
    _previousElapsed = null;
    notifyListeners();
  }

  void play() => startNewWorld(_newSeed());

  void restart() => play();

  void resumeLoadedWorld() {
    input.clear();
    api.setPresentationMode(odgPresentationGameplay);
    _started = true;
    _previousElapsed = null;
    notifyListeners();
  }

  void showMenu() {
    input.clear();
    api.setPresentationMode(odgPresentationShowcase);
    _started = false;
    _previousElapsed = null;
    notifyListeners();
  }

  void selectSlot(int slot) {
    _submitCommand(odgCommandSelectSlot, arg0: slot);
  }

  void setConstructionShape(int shape) {
    if (shape != odgConstructionShapeWall &&
        shape != odgConstructionShapeFloor &&
        shape != odgConstructionShapeDoorway &&
        shape != odgConstructionShapeRoof) {
      throw ArgumentError.value(shape, 'shape', 'Unknown construction shape');
    }
    if (_constructionShape == shape) return;
    _submitCommand(odgCommandSetConstructionShape, arg0: shape);
    _constructionShape = shape;
    notifyListeners();
  }

  void dropSelected() {
    _submitCommand(odgCommandDropSelected);
  }

  void placeSelected() => _submitCommand(odgCommandPlaceSelected);

  void useSelected() => _submitCommand(odgCommandUseSelected);

  void requestRespawn() => _submitCommand(odgCommandRequestRespawn);

  void consumeSelected() => _submitCommand(odgCommandConsumeSelected);

  void plantSelected() => _submitCommand(odgCommandPlantSelected);

  void drinkSelected() => _submitCommand(odgCommandDrinkSelected);

  int itemCapabilityBits(int typeId) {
    if (typeId <= odgItemNone) return 0;
    final int? cached = _itemCapabilityCache[typeId];
    if (cached != null) return cached;
    _required.value = 0;
    final int status = api.itemDefinitionGet(
      typeId,
      _itemDefinitionBuffer,
      sizeOf<OdgItemDefinition>(),
      _required,
    );
    if (status != odgStatusOk ||
        _required.value != sizeOf<OdgItemDefinition>() ||
        _itemDefinitionBuffer.ref.structSize != sizeOf<OdgItemDefinition>() ||
        _itemDefinitionBuffer.ref.typeId != typeId) {
      return 0;
    }
    final int bits = _itemDefinitionBuffer.ref.capabilityBits;
    _itemCapabilityCache[typeId] = bits;
    return bits;
  }

  void equipBackpack() {
    _submitCommand(odgCommandEquipBackpack);
  }

  void moveSlot(int fromSlot, int toSlot) {
    _submitCommand(odgCommandMoveSlot, arg0: fromSlot, arg1: toSlot);
  }

  void craft(int recipeId, int quantity) {
    if (quantity <= 0) return;
    _submitCommand(odgCommandCraft, arg0: recipeId, arg1: quantity);
  }

  void repairSelected() {
    _submitCommand(odgCommandRepairSelected);
  }

  void closeArtifact() {
    _submitCommand(odgCommandCloseArtifact);
  }

  void storageDeposit(int artifactId, int inventorySlot, {int quantity = 0}) {
    _submitCommand(
      odgCommandStorageDeposit,
      arg0: artifactId,
      arg1: inventorySlot,
      arg2: quantity,
    );
  }

  void storageWithdraw(int artifactId, int storageSlot, {int quantity = 0}) {
    _submitCommand(
      odgCommandStorageWithdraw,
      arg0: artifactId,
      arg1: storageSlot,
      arg2: quantity,
    );
  }

  void setTurretMode(int turretId, int mode) {
    if (mode != odgTurretModeDefense && mode != odgTurretModeHarvest) return;
    _submitCommand(odgCommandSetTurretMode, arg0: turretId, arg1: mode);
  }

  List<GameRecipe> recipesForStation(int stationItemType) {
    final List<GameRecipe> recipes = <GameRecipe>[];
    for (int recipeId = 1; recipeId <= api.recipeCount(); recipeId += 1) {
      _required.value = 0;
      final int status = api.recipeGet(
        recipeId,
        _recipeBuffer,
        sizeOf<OdgRecipeDefinition>(),
        _required,
      );
      if (status != odgStatusOk ||
          _required.value != sizeOf<OdgRecipeDefinition>() ||
          _recipeBuffer.ref.structSize != sizeOf<OdgRecipeDefinition>()) {
        continue;
      }
      final OdgRecipeDefinition recipe = _recipeBuffer.ref;
      if (recipe.stationItemType != stationItemType) continue;
      final int ingredientCount = recipe.ingredientCount.clamp(0, 4).toInt();
      recipes.add(GameRecipe(
        recipeId: recipe.recipeId,
        stationItemType: recipe.stationItemType,
        displayCode: recipe.displayCode,
        outputItemType: recipe.outputItemType,
        outputQuantity: recipe.outputQuantity,
        outputMaterialTier: recipe.outputMaterialTier,
        ingredients: List<GameRecipeIngredient>.unmodifiable(<GameRecipeIngredient>[
          for (int i = 0; i < ingredientCount; i += 1)
            GameRecipeIngredient(
              itemType: recipe.ingredients[i].itemType,
              materialTier: recipe.ingredients[i].materialTier,
              quantity: recipe.ingredients[i].quantity,
            ),
        ]),
        maxCraftable: api.recipeMaxCraftable(0, recipe.recipeId),
      ));
    }
    return List<GameRecipe>.unmodifiable(recipes);
  }

  GameStorage? openedStorage() {
    final int artifactId = api.openedArtifactId();
    if (artifactId == 0xFFFFFFFF) return null;
    _required.value = 0;
    final int status = api.copyArtifactStorage(
      0,
      artifactId,
      _storageBuffer,
      sizeOf<OdgStorageSnapshot>(),
      _required,
    );
    if (status != odgStatusOk ||
        _required.value != sizeOf<OdgStorageSnapshot>() ||
        _storageBuffer.ref.structSize != sizeOf<OdgStorageSnapshot>()) {
      return null;
    }
    final OdgStorageSnapshot storage = _storageBuffer.ref;
    final int slotCount = storage.slotCount.clamp(0, odgChestSlots).toInt();
    return GameStorage(
      artifactId: storage.artifactId,
      slotCount: slotCount,
      usedSlots: storage.usedSlots,
      slots: List<GameItemStack>.unmodifiable(<GameItemStack>[
        for (int i = 0; i < slotCount; i += 1)
          GameItemStack.fromNative(storage.slots[i]),
      ]),
    );
  }

  GameRepairQuote? repairQuote() {
    _required.value = 0;
    final int status = api.repairQuoteSelected(
      0,
      _repairBuffer,
      sizeOf<OdgRepairQuote>(),
      _required,
    );
    if (status != odgStatusOk ||
        _required.value != sizeOf<OdgRepairQuote>() ||
        _repairBuffer.ref.structSize != sizeOf<OdgRepairQuote>()) {
      return null;
    }
    final OdgRepairQuote q = _repairBuffer.ref;
    return GameRepairQuote(
      itemType: q.itemType,
      materialTier: q.materialTier,
      durabilityBefore: q.durabilityBefore,
      durabilityAfter: q.durabilityAfter,
      costItemType: q.costItemType,
      costQuantity: q.costQuantity,
      stationItemType: q.stationItemType,
    );
  }

  Uint8List saveBlob() {
    final int size = api.saveBlobSize();
    if (size <= 0) throw const OdgNativeException('Native save size is invalid.');
    final Pointer<Uint8> blob = calloc<Uint8>(size);
    try {
      _required.value = 0;
      final int status = api.saveWrite(blob, size, _required);
      if (status != odgStatusOk || _required.value != size) {
        throw OdgNativeException('Native save serialization failed', status);
      }
      return Uint8List.fromList(blob.asTypedList(size));
    } finally {
      calloc.free(blob);
    }
  }

  void loadBlob(Uint8List bytes) {
    if (bytes.isEmpty) throw const OdgNativeException('Save blob is empty.');
    final Pointer<Uint8> blob = calloc<Uint8>(bytes.length);
    try {
      blob.asTypedList(bytes.length).setAll(0, bytes);
      final int status = api.saveLoad(blob, bytes.length);
      if (status != odgStatusOk) {
        throw OdgNativeException('Native save load failed', status);
      }
      input.clear();
      _constructionShape = odgConstructionShapeWall;
      _previousElapsed = null;
      notifyListeners();
    } finally {
      calloc.free(blob);
    }
  }

  GameMapData queryMap({
    required int minXMilli,
    required int minZMilli,
    required int maxXMilli,
    required int maxZMilli,
    int width = 64,
    int height = 64,
  }) {
    final int w = width.clamp(8, odgMapMaxResolution).toInt();
    final int h = height.clamp(8, odgMapMaxResolution).toInt();
    final Pointer<OdgMapQueryDesc> query = calloc<OdgMapQueryDesc>();
    final Pointer<OdgMapSample> samples = calloc<OdgMapSample>(w * h);
    final Pointer<OdgMapMarker> markers = calloc<OdgMapMarker>(odgMapMaxMarkers);
    final Pointer<Uint32> markerCount = calloc<Uint32>();
    try {
      query.ref
        ..structSize = sizeOf<OdgMapQueryDesc>()
        ..minXMilli = minXMilli
        ..minZMilli = minZMilli
        ..maxXMilli = maxXMilli
        ..maxZMilli = maxZMilli
        ..width = w
        ..height = h
        ..reservedU32 = 0;
      _required.value = 0;
      markerCount.value = 0;
      final int status = api.mapQuery(
        query,
        samples,
        w * h,
        _required,
        markers,
        odgMapMaxMarkers,
        markerCount,
      );
      if (status != odgStatusOk || _required.value != w * h) {
        throw OdgNativeException('Native map query failed', status);
      }
      final int markersUsed = markerCount.value.clamp(0, odgMapMaxMarkers).toInt();
      return GameMapData(
        minXMilli: minXMilli,
        minZMilli: minZMilli,
        maxXMilli: maxXMilli,
        maxZMilli: maxZMilli,
        width: w,
        height: h,
        samples: List<GameMapSample>.unmodifiable(<GameMapSample>[
          for (int i = 0; i < w * h; i += 1)
            GameMapSample(
              ownerActorPlusOne: samples[i].ownerActorPlusOne,
              flags: samples[i].flags,
              heightMilli: samples[i].heightMilli,
            ),
        ]),
        markers: List<GameMapMarker>.unmodifiable(<GameMapMarker>[
          for (int i = 0; i < markersUsed; i += 1)
            GameMapMarker(
              kind: markers[i].kind,
              id: markers[i].id,
              ownerActorId: markers[i].ownerActorId,
              materialTier: markers[i].materialTier,
              xMilli: markers[i].xMilli,
              zMilli: markers[i].zMilli,
              state: markers[i].state,
            ),
        ]),
      );
    } finally {
      calloc.free(markerCount);
      calloc.free(markers);
      calloc.free(samples);
      calloc.free(query);
    }
  }

  void uploadAvatarTexture(int face, Uint8List rgba) {
    if (face < 0 || face > odgAvatarFaceBottom ||
        rgba.length != odgAvatarTextureSize * odgAvatarTextureSize * 4) {
      throw const OdgNativeException('Avatar texture must be 256x256 RGBA8.');
    }
    final Pointer<Uint8> pixels = calloc<Uint8>(rgba.length);
    try {
      pixels.asTypedList(rgba.length).setAll(0, rgba);
      final int status = api.avatarTextureUpload(
        face,
        pixels,
        odgAvatarTextureSize,
        odgAvatarTextureSize,
        odgAvatarTextureSize * 4,
      );
      if (status != odgStatusOk) {
        throw OdgNativeException('Avatar texture upload failed', status);
      }
    } finally {
      calloc.free(pixels);
    }
  }

  void clearAvatarTexture(int face) {
    final int status = api.avatarTextureClear(face);
    if (status != odgStatusOk) {
      throw OdgNativeException('Avatar texture clear failed', status);
    }
  }

  void _submitCommand(
    int type, {
    int arg0 = 0,
    int arg1 = 0,
    int arg2 = 0,
    int arg3 = 0,
    int payload = 0,
  }) {
    final OdgCommand command = _commandBuffer.ref;
    command
      ..structSize = sizeOf<OdgCommand>()
      ..type = type
      ..arg0 = arg0
      ..arg1 = arg1
      ..arg2 = arg2
      ..arg3 = arg3
      ..payload = payload;
    final int status = api.commandSubmit(_commandBuffer, sizeOf<OdgCommand>());
    if (status != odgStatusOk) {
      throw OdgNativeException('Native command queue rejected command $type', status);
    }
  }

  void setTheme(int theme) {
    api.setVisualTheme(theme.clamp(0, odgVisualThemeCount - 1).toInt());
    notifyListeners();
  }

  void setCameraMode(int mode) {
    api.setCameraMode(mode.clamp(0, odgCameraModeCount - 1).toInt());
    notifyListeners();
  }

  int get cameraMode => api.cameraMode();

  void setMusicReactivity(double amount) {
    final double bounded = amount.clamp(0.0, 1.5).toDouble();
    api.setMusicReactivity((bounded * 65535.0).round());
    notifyListeners();
  }

  double get musicReactivity => api.musicReactivity() / 65535.0;

  Future<ui.Image?> renderArtifactView(int artifactId) async {
    const int bytes = odgRemoteViewWidth * odgRemoteViewHeight * 4;
    final Pointer<Uint8> buffer = calloc<Uint8>(bytes);
    try {
      _required.value = 0;
      final int status = api.renderArtifactView(
        artifactId,
        buffer,
        bytes,
        _required,
      );
      if (status != odgStatusOk || _required.value != bytes) return null;
      final Uint8List pixels = Uint8List.fromList(buffer.asTypedList(bytes));
      // Restore the primary framebuffer immediately; the remote render is a secondary
      // presentation pass and must never replace the gameplay frame retained by the host.
      api.renderFrame();
      ui.ImmutableBuffer? immutable;
      ui.ImageDescriptor? descriptor;
      ui.Codec? codec;
      try {
        immutable = await ui.ImmutableBuffer.fromUint8List(pixels);
        descriptor = ui.ImageDescriptor.raw(
          immutable,
          width: odgRemoteViewWidth,
          height: odgRemoteViewHeight,
          rowBytes: odgRemoteViewWidth * 4,
          pixelFormat: ui.PixelFormat.rgba8888,
        );
        codec = await descriptor.instantiateCodec();
        final ui.FrameInfo frame = await codec.getNextFrame();
        return frame.image;
      } finally {
        codec?.dispose();
        descriptor?.dispose();
        immutable?.dispose();
      }
    } finally {
      calloc.free(buffer);
    }
  }

  Future<ui.Image?> renderAvatarPreview(int yawQ16) async {
    const int bytes = odgAvatarPreviewSize * odgAvatarPreviewSize * 4;
    final Pointer<Uint8> buffer = calloc<Uint8>(bytes);
    try {
      _required.value = 0;
      final int status = api.renderAvatarPreview(yawQ16 & 0xFFFF, buffer, bytes, _required);
      if (status != odgStatusOk || _required.value != bytes) return null;
      final Uint8List pixels = Uint8List.fromList(buffer.asTypedList(bytes));
      api.renderFrame();
      ui.ImmutableBuffer? immutable; ui.ImageDescriptor? descriptor; ui.Codec? codec;
      try {
        immutable = await ui.ImmutableBuffer.fromUint8List(pixels);
        descriptor = ui.ImageDescriptor.raw(immutable, width: odgAvatarPreviewSize, height: odgAvatarPreviewSize, rowBytes: odgAvatarPreviewSize * 4, pixelFormat: ui.PixelFormat.rgba8888);
        codec = await descriptor.instantiateCodec();
        return (await codec.getNextFrame()).image;
      } finally { codec?.dispose(); descriptor?.dispose(); immutable?.dispose(); }
    } finally { calloc.free(buffer); }
  }


  Future<ui.Image?> renderCameraPreview(int mode, int yawQ16, int pitchQ15) async {
    const int bytes = odgRemoteViewWidth * odgRemoteViewHeight * 4;
    final Pointer<Uint8> buffer = calloc<Uint8>(bytes);
    try {
      _required.value = 0;
      final int status = api.renderCameraPreview(
        mode.clamp(0, odgCameraModeCount - 1).toInt(), yawQ16 & 0xFFFF, pitchQ15,
        buffer, bytes, _required,
      );
      if (status != odgStatusOk || _required.value != bytes) return null;
      final Uint8List pixels = Uint8List.fromList(buffer.asTypedList(bytes));
      api.renderFrame();
      ui.ImmutableBuffer? immutable; ui.ImageDescriptor? descriptor; ui.Codec? codec;
      try {
        immutable = await ui.ImmutableBuffer.fromUint8List(pixels);
        descriptor = ui.ImageDescriptor.raw(immutable, width: odgRemoteViewWidth, height: odgRemoteViewHeight, rowBytes: odgRemoteViewWidth * 4, pixelFormat: ui.PixelFormat.rgba8888);
        codec = await descriptor.instantiateCodec();
        return (await codec.getNextFrame()).image;
      } finally { codec?.dispose(); descriptor?.dispose(); immutable?.dispose(); }
    } finally { calloc.free(buffer); }
  }
  void setQuality(RasterQuality quality) {
    if (_quality == quality) return;
    _quality = quality;
    _governor.reset();
    _applyRenderSize();
    notifyListeners();
  }

  void setViewport(Size viewport) {
    if (viewport.width <= 0 || viewport.height <= 0) return;
    final bool materiallyChanged =
        (_viewport.aspectRatio - viewport.aspectRatio).abs() > 0.002;
    _viewport = viewport;
    if (materiallyChanged) _applyRenderSize();
  }

  void setLifecycleActive(bool active) {
    if (_lifecycleActive == active) return;
    _lifecycleActive = active;
    if (!active) input.clear();
    _syncTicker();
  }

  void setHostPaused(bool paused) {
    if (_hostPaused == paused) return;
    _hostPaused = paused;
    if (paused) input.clear();
    _syncTicker();
  }

  void _syncTicker() {
    final bool shouldRun = _lifecycleActive && !_hostPaused && !_disposed;
    _previousElapsed = null;
    if (shouldRun && !_ticker.isActive) {
      _ticker.start();
    } else if (!shouldRun && _ticker.isActive) {
      _ticker.stop();
      api.setInput(0, 0, 0, 0, 0);
    }
  }

  void _onTick(Duration elapsed) {
    if (_disposed || !_lifecycleActive || _hostPaused) return;
    final Duration? previous = _previousElapsed;
    _previousElapsed = elapsed;
    if (previous == null) return;
    final int elapsedUs =
        (elapsed - previous).inMicroseconds.clamp(0, 50000).toInt();
    final GameInputSample sample = input.sample();
    api.setInput(
      sample.moveXQ15,
      sample.moveForwardQ15,
      sample.lookXQ15,
      sample.lookYQ15,
      sample.buttons,
    );
    api.tickUs(elapsedUs);
    if (!_decodePending) _renderAndPublish();
  }

  void _renderAndPublish() {
    final Stopwatch watch = Stopwatch()..start();
    if (api.renderFrame() == 0) return;
    final int width = api.renderWidth();
    final int height = api.renderHeight();
    final int bytes = api.framebufferBytes();
    final int stride = api.framebufferStrideBytes();
    if (width <= 0 ||
        height <= 0 ||
        stride != width * 4 ||
        bytes != stride * height ||
        bytes > _frameCapacity) {
      throw const OdgNativeException('Native framebuffer metadata is invalid.');
    }
    _required.value = 0;
    final int copyStatus = api.copyFramebuffer(
      _frameBuffer,
      _frameCapacity,
      _required,
    );
    if (copyStatus != odgStatusOk || _required.value != bytes) {
      throw OdgNativeException('Framebuffer copy failed', copyStatus);
    }
    final Uint8List pixels = Uint8List.fromList(
      _frameBuffer.asTypedList(bytes),
    );
    _snapshot = _readSnapshot();
    watch.stop();
    if (_governor.observe(watch.elapsedMicroseconds)) _applyRenderSize();

    _decodePending = true;
    unawaited(_decodeAndPublish(pixels, width, height, stride));
  }

  Future<void> _decodeAndPublish(
    Uint8List pixels,
    int width,
    int height,
    int stride,
  ) async {
    ui.ImmutableBuffer? buffer;
    ui.ImageDescriptor? descriptor;
    ui.Codec? codec;
    ui.Image? decoded;
    try {
      buffer = await ui.ImmutableBuffer.fromUint8List(pixels);
      descriptor = ui.ImageDescriptor.raw(
        buffer,
        width: width,
        height: height,
        rowBytes: stride,
        pixelFormat: ui.PixelFormat.rgba8888,
      );
      codec = await descriptor.instantiateCodec();
      final ui.FrameInfo frame = await codec.getNextFrame();
      decoded = frame.image;
    } on Object catch (error, stackTrace) {
      if (!_disposed) {
        FlutterError.reportError(
          FlutterErrorDetails(
            exception: error,
            stack: stackTrace,
            library: 'ODPAR native framebuffer',
            context: ErrorDescription('while decoding an RGBA8 frame'),
          ),
        );
      }
    } finally {
      codec?.dispose();
      descriptor?.dispose();
      buffer?.dispose();
    }

    if (_disposed) {
      decoded?.dispose();
      return;
    }
    _decodePending = false;
    if (decoded == null) return;
    final ui.Image? previous = _image;
    _image = decoded;
    previous?.dispose();
    notifyListeners();
  }

  GameSnapshot _readSnapshot() {
    _required.value = 0;
    final int status = api.copyStats(
      _statsBuffer,
      sizeOf<OdgGameStats>(),
      _required,
    );
    if (status != odgStatusOk ||
        _required.value != sizeOf<OdgGameStats>() ||
        _statsBuffer.ref.structSize != sizeOf<OdgGameStats>() ||
        _statsBuffer.ref.apiVersion != odgApiVersion) {
      throw OdgNativeException('Stats snapshot copy failed', status);
    }
    final int count = math.min(3, api.leaderCount()).toInt();
    final List<GameLeader> leaders = <GameLeader>[
      for (int rank = 0; rank < count; rank += 1)
        GameLeader(
          score: api.leaderScore(rank),
          nameCode: api.leaderNameCode(rank),
          isPlayer: api.leaderIsPlayer(rank) != 0,
        ),
    ];
    _required.value = 0;
    final int inventoryStatus = api.copyInventory(
      0,
      _inventoryBuffer,
      sizeOf<OdgInventorySnapshot>(),
      _required,
    );
    if (inventoryStatus != odgStatusOk ||
        _required.value != sizeOf<OdgInventorySnapshot>() ||
        _inventoryBuffer.ref.structSize != sizeOf<OdgInventorySnapshot>()) {
      throw OdgNativeException('Inventory snapshot copy failed', inventoryStatus);
    }
    final OdgInventorySnapshot nativeInventory = _inventoryBuffer.ref;
    final int slotCount = nativeInventory.slotCount
        .clamp(odgInventoryBaseSlots, odgInventoryMaxSlots)
        .toInt();
    final List<GameItemStack> slots = <GameItemStack>[
      for (int slot = 0; slot < slotCount; slot += 1)
        GameItemStack.fromNative(nativeInventory.slots[slot]),
    ];
    final GameInventory inventory = GameInventory(
      slotCount: slotCount,
      baseSlotCount: nativeInventory.baseSlotCount,
      selectedSlot: nativeInventory.selectedSlot,
      equippedBackpackType: nativeInventory.equippedBackpackType,
      slots: List<GameItemStack>.unmodifiable(slots),
    );

    _required.value = 0;
    final int interactionStatus = api.copyInteractionHint(
      _interactionBuffer,
      sizeOf<OdgInteractionHint>(),
      _required,
    );
    if (interactionStatus != odgStatusOk ||
        _required.value != sizeOf<OdgInteractionHint>() ||
        _interactionBuffer.ref.structSize != sizeOf<OdgInteractionHint>()) {
      throw OdgNativeException(
        'Interaction hint copy failed',
        interactionStatus,
      );
    }
    final OdgInteractionHint nativeInteraction = _interactionBuffer.ref;
    final GameInteraction interaction = GameInteraction(
      action: nativeInteraction.action,
      targetKind: nativeInteraction.targetKind,
      targetId: nativeInteraction.targetId,
      valid: nativeInteraction.valid != 0,
      requiresHold: nativeInteraction.requiresHold != 0,
      progressTicks: nativeInteraction.progressTicks,
      thresholdTicks: nativeInteraction.thresholdTicks,
      messageCode: nativeInteraction.messageCode,
    );
    final List<GameArtifact> artifacts = <GameArtifact>[];
    int artifactOffset = 0;
    int artifactTotal = 0;
    int openedArtifactId = 0xffffffff;
    do {
      _required.value = 0;
      final int artifactStatus = api.copyArtifactsPage(
        artifactOffset,
        _artifactBuffer,
        sizeOf<OdgArtifactSnapshot>(),
        _required,
      );
      if (artifactStatus != odgStatusOk ||
          _required.value != sizeOf<OdgArtifactSnapshot>() ||
          _artifactBuffer.ref.structSize != sizeOf<OdgArtifactSnapshot>()) {
        throw OdgNativeException('Artifact snapshot page failed', artifactStatus);
      }
      final OdgArtifactSnapshot nativeArtifacts = _artifactBuffer.ref;
      openedArtifactId = nativeArtifacts.openedArtifactId;
      artifactTotal = nativeArtifacts.totalCount;
      final int pageCount = nativeArtifacts.count.clamp(0, odgArtifactMaxEntries).toInt();
      for (int i = 0; i < pageCount; i += 1) {
        artifacts.add(GameArtifact.fromNative(nativeArtifacts.entries[i]));
      }
      artifactOffset += pageCount;
      if (pageCount == 0 && artifactOffset < artifactTotal) {
        throw const OdgNativeException('Artifact paging made no progress.');
      }
    } while (artifactOffset < artifactTotal);

    final List<GameConstruction> constructions = <GameConstruction>[];
    int constructionOffset = 0;
    int constructionTotal = 0;
    do {
      _required.value = 0;
      final int constructionStatus = api.copyConstructionPage(
        constructionOffset,
        _constructionBuffer,
        sizeOf<OdgConstructionSnapshot>(),
        _required,
      );
      if (constructionStatus != odgStatusOk ||
          _required.value != sizeOf<OdgConstructionSnapshot>() ||
          _constructionBuffer.ref.structSize != sizeOf<OdgConstructionSnapshot>()) {
        throw OdgNativeException('Construction snapshot page failed', constructionStatus);
      }
      final OdgConstructionSnapshot nativeConstruction = _constructionBuffer.ref;
      constructionTotal = nativeConstruction.totalCount;
      _constructionShape = nativeConstruction.selectedShape;
      final int pageCount = nativeConstruction.count.clamp(0, odgConstructionMaxEntries).toInt();
      for (int i = 0; i < pageCount; i += 1) {
        constructions.add(GameConstruction.fromNative(nativeConstruction.entries[i]));
      }
      constructionOffset += pageCount;
      if (pageCount == 0 && constructionOffset < constructionTotal) {
        throw const OdgNativeException('Construction paging made no progress.');
      }
    } while (constructionOffset < constructionTotal);
    return GameSnapshot.fromNative(
      _statsBuffer.ref,
      inventory,
      interaction,
      leaders,
      artifacts: List<GameArtifact>.unmodifiable(artifacts),
      constructions: List<GameConstruction>.unmodifiable(constructions),
      openedArtifactId: openedArtifactId,
    );
  }

  void _applyRenderSize() {
    final RenderSize requested = RasterBudget.resolve(
      viewportAspect: _viewport.aspectRatio,
      abi: abi,
      quality: _quality,
      adaptiveScale: _governor.scale,
    );
    if (requested == _renderSize) return;
    final int status = api.resize(requested.width, requested.height);
    if (status != odgStatusOk) {
      throw OdgNativeException('Native resize rejected $requested', status);
    }
    _renderSize = requested;
  }

  int _newSeed() {
    final math.Random random = math.Random.secure();
    final int high = random.nextInt(0x7fffffff);
    final int low = random.nextInt(0x7fffffff);
    final int seed = (high << 31) | low;
    return seed == 0 ? 1 : seed;
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    if (_ticker.isActive) _ticker.stop();
    _ticker.dispose();
    _image?.dispose();
    calloc.free(_required);
    calloc.free(_itemDefinitionBuffer);
    calloc.free(_commandBuffer);
    calloc.free(_repairBuffer);
    calloc.free(_storageBuffer);
    calloc.free(_recipeBuffer);
    calloc.free(_constructionBuffer);
    calloc.free(_artifactBuffer);
    calloc.free(_interactionBuffer);
    calloc.free(_inventoryBuffer);
    calloc.free(_statsBuffer);
    calloc.free(_frameBuffer);
    super.dispose();
  }
}
