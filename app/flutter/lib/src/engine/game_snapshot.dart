import '../native/odg_bindings.dart';

String gameMaterialName(int materialTier) => switch (materialTier) {
      odgMaterialWood => 'Madera',
      odgMaterialStone => 'Piedra',
      odgMaterialIron => 'Hierro',
      _ => '',
    };

String gameItemName(int typeId) => switch (typeId) {
      odgItemWood => 'Madera',
      odgItemStone => 'Piedra',
      odgItemIron => 'Hierro',
      odgItemAmmo => 'Munición',
      odgItemReprogramChip => 'Chip de reprogramación',
      odgItemAscensionChip => 'Chip de ascenso',
      odgItemAxe => 'Hacha',
      odgItemPickaxe => 'Pico',
      odgItemTurret => 'Torreta',
      odgItemWorkbench => 'Mesa de trabajo',
      odgItemSmithy => 'Mesa de herrería',
      odgItemChest => 'Cofre',
      odgItemBackpack => 'Mochila',
      odgItemApple => 'Manzana',
      odgItemAppleSeed => 'Semilla de manzana',
      odgItemBirdTrap => 'Trampa para aves',
      odgItemLeather => 'Cuero',
      odgItemRawMeat => 'Carne cruda',
      odgItemHuntingKnife => 'Cuchillo de caza',
      odgItemSword => 'Espada',
      odgItemWaterFlask => 'Cantimplora',
      odgItemRainBarrel => 'Barril de lluvia',
      odgItemCoal => 'Carbón',
      odgItemTorch => 'Antorcha',
      odgItemNightShard => 'Fragmento nocturno',
      odgItemRawFish => 'Pescado crudo',
      odgItemBuildingBlock => 'Bloque de construcción',
      odgItemRaft => 'Balsa',
      _ => 'Objeto',
    };

bool gameItemShowsMaterial(int typeId) => switch (typeId) {
      odgItemReprogramChip ||
      odgItemAscensionChip ||
      odgItemAxe ||
      odgItemPickaxe ||
      odgItemTurret ||
      odgItemSword ||
      odgItemBuildingBlock => true,
      _ => false,
    };


final class GameLeader {
  const GameLeader({
    required this.score,
    required this.nameCode,
    required this.isPlayer,
  });

  final int score;
  final int nameCode;
  final bool isPlayer;
}

final class GameItemStack {
  const GameItemStack({
    required this.typeId,
    required this.quantity,
    required this.materialTier,
    required this.durability,
    required this.maxDurability,
    required this.flags,
    required this.instanceId,
    required this.payloadId,
  });

  const GameItemStack.empty()
      : typeId = odgItemNone,
        quantity = 0,
        materialTier = odgMaterialNone,
        durability = 0,
        maxDurability = 0,
        flags = 0,
        instanceId = 0,
        payloadId = 0;

  factory GameItemStack.fromNative(OdgItemStack stack) => GameItemStack(
        typeId: stack.typeId,
        quantity: stack.quantity,
        materialTier: stack.materialTier,
        durability: stack.durability,
        maxDurability: stack.maxDurability,
        flags: stack.flags,
        instanceId: stack.instanceId,
        payloadId: stack.payloadId,
      );

  final int typeId;
  final int quantity;
  final int materialTier;
  final int durability;
  final int maxDurability;
  final int flags;
  final int instanceId;
  final int payloadId;

  bool get isEmpty => typeId == odgItemNone || quantity <= 0;
  bool get usesDurability => maxDurability > 0;
  int get fluidUnits => payloadId & 0xFFFFFFFF;
  double get durabilityFraction =>
      maxDurability <= 0 ? 1.0 : (durability / maxDurability).clamp(0.0, 1.0).toDouble();

  String get materialName => gameMaterialName(materialTier);

  String get itemName => gameItemName(typeId);

  String get displayLabel {
    if (isEmpty) return 'Vacío';
    final String tier = materialName;
    final String base = tier.isEmpty || !gameItemShowsMaterial(typeId)
        ? itemName
        : '$itemName de ${tier.toLowerCase()}';
    if (quantity > 1) return '$base ×$quantity';
    if (usesDurability) return '$base · $durability/$maxDurability';
    return base;
  }


}

final class GameInventory {
  const GameInventory({
    required this.slotCount,
    required this.baseSlotCount,
    required this.selectedSlot,
    required this.equippedBackpackType,
    required this.slots,
  });

  const GameInventory.empty()
      : slotCount = odgInventoryBaseSlots,
        baseSlotCount = odgInventoryBaseSlots,
        selectedSlot = 0,
        equippedBackpackType = 0,
        slots = const <GameItemStack>[
          GameItemStack.empty(),
          GameItemStack.empty(),
          GameItemStack.empty(),
          GameItemStack.empty(),
        ];

  final int slotCount;
  final int baseSlotCount;
  final int selectedSlot;
  final int equippedBackpackType;
  final List<GameItemStack> slots;

  bool get hasBackpack => slotCount > baseSlotCount || equippedBackpackType != 0;
  GameItemStack get selected =>
      selectedSlot >= 0 && selectedSlot < slots.length ? slots[selectedSlot] : const GameItemStack.empty();
}

final class GameInteraction {
  const GameInteraction({
    required this.action,
    required this.targetKind,
    required this.targetId,
    required this.valid,
    required this.requiresHold,
    required this.progressTicks,
    required this.thresholdTicks,
    required this.messageCode,
  });

  const GameInteraction.none()
      : action = odgInteractionNone,
        targetKind = odgInteractionTargetNone,
        targetId = 0,
        valid = false,
        requiresHold = false,
        progressTicks = 0,
        thresholdTicks = 0,
        messageCode = 0;

  final int action;
  final int targetKind;
  final int targetId;
  final bool valid;
  final bool requiresHold;
  final int progressTicks;
  final int thresholdTicks;
  final int messageCode;

  double get progress => thresholdTicks <= 0
      ? 0
      : (progressTicks / thresholdTicks).clamp(0.0, 1.0);

  String get label => switch (action) {
        odgInteractionPickup || odgInteractionPickupArtifact => 'RECOGER',
        odgInteractionPlace || odgInteractionPlaceArtifact || odgInteractionPlaceConstruction => 'COLOCAR',
        odgInteractionReprogram => 'REPROGRAMAR',
        odgInteractionUpgrade => 'ASCENDER',
        odgInteractionRefill => 'RECARGAR',
        odgInteractionHarvest => 'EXTRAER',
        odgInteractionOpenArtifact => 'ABRIR',
        odgInteractionGatherFruit => 'COSECHAR',
        odgInteractionHuntFauna => 'CAZAR',
        odgInteractionAttackActor || odgInteractionAttackConstruction => 'GOLPEAR',
        odgInteractionCollectWater => 'RECOGER AGUA',
        odgInteractionIrrigate => 'REGAR',
        odgInteractionDrinkWater => 'BEBER',
        odgInteractionUseVehicle => 'USAR',
        odgInteractionDismantleConstruction => 'DESMONTAR',
        odgInteractionRepairConstruction => 'REPARAR',
        _ => 'INTERACTUAR',
      };


  String get messageLabel => switch (messageCode) {
        odgMessageNone => '',
        odgMessageWrongTier => 'NIVEL INCORRECTO',
        odgMessageWrongUpgrade => 'MEJORA INCORRECTA',
        odgMessageToolRequired => 'HERRAMIENTA REQUERIDA',
        odgMessagePickaxeTierRequired => 'PICO DE MAYOR NIVEL',
        odgMessageInventoryFull => 'INVENTARIO LLENO',
        odgMessageOwnerOnly => 'SOLO EL DUEÑO',
        odgMessageEmptyChestToMove => 'VACÍA EL COFRE PRIMERO',
        odgMessageInvalidPlacement => 'POSICIÓN BLOQUEADA',
        odgMessageStationRequired => 'ESTACIÓN REQUERIDA',
        odgMessageMissingResources => 'FALTAN RECURSOS',
        odgMessageTerritoryRequired => 'CONTROL TERRITORIAL REQUERIDO',
        odgMessageTooDry => 'NO HAY AGUA SUFICIENTE',
        odgMessageTooSteep => 'TERRENO DEMASIADO INCLINADO',
        odgMessageNotHungry => 'SIN HAMBRE',
        odgMessageNotThirsty => 'SIN SED',
        _ => 'ACCIÓN NO DISPONIBLE',
      };
}

final class GameArtifact {
  const GameArtifact({
    required this.instanceId,
    required this.artifactId,
    required this.itemType,
    required this.ownerActorId,
    required this.materialTier,
    required this.capabilityBits,
    required this.xMilli,
    required this.zMilli,
    required this.storageUsed,
    required this.state,
  });

  factory GameArtifact.fromNative(OdgArtifactEntry entry) => GameArtifact(
        instanceId: entry.instanceId,
        artifactId: entry.artifactId,
        itemType: entry.itemType,
        ownerActorId: entry.ownerActorId,
        materialTier: entry.materialTier,
        capabilityBits: entry.capabilityBits,
        xMilli: entry.xMilli,
        zMilli: entry.zMilli,
        storageUsed: entry.storageUsed,
        state: entry.state,
      );

  final int instanceId;
  final int artifactId;
  final int itemType;
  final int ownerActorId;
  final int materialTier;
  final int capabilityBits;
  final int xMilli;
  final int zMilli;
  final int storageUsed;
  final int state;

  String get name => gameItemName(itemType);
}

final class GameConstruction {
  const GameConstruction({
    required this.instanceId,
    required this.constructionId,
    required this.ownerActorId,
    required this.controllerActorId,
    required this.materialTier,
    required this.shape,
    required this.xMilli,
    required this.zMilli,
    required this.state,
    required this.health,
    required this.maxHealth,
  });

  factory GameConstruction.fromNative(OdgConstructionEntry entry) => GameConstruction(
        instanceId: entry.instanceId,
        constructionId: entry.constructionId,
        ownerActorId: entry.ownerActorId,
        controllerActorId: entry.controllerActorId,
        materialTier: entry.materialTier,
        shape: entry.shape,
        xMilli: entry.xMilli,
        zMilli: entry.zMilli,
        state: entry.state,
        health: entry.health,
        maxHealth: entry.maxHealth,
      );

  final int instanceId;
  final int constructionId;
  final int ownerActorId;
  final int controllerActorId;
  final int materialTier;
  final int shape;
  final int xMilli;
  final int zMilli;
  final int state;
  final int health;
  final int maxHealth;

  double get healthFraction => maxHealth <= 0 ? 0 : (health / maxHealth).clamp(0.0, 1.0).toDouble();

  int get healthPercent => (healthFraction * 100).round();

  String get shapeLabel => switch (shape) {
        odgConstructionShapeWall => 'MURO',
        odgConstructionShapeFloor => 'SUELO',
        odgConstructionShapeDoorway => 'VANO',
        odgConstructionShapeRoof => 'TECHO',
        _ => 'ESTRUCTURA',
      };
}

final class GameRecipeIngredient {
  const GameRecipeIngredient({required this.itemType, required this.materialTier, required this.quantity});
  final int itemType;
  final int materialTier;
  final int quantity;

  String get label => GameItemStack(
        typeId: itemType,
        quantity: quantity,
        materialTier: materialTier,
        durability: 0,
        maxDurability: 0,
        flags: 0,
        instanceId: 0,
        payloadId: 0,
      ).displayLabel;
}

final class GameRecipe {
  const GameRecipe({
    required this.recipeId,
    required this.stationItemType,
    required this.displayCode,
    required this.outputItemType,
    required this.outputQuantity,
    required this.outputMaterialTier,
    required this.ingredients,
    required this.maxCraftable,
  });
  final int recipeId;
  final int stationItemType;
  final int displayCode;
  final int outputItemType;
  final int outputQuantity;
  final int outputMaterialTier;
  final List<GameRecipeIngredient> ingredients;
  final int maxCraftable;

  GameItemStack get output => GameItemStack(
        typeId: outputItemType,
        quantity: outputQuantity,
        materialTier: outputMaterialTier,
        durability: 0,
        maxDurability: 0,
        flags: 0,
        instanceId: 0,
        payloadId: 0,
      );
}

final class GameSnapshot {
  const GameSnapshot({
    required this.tick,
    required this.width,
    required this.height,
    required this.aliveCount,
    required this.playerAlive,
    required this.territoryTotalCells,
    required this.territoryPermille,
    required this.trailActive,
    required this.matchOver,
    required this.winnerId,
    required this.deathReason,
    required this.ownedTurrets,
    required this.nearbyTurretVisible,
    required this.nearbyTurretAmmo,
    required this.nearbyTurretMaxAmmo,
    required this.stateHash,
    required this.inventory,
    required this.interaction,
    required this.artifacts,
    required this.constructions,
    required this.openedArtifactId,
    required this.leaders,
  });

  const GameSnapshot.empty()
      : tick = 0,
        width = 0,
        height = 0,
        aliveCount = 10,
        playerAlive = true,
        territoryTotalCells = 1,
        territoryPermille = 0,
        trailActive = false,
        matchOver = false,
        winnerId = 0,
        deathReason = 0,
        ownedTurrets = 0,
        nearbyTurretVisible = false,
        nearbyTurretAmmo = 0,
        nearbyTurretMaxAmmo = 0,
        stateHash = 0,
        inventory = const GameInventory.empty(),
        interaction = const GameInteraction.none(),
        artifacts = const <GameArtifact>[],
        constructions = const <GameConstruction>[],
        openedArtifactId = 0xFFFFFFFF,
        leaders = const <GameLeader>[];

  factory GameSnapshot.fromNative(
    OdgGameStats stats,
    GameInventory inventory,
    GameInteraction interaction,
    List<GameLeader> leaders, {
    List<GameArtifact> artifacts = const <GameArtifact>[],
    List<GameConstruction> constructions = const <GameConstruction>[],
    int openedArtifactId = 0xFFFFFFFF,
  }) {
    return GameSnapshot(
      tick: stats.tick,
      width: stats.width,
      height: stats.height,
      aliveCount: stats.aliveCount,
      playerAlive: stats.playerAlive != 0,
      territoryTotalCells: stats.territoryTotalCells,
      territoryPermille: stats.territoryPermille,
      trailActive: stats.playerTrailActive != 0,
      matchOver: stats.matchOver != 0,
      winnerId: stats.winnerId,
      deathReason: stats.playerDeathReason,
      ownedTurrets: stats.playerOwnedTurrets,
      nearbyTurretVisible: stats.nearbyOwnedTurretVisible != 0,
      nearbyTurretAmmo: stats.nearbyOwnedTurretAmmo,
      nearbyTurretMaxAmmo: stats.nearbyOwnedTurretMaxAmmo,
      stateHash: stats.deterministicStateHash,
      inventory: inventory,
      interaction: interaction,
      artifacts: List<GameArtifact>.unmodifiable(artifacts),
      constructions: List<GameConstruction>.unmodifiable(constructions),
      openedArtifactId: openedArtifactId,
      leaders: List<GameLeader>.unmodifiable(leaders),
    );
  }

  final int tick;
  final int width;
  final int height;
  final int aliveCount;
  final bool playerAlive;
  final int territoryTotalCells;
  final int territoryPermille;
  final bool trailActive;
  final bool matchOver;
  final int winnerId;
  final int deathReason;
  final int ownedTurrets;
  final bool nearbyTurretVisible;
  final int nearbyTurretAmmo;
  final int nearbyTurretMaxAmmo;
  final int stateHash;
  final GameInventory inventory;
  final GameInteraction interaction;
  final List<GameArtifact> artifacts;
  final List<GameConstruction> constructions;
  final int openedArtifactId;
  final List<GameLeader> leaders;

  double get territoryPercent => territoryPermille / 10;
  double leaderPercent(int score) => territoryTotalCells <= 0 ? 0 : score * 100 / territoryTotalCells;

  bool get turretActionAvailable => interaction.valid &&
      (interaction.action == odgInteractionReprogram ||
       interaction.action == odgInteractionUpgrade ||
       interaction.action == odgInteractionRefill ||
       inventory.selected.typeId == odgItemTurret);
  bool get hackActionAvailable => interaction.valid && interaction.action == odgInteractionReprogram;
  bool get dropActionAvailable => !inventory.selected.isEmpty;
  bool get carryingTurret => inventory.selected.typeId == odgItemTurret;
  bool get carryingAmmo => inventory.selected.typeId == odgItemAmmo;
  bool get carryingChip => inventory.selected.typeId == odgItemReprogramChip || inventory.selected.typeId == odgItemAscensionChip;
  int get carriedAmmo => carryingAmmo ? inventory.selected.quantity : 0;
  int get carriedTurretAmmo => carryingTurret ? inventory.selected.payloadId & 0xFFFFFFFF : 0;
  int get ammoReserve => inventory.slots.where((GameItemStack s) => s.typeId == odgItemAmmo).fold<int>(0, (int sum, GameItemStack s) => sum + s.quantity);

  GameConstruction? get targetedConstruction {
    if (interaction.targetKind != odgInteractionTargetConstruction || interaction.targetId < 0) {
      return null;
    }
    for (final GameConstruction value in constructions) {
      if (value.constructionId == interaction.targetId) return value;
    }
    return null;
  }

  String get contextualActionLabel =>
      interaction.action == odgInteractionNone ? 'INTERACTUAR' : interaction.label;

  String get statusLabel {
    if (!playerAlive) return 'REAGRUPANDO';
    if (trailActive) return 'TRAZO EXPUESTO';
    if (!inventory.selected.isEmpty) return inventory.selected.displayLabel.toUpperCase();
    return '$ownedTurrets TORRETAS';
  }
}

final class GameStorage {
  const GameStorage({
    required this.artifactId,
    required this.slotCount,
    required this.usedSlots,
    required this.slots,
  });
  final int artifactId;
  final int slotCount;
  final int usedSlots;
  final List<GameItemStack> slots;
}

final class GameRepairQuote {
  const GameRepairQuote({
    required this.itemType,
    required this.materialTier,
    required this.durabilityBefore,
    required this.durabilityAfter,
    required this.costItemType,
    required this.costQuantity,
    required this.stationItemType,
  });
  final int itemType;
  final int materialTier;
  final int durabilityBefore;
  final int durabilityAfter;
  final int costItemType;
  final int costQuantity;
  final int stationItemType;
}

final class GameMapSample {
  const GameMapSample({required this.ownerActorPlusOne, required this.flags, required this.heightMilli});
  final int ownerActorPlusOne;
  final int flags;
  final int heightMilli;
}

final class GameMapMarker {
  const GameMapMarker({
    required this.kind,
    required this.id,
    required this.ownerActorId,
    required this.materialTier,
    required this.xMilli,
    required this.zMilli,
    required this.state,
  });
  final int kind;
  final int id;
  final int ownerActorId;
  final int materialTier;
  final int xMilli;
  final int zMilli;
  final int state;
}

final class GameMapData {
  const GameMapData({
    required this.minXMilli,
    required this.minZMilli,
    required this.maxXMilli,
    required this.maxZMilli,
    required this.width,
    required this.height,
    required this.samples,
    required this.markers,
  });
  final int minXMilli;
  final int minZMilli;
  final int maxXMilli;
  final int maxZMilli;
  final int width;
  final int height;
  final List<GameMapSample> samples;
  final List<GameMapMarker> markers;
}
