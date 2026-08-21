import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

const int odgApiVersion = 37;
const int odgFfiAbiVersion = 9;
const int odgFfiEndianMarker = 0x01020304;
const int odgPixelFormatRgba8 = 1;
const int odgTickRate = 120;

const int odgStatusOk = 0;
const int odgStatusInvalidArgument = 1;
const int odgStatusInvalidState = 2;
const int odgStatusUnsupported = 3;
const int odgStatusBufferTooSmall = 4;
const int odgStatusVersionMismatch = 5;

const int odgFfiFeatureFramebufferPointer = 1 << 0;
const int odgFfiFeatureFramebufferCopy = 1 << 1;
const int odgFfiFeatureStatsPointer = 1 << 2;
const int odgFfiFeatureStatsCopy = 1 << 3;
const int odgFfiFeaturePortraitRender = 1 << 4;
const int odgFfiFeatureFixed120Hz = 1 << 5;
const int odgFfiFeatureCameraInput = 1 << 6;
const int odgFfiFeatureGenericItems = 1 << 7;
const int odgFfiFeatureInventoryQuery = 1 << 8;
const int odgFfiFeatureCommandQueue = 1 << 9;
const int odgFfiFeatureInteractionHint = 1 << 10;
const int odgFfiFeatureMusicAnalyzer = 1 << 11;
const int odgFfiFeatureResources = 1 << 12;
const int odgFfiFeatureCrafting = 1 << 13;
const int odgFfiFeatureArtifacts = 1 << 14;
const int odgFfiFeatureSaveLoad = 1 << 15;
const int odgFfiFeatureMapQuery = 1 << 16;
const int odgFfiFeatureTextureUpload = 1 << 17;
const int odgFfiFeatureAvatarSkins = 1 << 18;
const int odgFfiFeatureChunkWorldgen = 1 << 19;
const int odgFfiFeatureCameraProfile = 1 << 20;
const int odgFfiFeatureRemoteView = 1 << 21;
const int odgFfiFeatureChunkRuntime = 1 << 22;
const int odgFfiFeatureTurretModes = 1 << 23;
const int odgFfiFeatureArtifactPaging = 1 << 24;
const int odgFfiFeatureEcology = 1 << 25;
const int odgFfiFeatureNutrition = 1 << 26;
const int odgFfiFeatureFauna = 1 << 27;
const int odgFfiFeatureTerrainSurface = 1 << 28;
const int odgFfiFeatureWeather = 1 << 29;
const int odgFfiFeatureDeathRecovery = 1 << 30;
const int odgFfiFeatureTerritoryPolicy = 1 << 31;
const int odgFfiFeatureTrailBreak = 1 << 32;
const int odgFfiFeatureHydration = 1 << 33;
const int odgFfiFeatureFluidRegistry = 1 << 34;
const int odgFfiFeatureSaveSchemaQuery = 1 << 35;
const int odgFfiFeatureFaunaNestingRegistry = 1 << 36;
const int odgFfiFeatureGeology = 1 << 37;
const int odgFfiFeatureDayNight = 1 << 38;
const int odgFfiFeatureRespiration = 1 << 39;
const int odgFfiFeatureConstructionBlocks = 1 << 40;
const int odgFfiFeatureConstructionShapes = 1 << 41;
const int odgFfiFeatureConstructionDurability = 1 << 42;

const int odgVisualThemeNeonTides = 0;
const int odgVisualThemeEmeraldCrown = 1;
const int odgVisualThemeSolarEmber = 2;
const int odgVisualThemeObsidianPulse = 3;
const int odgVisualThemeCount = 4;

const int odgPresentationGameplay = 0;
const int odgPresentationShowcase = 1;

const int odgCameraModeFirstPerson = 0;
const int odgCameraModeClose = 1;
const int odgCameraModeMedium = 2;
const int odgCameraModeFar = 3;
const int odgCameraModeCount = 4;
const int odgMusicReactivityMaxQ16 = 98303;
const int odgRemoteViewWidth = 320;
const int odgRemoteViewHeight = 180;
const int odgAvatarPreviewSize = 256;

const int odgButtonFire = 1 << 0;
const int odgButtonDash = 1 << 1;
const int odgButtonRestart = 1 << 2;
const int odgButtonInteract = 1 << 3;
const int odgButtonAction = odgButtonInteract;
const int odgButtonDrop = 1 << 4;
const int odgButtonJump = 1 << 5;

const int odgInventoryBaseSlots = 4;
const int odgInventoryMaxSlots = 12;

const int odgMaterialNone = 0;
const int odgMaterialWood = 1;
const int odgMaterialStone = 2;
const int odgMaterialIron = 3;

const int odgItemNone = 0;
const int odgItemWood = 1;
const int odgItemStone = 2;
const int odgItemIron = 3;
const int odgItemAmmo = 4;
const int odgItemReprogramChip = 5;
const int odgItemAscensionChip = 6;
const int odgItemAxe = 7;
const int odgItemPickaxe = 8;
const int odgItemTurret = 9;
const int odgItemWorkbench = 10;
const int odgItemSmithy = 11;
const int odgItemChest = 12;
const int odgItemBackpack = 13;
const int odgItemApple = 14;
const int odgItemAppleSeed = 15;
const int odgItemBirdTrap = 16;
const int odgItemLeather = 17;
const int odgItemRawMeat = 18;
const int odgItemHuntingKnife = 19;
const int odgItemSword = 20;
const int odgItemWaterFlask = 21;
const int odgItemRainBarrel = 22;
const int odgItemCoal = 23;
const int odgItemTorch = 24;
const int odgItemNightShard = 25;
const int odgItemRawFish = 26;
const int odgItemBuildingBlock = 27;
const int odgItemRaft = 28;

const int odgItemCapHarvest = 1;
const int odgItemCapMine = 2;
const int odgItemCapReprogram = 4;
const int odgItemCapUpgrade = 8;
const int odgItemCapPlace = 16;
const int odgItemCapStore = 32;
const int odgItemCapExpandInventory = 64;
const int odgItemCapConsume = 128;
const int odgItemCapPlant = 256;
const int odgItemCapHunt = 512;
const int odgItemCapAttack = 1024;
const int odgItemCapCollectWater = 2048;
const int odgItemCapIrrigate = 4096;
const int odgItemCapDrink = 8192;
const int odgItemCapRefillTurret = 16384;
const int odgItemCapConstruct = 32768;

const int odgInteractionNone = 0;
const int odgInteractionPickup = 1;
const int odgInteractionPlace = 2;
const int odgInteractionReprogram = 3;
const int odgInteractionUpgrade = 4;
const int odgInteractionRefill = 5;
const int odgInteractionPickupArtifact = 6;
const int odgInteractionHarvest = 7;
const int odgInteractionOpenArtifact = 8;
const int odgInteractionPlaceArtifact = 9;
const int odgInteractionGatherFruit = 10;
const int odgInteractionHuntFauna = 11;
const int odgInteractionAttackActor = 12;
const int odgInteractionCollectWater = 13;
const int odgInteractionIrrigate = 14;
const int odgInteractionDrinkWater = 15;
const int odgInteractionUseVehicle = 16;
const int odgInteractionPlaceConstruction = 17;
const int odgInteractionDismantleConstruction = 18;
const int odgInteractionRepairConstruction = 19;
const int odgInteractionAttackConstruction = 20;


const int odgInteractionTargetNone = 0;
const int odgInteractionTargetPickup = 1;
const int odgInteractionTargetTurret = 2;
const int odgInteractionTargetResource = 3;
const int odgInteractionTargetArtifact = 4;
const int odgInteractionTargetFauna = 5;
const int odgInteractionTargetActor = 6;
const int odgInteractionTargetSurface = 7;
const int odgInteractionTargetConstruction = 8;

const int odgMessageNone = 0;
const int odgMessageWrongTier = 1;
const int odgMessageWrongUpgrade = 2;
const int odgMessageToolRequired = 3;
const int odgMessagePickaxeTierRequired = 4;
const int odgMessageInventoryFull = 5;
const int odgMessageOwnerOnly = 6;
const int odgMessageEmptyChestToMove = 7;
const int odgMessageInvalidPlacement = 8;
const int odgMessageStationRequired = 9;
const int odgMessageMissingResources = 10;
const int odgMessageTerritoryRequired = 11;
const int odgMessageTooDry = 12;
const int odgMessageTooSteep = 13;
const int odgMessageNotHungry = 14;
const int odgMessageNotThirsty = 15;

const int odgCommandSelectSlot = 1;
const int odgCommandDropSelected = 2;
const int odgCommandPlaceSelected = 3;
const int odgCommandUseSelected = 4;
const int odgCommandEquipBackpack = 5;
const int odgCommandRequestRespawn = 6;
const int odgCommandMoveSlot = 7;
const int odgCommandCraft = 8;
const int odgCommandRepairSelected = 9;
const int odgCommandCloseArtifact = 10;
const int odgCommandStorageDeposit = 11;
const int odgCommandStorageWithdraw = 12;
const int odgCommandSetTurretMode = 13;
const int odgCommandConsumeSelected = 14;
const int odgCommandPlantSelected = 15;
const int odgCommandDrinkSelected = 16;
const int odgCommandSetConstructionShape = 17;

const int odgTurretModeDefense = 0;
const int odgTurretModeHarvest = 1;
const int odgTurretArtifactStateMaxAmmoMask = 0xffff;
const int odgTurretArtifactStateModeShift = 16;

const int odgRecipeCount = 30;
const int odgChestSlots = 24;
const int odgArtifactMaxEntries = 64;
const int odgConstructionMaxEntries = 64;
const int odgConstructionShapeWall = 1;
const int odgConstructionShapeFloor = 2;
const int odgConstructionShapeDoorway = 3;
const int odgConstructionShapeRoof = 4;
const int odgArtifactCapOpenUi = 1 << 0;
const int odgArtifactCapMove = 1 << 1;
const int odgArtifactCapStore = 1 << 2;
const int odgArtifactCapRemoteView = 1 << 3;
const int odgArtifactCapFire = 1 << 4;
const int odgArtifactCapHarvest = 1 << 5;
const int odgArtifactCapUpgrade = 1 << 6;
const int odgArtifactCapTame = 1 << 7;
const int odgArtifactCapCollectRain = 1 << 8;
const int odgArtifactCapLight = 1 << 9;
const int odgArtifactCapConstruction = 1 << 10;
const int odgArtifactCapVehicle = 1 << 11;
const int odgMapMarkerActor = 1;
const int odgMapMarkerTurret = 2;
const int odgMapMarkerArtifact = 3;
const int odgMapMarkerConstruction = 4;
const int odgMapFlagConstruction = 1 << 7;
const int odgMapMaxResolution = 128;
const int odgMapMaxMarkers = 96;
const int odgAvatarTextureSize = 256;
const int odgAvatarFaceFront = 0;
const int odgAvatarFaceRight = 1;
const int odgAvatarFaceBack = 2;
const int odgAvatarFaceLeft = 3;
const int odgAvatarFaceTop = 4;
const int odgAvatarFaceBottom = 5;
const int odgResourceMaxEntries = 128;
const int odgFaunaMaxEntries = 48;
const int odgFaunaMaxNests = 12;
const int odgLootMaxEntries = 4;
const int odgFloraMaxStages = 5;
const int odgFaunaFamilyBird = 1;
const int odgFaunaFamilyMammal = 2;
const int odgFaunaFamilyAquatic = 3;
const int odgFaunaFamilyMonster = 4;
const int odgFaunaSpeciesOrchardBird = 1;
const int odgFaunaSpeciesForestDeer = 2;
const int odgFaunaSpeciesMeadowRabbit = 3;
const int odgFaunaSpeciesFieldFowl = 4;
const int odgFaunaSpeciesRiverFish = 5;
const int odgFaunaSpeciesNightStalker = 6;
const int odgFaunaSpeciesMarshCrocodile = 7;
const int odgNestSubstrateTree = 1 << 0;
const int odgNestSubstrateGround = 1 << 1;
const int odgNestSubstrateCliff = 1 << 2;
const int odgNestSubstrateStructure = 1 << 3;
const int odgFaunaStageYoung = 1;
const int odgFaunaStageJuvenile = 2;
const int odgFaunaStageAdult = 3;
const int odgFaunaStageOld = 4;
const int odgFaunaSexNone = 0;
const int odgFaunaSexFemale = 1;
const int odgFaunaSexMale = 2;
const int odgSurfaceFlagWater = 1 << 0;
const int odgSurfaceFlagSteep = 1 << 1;
const int odgSurfaceFlagWet = 1 << 2;
const int odgSurfaceFlagMountain = 1 << 3;


final class OdgFfiAbiInfo extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int ffiAbiVersion;

  @Uint32()
  external int engineApiVersion;

  @Uint32()
  external int endianMarker;

  @Uint32()
  external int gameStatsSize;

  @Uint32()
  external int leaderEntrySize;

  @Uint32()
  external int tickRate;

  @Uint32()
  external int maxRenderWidth;

  @Uint32()
  external int maxRenderHeight;

  @Uint32()
  external int maxRenderPixels;

  @Uint32()
  external int framebufferPixelFormat;

  @Uint32()
  external int framebufferBytesPerPixel;

  @Uint64()
  external int featureBits;

  @Uint32()
  external int inventorySnapshotSize;

  @Uint32()
  external int interactionHintSize;

  @Uint32()
  external int resourceSnapshotSize;
  @Uint32()
  external int artifactSnapshotSize;
  @Uint32()
  external int foodDefinitionSize;
  @Uint32()
  external int floraSpeciesDefinitionSize;
  @Uint32()
  external int faunaSpeciesDefinitionSize;
  @Uint32()
  external int faunaSnapshotSize;
  @Uint32()
  external int faunaNestSnapshotSize;
  @Uint32()
  external int surfaceSampleSize;
  @Uint32()
  external int fluidDefinitionSize;
  @Uint32()
  external int fluidContainerDefinitionSize;
  @Uint32()
  external int faunaNestingDefinitionSize;
  @Uint32()
  external int constructionSnapshotSize;
}

final class OdgItemStack extends Struct {
  @Uint32()
  external int typeId;
  @Uint32()
  external int quantity;
  @Uint32()
  external int materialTier;
  @Uint32()
  external int durability;
  @Uint32()
  external int maxDurability;
  @Uint32()
  external int flags;
  @Uint64()
  external int instanceId;
  @Uint64()
  external int payloadId;
}

final class OdgItemDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int typeId;
  @Uint32() external int category;
  @Uint32() external int displayCode;
  @Uint32() external int maxStack;
  @Uint32() external int defaultMaterialTier;
  @Uint32() external int flags;
  @Uint32() external int baseDurability;
  @Uint32() external int capabilityBits;
  @Array(3) external Array<Uint32> reservedU32;
}

final class OdgInventorySnapshot extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int schemaVersion;
  @Uint32()
  external int actorId;
  @Uint32()
  external int slotCount;
  @Uint32()
  external int baseSlotCount;
  @Uint32()
  external int selectedSlot;
  @Uint32()
  external int equippedBackpackType;
  @Uint32()
  external int reservedU32;
  @Array(odgInventoryMaxSlots)
  external Array<OdgItemStack> slots;
}

final class OdgInteractionHint extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int action;
  @Uint32()
  external int targetKind;
  @Uint32()
  external int targetId;
  @Uint32()
  external int valid;
  @Uint32()
  external int requiresHold;
  @Uint32()
  external int progressTicks;
  @Uint32()
  external int thresholdTicks;
  @Uint32()
  external int messageCode;
  @Array(3)
  external Array<Uint32> reservedU32;
}

final class OdgCommand extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int type;
  @Uint32()
  external int arg0;
  @Uint32()
  external int arg1;
  @Uint32()
  external int arg2;
  @Uint32()
  external int arg3;
  @Uint64()
  external int payload;
}


final class OdgFoodDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int itemType;
  @Uint32() external int satietyRestore;
  @Uint32() external int hydrationRestore;
  @Uint32() external int healAmount;
  @Uint32() external int flags;
  @Uint32() external int groundLifetimeTicks;
  @Uint32() external int reservedU32;
}

final class OdgFluidDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int fluidId;
  @Uint32() external int displayCode;
  @Uint32() external int hydrationRestorePerUnit;
  @Uint32() external int flags;
  @Array(3) external Array<Uint32> reservedU32;
}

final class OdgFluidContainerDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int itemType;
  @Uint32() external int capacityUnits;
  @Uint32() external int acceptedFluidFlags;
  @Uint32() external int flags;
  @Array(3) external Array<Uint32> reservedU32;
}

final class OdgResourceEntry extends Struct {
  @Uint64() external int stableId;
  @Uint32() external int resourceId;
  @Uint32() external int kind;
  @Uint32() external int state;
  @Int32() external int xMilli;
  @Int32() external int zMilli;
  @Uint32() external int progressTicks;
  @Uint32() external int requiredTicks;
  @Uint32() external int yieldPreviewMin;
  @Uint32() external int yieldPreviewMax;
  @Uint32() external int speciesId;
  @Uint32() external int floraStage;
  @Uint32() external int fruitCount;
  @Uint32() external int soilMoisturePermille;
}

final class OdgResourceSnapshot extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int count;
  @Array(odgResourceMaxEntries) external Array<OdgResourceEntry> entries;
}

final class OdgFloraSpeciesDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int speciesId;
  @Uint32() external int displayCode;
  @Uint32() external int fruitItemType;
  @Uint32() external int seedItemType;
  @Uint32() external int growthForm;
  @Uint32() external int harvestItemType;
  @Uint32() external int harvestToolItemType;
  @Uint32() external int harvestFlags;
  @Uint32() external int harvestBaseTicks;
  @Uint32() external int seedlingTicks;
  @Uint32() external int saplingTicks;
  @Uint32() external int youngTicks;
  @Uint32() external int oldTicks;
  @Uint32() external int fruitCycleTicks;
  @Uint32() external int fruitCapacityMin;
  @Uint32() external int fruitCapacityMax;
  @Uint32() external int maxDynamicPerChunk;
  @Uint32() external int minSpacingMilli;
  @Uint32() external int plantingClearanceMilli;
  @Uint32() external int minGrowthMoisturePermille;
  @Uint32() external int preferredGrowthMoisturePermille;
  @Uint32() external int seedGerminationPermille;
  @Uint32() external int fruitSeedRecoveryPermille;
  @Uint32() external int fallenFruitSeedPermille;
  @Uint32() external int naturalDropMaxPerCycle;
  @Uint32() external int collisionRadiusMilli;
  @Uint32() external int variantCount;
  @Array(odgFloraMaxStages) external Array<Uint32> stageYieldPermille;
  @Array(odgFloraMaxStages) external Array<Uint32> stageHarvestTimePermille;
  @Array(odgFloraMaxStages) external Array<Uint32> stageCollisionPermille;
  @Array(2) external Array<Uint32> reservedU32;
}

final class OdgFaunaSpeciesDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int speciesId;
  @Uint32() external int family;
  @Uint32() external int displayCode;
  @Uint32() external int variantCount;
  @Uint32() external int maxHealth;
  @Uint32() external int satietyDecayTicks;
  @Uint32() external int forageBelowPermille;
  @Uint32() external int wildPopulationTarget;
  @Uint32() external int wildPopulationHardCap;
  @Uint32() external int youngTicks;
  @Uint32() external int juvenileTicks;
  @Uint32() external int oldTicks;
  @Uint32() external int lifespanTicks;
  @Uint32() external int breedingCooldownTicks;
  @Uint32() external int breedingMinSatietyPermille;
  @Uint32() external int breedingMinHydrationPermille;
  @Uint32() external int reproductionMode;
  @Uint32() external int offspringMin;
  @Uint32() external int offspringMax;
  @Uint32() external int gestationOrIncubationTicks;
  @Uint32() external int lootTableId;
  @Uint32() external int behaviorFlags;
  @Uint32() external int groundSpeedMilliPerS;
  @Uint32() external int fleeSpeedMilliPerS;
  @Uint32() external int flightSpeedMilliPerS;
  @Uint32() external int bodyRadiusMilli;
  @Uint32() external int hydrationDecayTicks;
  @Uint32() external int drinkBelowPermille;
}

final class OdgFaunaDietDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int faunaSpeciesId;
  @Uint32() external int itemType;
  @Uint32() external int satietyRestore;
  @Uint32() external int propagationSurvivalPermille;
  @Uint32() external int hydrationRestore;
  @Uint32() external int flags;
  @Uint32() external int reservedU32;
}

final class OdgFaunaHabitatDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int faunaSpeciesId;
  @Uint32() external int biomeMask;
  @Int32() external int minAltitudeMilli;
  @Int32() external int maxAltitudeMilli;
  @Uint32() external int minMoisturePermille;
  @Uint32() external int maxMoisturePermille;
  @Uint32() external int ambientForageRestore;
  @Uint32() external int ambientForageIntervalTicks;
  @Uint32() external int migrationIntervalTicks;
  @Uint32() external int spawnWeight;
  @Uint32() external int ambientWaterRestore;
  @Uint32() external int ambientWaterIntervalTicks;
}

final class OdgFaunaNestingDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int faunaSpeciesId;
  @Uint32() external int substrateMask;
  @Uint32() external int searchRangeMilli;
  @Uint32() external int minHostFloraStage;
  @Uint32() external int maxGroundSlopePermille;
  @Uint32() external int nestSpacingMilli;
  @Int32() external int heightOffsetMilli;
  @Array(2) external Array<Uint32> reservedU32;
}

final class OdgLootEntry extends Struct {
  @Uint32() external int itemType;
  @Uint32() external int quantityMin;
  @Uint32() external int quantityMax;
  @Uint32() external int chancePermille;
}

final class OdgLootTableDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int lootTableId;
  @Uint32() external int entryCount;
  @Uint32() external int reservedU32;
  @Array(odgLootMaxEntries) external Array<OdgLootEntry> entries;
}

final class OdgFaunaEntry extends Struct {
  @Uint64() external int stableId;
  @Uint32() external int faunaId;
  @Uint32() external int speciesId;
  @Uint32() external int family;
  @Uint32() external int variant;
  @Uint32() external int state;
  @Uint32() external int tame;
  @Uint32() external int ownerActorId;
  @Uint32() external int health;
  @Uint32() external int maxHealth;
  @Uint32() external int satietyPermille;
  @Uint32() external int hydrationPermille;
  @Uint32() external int lifeStage;
  @Uint32() external int sex;
  @Uint32() external int nestId;
  @Uint64() external int ageTicks;
  @Int32() external int xMilli;
  @Int32() external int yMilli;
  @Int32() external int zMilli;
}

final class OdgFaunaSnapshot extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int count;
  @Uint32() external int birdCount;
  @Uint32() external int mammalCount;
  @Array(odgFaunaMaxEntries) external Array<OdgFaunaEntry> entries;
}

final class OdgFaunaNestEntry extends Struct {
  @Uint64() external int stableId;
  @Uint32() external int nestId;
  @Uint32() external int speciesId;
  @Uint32() external int state;
  @Uint32() external int substrate;
  @Uint32() external int eggCount;
  @Uint32() external int hatchTicks;
  @Uint32() external int parentA;
  @Uint32() external int parentB;
  @Uint64() external int hostResourceStableId;
  @Int32() external int xMilli;
  @Int32() external int yMilli;
  @Int32() external int zMilli;
}

final class OdgFaunaNestSnapshot extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int count;
  @Array(odgFaunaMaxNests) external Array<OdgFaunaNestEntry> entries;
}

final class OdgSurfaceSample extends Struct {
  @Uint32() external int structSize;
  @Int32() external int heightMilli;
  @Int32() external int normalXQ15;
  @Int32() external int normalYQ15;
  @Int32() external int normalZQ15;
  @Uint32() external int biome;
  @Uint32() external int moisturePermille;
  @Uint32() external int waterDepthMilli;
  @Uint32() external int rainPermille;
  @Uint32() external int flags;
  @Array(2) external Array<Uint32> reservedU32;
}

final class OdgRecipeIngredient extends Struct {
  @Uint32() external int itemType;
  @Uint32() external int materialTier;
  @Uint32() external int quantity;
  @Uint32() external int reservedU32;
}

final class OdgRecipeDefinition extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int recipeId;
  @Uint32() external int stationItemType;
  @Uint32() external int displayCode;
  @Uint32() external int outputItemType;
  @Uint32() external int outputQuantity;
  @Uint32() external int outputMaterialTier;
  @Uint32() external int ingredientCount;
  @Array(4) external Array<OdgRecipeIngredient> ingredients;
  @Array(4) external Array<Uint32> reservedU32;
}

final class OdgArtifactEntry extends Struct {
  @Uint64() external int instanceId;
  @Uint32() external int artifactId;
  @Uint32() external int itemType;
  @Uint32() external int ownerActorId;
  @Uint32() external int materialTier;
  @Uint32() external int capabilityBits;
  @Int32() external int xMilli;
  @Int32() external int zMilli;
  @Uint32() external int storageUsed;
  @Uint32() external int state;
}

final class OdgArtifactSnapshot extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int count;
  @Uint32() external int openedArtifactId;
  @Uint32() external int totalCount;
  @Array(odgArtifactMaxEntries) external Array<OdgArtifactEntry> entries;
}

final class OdgConstructionEntry extends Struct {
  @Uint64() external int instanceId;
  @Uint32() external int constructionId;
  @Uint32() external int ownerActorId;
  @Uint32() external int controllerActorId;
  @Uint32() external int materialTier;
  @Uint32() external int shape;
  @Int32() external int xMilli;
  @Int32() external int zMilli;
  @Uint32() external int state;
  @Uint32() external int health;
  @Uint32() external int maxHealth;
}

final class OdgConstructionSnapshot extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int count;
  @Uint32() external int totalCount;
  @Uint32() external int selectedShape;
  @Array(odgConstructionMaxEntries) external Array<OdgConstructionEntry> entries;
}

final class OdgStorageSnapshot extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int artifactId;
  @Uint32() external int slotCount;
  @Uint32() external int usedSlots;
  @Array(odgChestSlots) external Array<OdgItemStack> slots;
}

final class OdgRepairQuote extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int itemType;
  @Uint32() external int materialTier;
  @Uint32() external int durabilityBefore;
  @Uint32() external int durabilityAfter;
  @Uint32() external int costItemType;
  @Uint32() external int costQuantity;
  @Uint32() external int stationItemType;
}

final class OdgMapQueryDesc extends Struct {
  @Uint32() external int structSize;
  @Int32() external int minXMilli;
  @Int32() external int minZMilli;
  @Int32() external int maxXMilli;
  @Int32() external int maxZMilli;
  @Uint32() external int width;
  @Uint32() external int height;
  @Uint32() external int reservedU32;
}

final class OdgMapSample extends Struct {
  @Uint32() external int ownerActorPlusOne;
  @Uint32() external int flags;
  @Int32() external int heightMilli;
  @Uint32() external int reservedU32;
}

final class OdgMapMarker extends Struct {
  @Uint32() external int kind;
  @Uint32() external int id;
  @Uint32() external int ownerActorId;
  @Uint32() external int materialTier;
  @Int32() external int xMilli;
  @Int32() external int zMilli;
  @Uint32() external int state;
  @Uint32() external int reservedU32;
}

final class OdgGameStats extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int apiVersion;
  @Uint64()
  external int tick;
  @Uint64()
  external int matchSeed;
  @Uint32()
  external int width;
  @Uint32()
  external int height;
  @Uint32()
  external int aliveCount;
  @Uint32()
  external int playerAlive;
  @Uint32()
  external int playerHealth;
  @Uint32()
  external int playerMaxHealth;
  @Uint32()
  external int playerLevel;
  @Uint32()
  external int playerScore;
  @Uint32()
  external int playerKills;
  @Uint32()
  external int playerDeaths;
  @Uint32()
  external int zoneRadiusMilli;
  @Uint32()
  external int simulationHz;
  @Uint32()
  external int renderTriangles;
  @Uint32()
  external int renderPixelsTouched;
  @Uint64()
  external int deterministicStateHash;
  @Uint32()
  external int territoryCells;
  @Uint32()
  external int territoryTotalCells;
  @Uint32()
  external int territoryPermille;
  @Uint32()
  external int playerTrailCells;
  @Uint32()
  external int playerTrailActive;
  @Uint32()
  external int matchOver;
  @Uint32()
  external int winnerId;
  @Uint32()
  external int playerDeathReason;
  @Uint32()
  external int turretTotal;
  @Uint32()
  external int playerOwnedTurrets;
  @Uint32()
  external int playerCarryingTurret;
  @Uint32()
  external int carriedTurretAmmo;
  @Uint32()
  external int turretActionAvailable;
  @Uint32()
  external int ammoCratesTotal;
  @Uint32()
  external int playerCarryingAmmoCrate;
  @Uint32()
  external int playerCarriedAmmo;
  @Uint32()
  external int playerAmmoReserve;
  @Uint32()
  external int chipsTotal;
  @Uint32()
  external int playerCarryingChip;
  @Uint32()
  external int playerChipKind;
  @Uint32()
  external int hackActionAvailable;
  @Uint32()
  external int dropActionAvailable;
  @Uint32()
  external int nearbyOwnedTurretVisible;
  @Uint32()
  external int nearbyOwnedTurretAmmo;
  @Uint32()
  external int nearbyOwnedTurretMaxAmmo;
  @Uint32()
  external int playerSatietyPermille;
  @Uint32()
  external int playerHydrationPermille;
  @Uint32()
  external int playerTrailBroken;
  @Uint32()
  external int weatherRainPermille;
  @Uint32()
  external int faunaCount;
}

final class OdgLeaderEntry extends Struct {
  @Uint32()
  external int actorId;
  @Uint32()
  external int score;
  @Uint32()
  external int level;
  @Uint32()
  external int alive;
  @Uint32()
  external int isPlayer;
  @Uint32()
  external int nameCode;
}

typedef _ApiVersionNative = Uint32 Function();
typedef _ApiVersionDart = int Function();
typedef _AbiQueryNative = Int32 Function(
  Uint32,
  Pointer<OdgFfiAbiInfo>,
  Uint64,
  Pointer<Uint64>,
);
typedef _AbiQueryDart = int Function(
  int,
  Pointer<OdgFfiAbiInfo>,
  int,
  Pointer<Uint64>,
);
typedef _InitNative = Int32 Function(Uint64, Uint32, Uint32);
typedef _InitDart = int Function(int, int, int);
typedef _ResizeNative = Int32 Function(Uint32, Uint32);
typedef _ResizeDart = int Function(int, int);
typedef _ResetNative = Int32 Function(Uint64);
typedef _ResetDart = int Function(int);
typedef _SetInputNative = Void Function(
  Int32,
  Int32,
  Int32,
  Int32,
  Uint32,
);
typedef _SetInputDart = void Function(int, int, int, int, int);
typedef _TickNative = Void Function(Uint32);
typedef _TickDart = void Function(int);
typedef _RenderNative = UintPtr Function();
typedef _RenderDart = int Function();
typedef _CopyFrameNative = Int32 Function(
  Pointer<Uint8>,
  Uint64,
  Pointer<Uint64>,
);
typedef _CopyFrameDart = int Function(Pointer<Uint8>, int, Pointer<Uint64>);
typedef _CopyStatsNative = Int32 Function(
  Pointer<OdgGameStats>,
  Uint64,
  Pointer<Uint64>,
);
typedef _CopyStatsDart = int Function(
  Pointer<OdgGameStats>,
  int,
  Pointer<Uint64>,
);
typedef _CopyInventoryNative = Int32 Function(
  Uint32,
  Pointer<OdgInventorySnapshot>,
  Uint64,
  Pointer<Uint64>,
);
typedef _CopyInventoryDart = int Function(
  int,
  Pointer<OdgInventorySnapshot>,
  int,
  Pointer<Uint64>,
);
typedef _CopyInteractionHintNative = Int32 Function(
  Pointer<OdgInteractionHint>,
  Uint64,
  Pointer<Uint64>,
);
typedef _CopyInteractionHintDart = int Function(
  Pointer<OdgInteractionHint>,
  int,
  Pointer<Uint64>,
);
typedef _CommandSubmitNative = Int32 Function(Pointer<OdgCommand>, Uint64);
typedef _CommandSubmitDart = int Function(Pointer<OdgCommand>, int);
typedef _ItemDefinitionGetNative = Int32 Function(
  Uint32, Pointer<OdgItemDefinition>, Uint64, Pointer<Uint64>,
);
typedef _ItemDefinitionGetDart = int Function(
  int, Pointer<OdgItemDefinition>, int, Pointer<Uint64>,
);
typedef _CopyResourcesNative = Int32 Function(Pointer<OdgResourceSnapshot>, Uint64, Pointer<Uint64>);
typedef _CopyResourcesDart = int Function(Pointer<OdgResourceSnapshot>, int, Pointer<Uint64>);
typedef _FoodGetNative = Int32 Function(Uint32, Pointer<OdgFoodDefinition>, Uint64, Pointer<Uint64>);
typedef _FoodGetDart = int Function(int, Pointer<OdgFoodDefinition>, int, Pointer<Uint64>);
typedef _FluidGetNative = Int32 Function(Uint32, Pointer<OdgFluidDefinition>, Uint64, Pointer<Uint64>);
typedef _FluidGetDart = int Function(int, Pointer<OdgFluidDefinition>, int, Pointer<Uint64>);
typedef _FluidContainerGetNative = Int32 Function(Uint32, Pointer<OdgFluidContainerDefinition>, Uint64, Pointer<Uint64>);
typedef _FluidContainerGetDart = int Function(int, Pointer<OdgFluidContainerDefinition>, int, Pointer<Uint64>);
typedef _FloraGetNative = Int32 Function(Uint32, Pointer<OdgFloraSpeciesDefinition>, Uint64, Pointer<Uint64>);
typedef _FloraGetDart = int Function(int, Pointer<OdgFloraSpeciesDefinition>, int, Pointer<Uint64>);
typedef _FaunaSpeciesGetNative = Int32 Function(Uint32, Pointer<OdgFaunaSpeciesDefinition>, Uint64, Pointer<Uint64>);
typedef _FaunaSpeciesGetDart = int Function(int, Pointer<OdgFaunaSpeciesDefinition>, int, Pointer<Uint64>);
typedef _FaunaDietGetNative = Int32 Function(Uint32, Pointer<OdgFaunaDietDefinition>, Uint64, Pointer<Uint64>);
typedef _FaunaDietGetDart = int Function(int, Pointer<OdgFaunaDietDefinition>, int, Pointer<Uint64>);
typedef _FaunaHabitatGetNative = Int32 Function(Uint32, Pointer<OdgFaunaHabitatDefinition>, Uint64, Pointer<Uint64>);
typedef _FaunaHabitatGetDart = int Function(int, Pointer<OdgFaunaHabitatDefinition>, int, Pointer<Uint64>);
typedef _FaunaNestingGetNative = Int32 Function(Uint32, Pointer<OdgFaunaNestingDefinition>, Uint64, Pointer<Uint64>);
typedef _FaunaNestingGetDart = int Function(int, Pointer<OdgFaunaNestingDefinition>, int, Pointer<Uint64>);
typedef _LootGetNative = Int32 Function(Uint32, Pointer<OdgLootTableDefinition>, Uint64, Pointer<Uint64>);
typedef _LootGetDart = int Function(int, Pointer<OdgLootTableDefinition>, int, Pointer<Uint64>);
typedef _CopyFaunaNative = Int32 Function(Pointer<OdgFaunaSnapshot>, Uint64, Pointer<Uint64>);
typedef _CopyFaunaDart = int Function(Pointer<OdgFaunaSnapshot>, int, Pointer<Uint64>);
typedef _CopyFaunaNestsNative = Int32 Function(Pointer<OdgFaunaNestSnapshot>, Uint64, Pointer<Uint64>);
typedef _CopyFaunaNestsDart = int Function(Pointer<OdgFaunaNestSnapshot>, int, Pointer<Uint64>);
typedef _WorldSurfaceNative = Int32 Function(Int64, Int64, Pointer<OdgSurfaceSample>, Uint64, Pointer<Uint64>);
typedef _WorldSurfaceDart = int Function(int, int, Pointer<OdgSurfaceSample>, int, Pointer<Uint64>);
typedef _GeologySampleNative = Uint32 Function(Int64, Int64, Uint32);
typedef _GeologySampleDart = int Function(int, int, int);
typedef _WorldCellPairNative = Uint32 Function(Int64, Int64);
typedef _WorldCellPairDart = int Function(int, int);
typedef _MusicResetNative = Void Function();
typedef _MusicResetDart = void Function();
typedef _MusicSubmitNative = Int32 Function(
  Pointer<Float>,
  Uint32,
  Uint32,
  Uint32,
  Uint64,
);
typedef _MusicSubmitDart = int Function(Pointer<Float>, int, int, int, int);

typedef _RecipeGetNative = Int32 Function(Uint32, Pointer<OdgRecipeDefinition>, Uint64, Pointer<Uint64>);
typedef _RecipeGetDart = int Function(int, Pointer<OdgRecipeDefinition>, int, Pointer<Uint64>);
typedef _RecipeMaxNative = Uint32 Function(Uint32, Uint32);
typedef _RecipeMaxDart = int Function(int, int);
typedef _CopyArtifactsNative = Int32 Function(Pointer<OdgArtifactSnapshot>, Uint64, Pointer<Uint64>);
typedef _CopyArtifactsDart = int Function(Pointer<OdgArtifactSnapshot>, int, Pointer<Uint64>);
typedef _CopyArtifactsPageNative = Int32 Function(Uint32, Pointer<OdgArtifactSnapshot>, Uint64, Pointer<Uint64>);
typedef _CopyArtifactsPageDart = int Function(int, Pointer<OdgArtifactSnapshot>, int, Pointer<Uint64>);
typedef _CopyConstructionPageNative = Int32 Function(Uint32, Pointer<OdgConstructionSnapshot>, Uint64, Pointer<Uint64>);
typedef _CopyConstructionPageDart = int Function(int, Pointer<OdgConstructionSnapshot>, int, Pointer<Uint64>);
typedef _CopyStorageNative = Int32 Function(Uint32, Uint32, Pointer<OdgStorageSnapshot>, Uint64, Pointer<Uint64>);
typedef _CopyStorageDart = int Function(int, int, Pointer<OdgStorageSnapshot>, int, Pointer<Uint64>);
typedef _RepairQuoteNative = Int32 Function(Uint32, Pointer<OdgRepairQuote>, Uint64, Pointer<Uint64>);
typedef _RepairQuoteDart = int Function(int, Pointer<OdgRepairQuote>, int, Pointer<Uint64>);
typedef _SaveWriteNative = Int32 Function(Pointer<Uint8>, Uint64, Pointer<Uint64>);
typedef _SaveWriteDart = int Function(Pointer<Uint8>, int, Pointer<Uint64>);
typedef _SaveLoadNative = Int32 Function(Pointer<Uint8>, Uint64);
typedef _SaveLoadDart = int Function(Pointer<Uint8>, int);
typedef _MapQueryNative = Int32 Function(Pointer<OdgMapQueryDesc>, Pointer<OdgMapSample>, Uint64, Pointer<Uint64>, Pointer<OdgMapMarker>, Uint32, Pointer<Uint32>);
typedef _MapQueryDart = int Function(Pointer<OdgMapQueryDesc>, Pointer<OdgMapSample>, int, Pointer<Uint64>, Pointer<OdgMapMarker>, int, Pointer<Uint32>);
typedef _RemoteViewNative = Int32 Function(Uint32, Pointer<Uint8>, Uint64, Pointer<Uint64>);
typedef _RemoteViewDart = int Function(int, Pointer<Uint8>, int, Pointer<Uint64>);
typedef _CameraPreviewNative = Int32 Function(Uint32, Uint32, Int32, Pointer<Uint8>, Uint64, Pointer<Uint64>);
typedef _CameraPreviewDart = int Function(int, int, int, Pointer<Uint8>, int, Pointer<Uint64>);
typedef _TextureUploadNative = Int32 Function(Uint32, Pointer<Uint8>, Uint32, Uint32, Uint32);
typedef _TextureUploadDart = int Function(int, Pointer<Uint8>, int, int, int);
typedef _TextureFaceNative = Int32 Function(Uint32);
typedef _TextureFaceDart = int Function(int);
typedef _SetU32Native = Void Function(Uint32);
typedef _SetU32Dart = void Function(int);
typedef _U32Native = Uint32 Function();
typedef _U32Dart = int Function();
typedef _U32ArgNative = Uint32 Function(Uint32);
typedef _U32ArgDart = int Function(int);
typedef _U64Native = Uint64 Function();
typedef _U64Dart = int Function();
typedef _RankNative = Uint32 Function(Uint32);
typedef _RankDart = int Function(int);

final class OdgAbiDescriptor {
  const OdgAbiDescriptor({
    required this.maxRenderWidth,
    required this.maxRenderHeight,
    required this.maxRenderPixels,
    required this.featureBits,
  });

  final int maxRenderWidth;
  final int maxRenderHeight;
  final int maxRenderPixels;
  final int featureBits;
}

final class OdgNativeException implements Exception {
  const OdgNativeException(this.message, [this.status]);

  final String message;
  final int? status;

  @override
  String toString() => status == null
      ? 'OdgNativeException: $message'
      : 'OdgNativeException: $message (status $status)';
}

final class OdgNativeApi {
  OdgNativeApi._(this.library)
      : apiVersion =
            library.lookupFunction<_ApiVersionNative, _ApiVersionDart>(
          'odg_api_version',
        ),
        ffiAbiQuery = library.lookupFunction<_AbiQueryNative, _AbiQueryDart>(
          'odg_ffi_abi_query',
        ),
        init = library.lookupFunction<_InitNative, _InitDart>('odg_init'),
        resize =
            library.lookupFunction<_ResizeNative, _ResizeDart>('odg_resize'),
        reset = library.lookupFunction<_ResetNative, _ResetDart>('odg_reset'),
        setInput = library.lookupFunction<_SetInputNative, _SetInputDart>(
          'odg_set_input',
        ),
        tickUs =
            library.lookupFunction<_TickNative, _TickDart>('odg_tick_us'),
        renderFrame =
            library.lookupFunction<_RenderNative, _RenderDart>(
          'odg_render_frame',
        ),
        copyFramebuffer =
            library.lookupFunction<_CopyFrameNative, _CopyFrameDart>(
          'odg_copy_framebuffer',
        ),
        copyStats =
            library.lookupFunction<_CopyStatsNative, _CopyStatsDart>(
          'odg_copy_stats',
        ),
        copyInventory = library.lookupFunction<_CopyInventoryNative, _CopyInventoryDart>(
          'odg_copy_inventory',
        ),
        copyInteractionHint = library.lookupFunction<_CopyInteractionHintNative, _CopyInteractionHintDart>(
          'odg_copy_interaction_hint',
        ),
        commandSubmit = library.lookupFunction<_CommandSubmitNative, _CommandSubmitDart>(
          'odg_command_submit',
        ),
        itemDefinitionGet = library.lookupFunction<_ItemDefinitionGetNative, _ItemDefinitionGetDart>(
          'odg_item_definition_get',
        ),
        recipeCount = library.lookupFunction<_U32Native, _U32Dart>('odg_recipe_count'),
        recipeGet = library.lookupFunction<_RecipeGetNative, _RecipeGetDart>('odg_recipe_get'),
        recipeMaxCraftable = library.lookupFunction<_RecipeMaxNative, _RecipeMaxDart>('odg_recipe_max_craftable'),
        copyResources = library.lookupFunction<_CopyResourcesNative, _CopyResourcesDart>('odg_copy_resources'),
        foodDefinitionCount = library.lookupFunction<_U32Native, _U32Dart>('odg_food_definition_count'),
        foodDefinitionGet = library.lookupFunction<_FoodGetNative, _FoodGetDart>('odg_food_definition_get'),
        fluidDefinitionCount = library.lookupFunction<_U32Native, _U32Dart>('odg_fluid_definition_count'),
        fluidDefinitionGet = library.lookupFunction<_FluidGetNative, _FluidGetDart>('odg_fluid_definition_get'),
        fluidContainerDefinitionCount = library.lookupFunction<_U32Native, _U32Dart>('odg_fluid_container_definition_count'),
        fluidContainerDefinitionGet = library.lookupFunction<_FluidContainerGetNative, _FluidContainerGetDart>('odg_fluid_container_definition_get'),
        floraSpeciesCount = library.lookupFunction<_U32Native, _U32Dart>('odg_flora_species_count'),
        floraSpeciesGet = library.lookupFunction<_FloraGetNative, _FloraGetDart>('odg_flora_species_get'),
        faunaSpeciesCount = library.lookupFunction<_U32Native, _U32Dart>('odg_fauna_species_count'),
        faunaSpeciesGet = library.lookupFunction<_FaunaSpeciesGetNative, _FaunaSpeciesGetDart>('odg_fauna_species_get'),
        faunaDietCount = library.lookupFunction<_U32Native, _U32Dart>('odg_fauna_diet_count'),
        faunaDietGet = library.lookupFunction<_FaunaDietGetNative, _FaunaDietGetDart>('odg_fauna_diet_get'),
        faunaHabitatCount = library.lookupFunction<_U32Native, _U32Dart>('odg_fauna_habitat_count'),
        faunaHabitatGet = library.lookupFunction<_FaunaHabitatGetNative, _FaunaHabitatGetDart>('odg_fauna_habitat_get'),
        faunaNestingCount = library.lookupFunction<_U32Native, _U32Dart>('odg_fauna_nesting_count'),
        faunaNestingGet = library.lookupFunction<_FaunaNestingGetNative, _FaunaNestingGetDart>('odg_fauna_nesting_get'),
        lootTableCount = library.lookupFunction<_U32Native, _U32Dart>('odg_loot_table_count'),
        lootTableGet = library.lookupFunction<_LootGetNative, _LootGetDart>('odg_loot_table_get'),
        copyFauna = library.lookupFunction<_CopyFaunaNative, _CopyFaunaDart>('odg_copy_fauna'),
        copyFaunaNests = library.lookupFunction<_CopyFaunaNestsNative, _CopyFaunaNestsDart>('odg_copy_fauna_nests'),
        worldSurfaceSample = library.lookupFunction<_WorldSurfaceNative, _WorldSurfaceDart>('odg_world_surface_sample64'),
        worldGeologyMaterial = library.lookupFunction<_GeologySampleNative, _GeologySampleDart>('odg_world_geology_material64'),
        worldGeologyOreResource = library.lookupFunction<_GeologySampleNative, _GeologySampleDart>('odg_world_geology_ore_resource64'),
        worldCaveOpennessPermille = library.lookupFunction<_GeologySampleNative, _GeologySampleDart>('odg_world_cave_openness_permille64'),
        worldCaveEntrance = library.lookupFunction<_WorldCellPairNative, _WorldCellPairDart>('odg_world_cave_entrance64'),
        worldgenVersion = library.lookupFunction<_U32Native, _U32Dart>('odg_worldgen_version'),
        dayIndex = library.lookupFunction<_U32Native, _U32Dart>('odg_day_index'),
        dayPhasePermille = library.lookupFunction<_U32Native, _U32Dart>('odg_day_phase_permille'),
        daylightPermille = library.lookupFunction<_U32Native, _U32Dart>('odg_daylight_permille'),
        isNight = library.lookupFunction<_U32Native, _U32Dart>('odg_is_night'),
        playerSatietyPermille = library.lookupFunction<_U32Native, _U32Dart>('odg_player_satiety_permille'),
        playerHydrationPermille = library.lookupFunction<_U32Native, _U32Dart>('odg_player_hydration_permille'),
        playerOxygenPermille = library.lookupFunction<_U32Native, _U32Dart>('odg_player_oxygen_permille'),
        playerTrailBroken = library.lookupFunction<_U32Native, _U32Dart>('odg_player_trail_broken'),
        weatherRainPermille = library.lookupFunction<_U32Native, _U32Dart>('odg_weather_rain_permille'),
        copyArtifacts = library.lookupFunction<_CopyArtifactsNative, _CopyArtifactsDart>('odg_copy_artifacts'),
        copyArtifactsPage = library.lookupFunction<_CopyArtifactsPageNative, _CopyArtifactsPageDart>('odg_copy_artifacts_page'),
        constructionCount = library.lookupFunction<_U32Native, _U32Dart>('odg_construction_count'),
        copyConstructionPage = library.lookupFunction<_CopyConstructionPageNative, _CopyConstructionPageDart>('odg_copy_construction_page'),
        openedArtifactId = library.lookupFunction<_U32Native, _U32Dart>('odg_opened_artifact_id'),
        copyArtifactStorage = library.lookupFunction<_CopyStorageNative, _CopyStorageDart>('odg_copy_artifact_storage'),
        repairQuoteSelected = library.lookupFunction<_RepairQuoteNative, _RepairQuoteDart>('odg_repair_quote_selected'),
        saveSchemaVersion = library.lookupFunction<_U32Native, _U32Dart>('odg_save_schema_version'),
        saveSchemaSupported = library.lookupFunction<_U32ArgNative, _U32ArgDart>('odg_save_schema_supported'),
        saveBlobSize = library.lookupFunction<_U64Native, _U64Dart>('odg_save_blob_size'),
        saveWrite = library.lookupFunction<_SaveWriteNative, _SaveWriteDart>('odg_save_write'),
        saveLoad = library.lookupFunction<_SaveLoadNative, _SaveLoadDart>('odg_save_load'),
        mapQuery = library.lookupFunction<_MapQueryNative, _MapQueryDart>('odg_map_query'),
        renderArtifactView = library.lookupFunction<_RemoteViewNative, _RemoteViewDart>('odg_render_artifact_view'),
        renderAvatarPreview = library.lookupFunction<_RemoteViewNative, _RemoteViewDart>('odg_render_avatar_preview'),
        renderCameraPreview = library.lookupFunction<_CameraPreviewNative, _CameraPreviewDart>('odg_render_camera_preview'),
        avatarTextureUpload = library.lookupFunction<_TextureUploadNative, _TextureUploadDart>('odg_avatar_texture_upload'),
        avatarTextureClear = library.lookupFunction<_TextureFaceNative, _TextureFaceDart>('odg_avatar_texture_clear'),
        musicReset = library.lookupFunction<_MusicResetNative, _MusicResetDart>(
          'odg_music_reset',
        ),
        musicSubmit = library.lookupFunction<_MusicSubmitNative, _MusicSubmitDart>(
          'odg_music_submit_pcm_f32',
        ),
        framebufferBytes = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_framebuffer_bytes',
        ),
        framebufferStrideBytes =
            library.lookupFunction<_U32Native, _U32Dart>(
          'odg_framebuffer_stride_bytes',
        ),
        renderWidth = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_render_width',
        ),
        renderHeight = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_render_height',
        ),
        setVisualTheme =
            library.lookupFunction<_SetU32Native, _SetU32Dart>(
          'odg_set_visual_theme',
        ),
        setCameraMode = library.lookupFunction<_SetU32Native, _SetU32Dart>('odg_set_camera_mode'),
        cameraMode = library.lookupFunction<_U32Native, _U32Dart>('odg_camera_mode'),
        setMusicReactivity = library.lookupFunction<_SetU32Native, _SetU32Dart>('odg_set_music_reactivity_q16'),
        musicReactivity = library.lookupFunction<_U32Native, _U32Dart>('odg_music_reactivity_q16'),
        visualTheme = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_visual_theme',
        ),
        setPresentationMode =
            library.lookupFunction<_SetU32Native, _SetU32Dart>(
          'odg_set_presentation_mode',
        ),
        stateHash = library.lookupFunction<_U64Native, _U64Dart>(
          'odg_state_hash',
        ),
        leaderCount = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_leader_count',
        ),
        leaderScore = library.lookupFunction<_RankNative, _RankDart>(
          'odg_leader_score',
        ),
        leaderNameCode = library.lookupFunction<_RankNative, _RankDart>(
          'odg_leader_name_code',
        ),
        leaderIsPlayer = library.lookupFunction<_RankNative, _RankDart>(
          'odg_leader_is_player',
        );

  factory OdgNativeApi.open() {
    if (!Platform.isAndroid && !Platform.isLinux) {
      throw const OdgNativeException(
        'This Flutter target requires the packaged ODPAR native library.',
      );
    }
    return OdgNativeApi._(
      DynamicLibrary.open('libodpar_territorial_domain.so'),
    );
  }

  final DynamicLibrary library;
  final _ApiVersionDart apiVersion;
  final _AbiQueryDart ffiAbiQuery;
  final _InitDart init;
  final _ResizeDart resize;
  final _ResetDart reset;
  final _SetInputDart setInput;
  final _TickDart tickUs;
  final _RenderDart renderFrame;
  final _CopyFrameDart copyFramebuffer;
  final _CopyStatsDart copyStats;
  final _CopyInventoryDart copyInventory;
  final _CopyInteractionHintDart copyInteractionHint;
  final _CommandSubmitDart commandSubmit;
  final _ItemDefinitionGetDart itemDefinitionGet;
  final _U32Dart recipeCount;
  final _RecipeGetDart recipeGet;
  final _RecipeMaxDart recipeMaxCraftable;
  final _CopyResourcesDart copyResources;
  final _U32Dart foodDefinitionCount;
  final _FoodGetDart foodDefinitionGet;
  final _U32Dart fluidDefinitionCount;
  final _FluidGetDart fluidDefinitionGet;
  final _U32Dart fluidContainerDefinitionCount;
  final _FluidContainerGetDart fluidContainerDefinitionGet;
  final _U32Dart floraSpeciesCount;
  final _FloraGetDart floraSpeciesGet;
  final _U32Dart faunaSpeciesCount;
  final _FaunaSpeciesGetDart faunaSpeciesGet;
  final _U32Dart faunaDietCount;
  final _FaunaDietGetDart faunaDietGet;
  final _U32Dart faunaHabitatCount;
  final _FaunaHabitatGetDart faunaHabitatGet;
  final _U32Dart faunaNestingCount;
  final _FaunaNestingGetDart faunaNestingGet;
  final _U32Dart lootTableCount;
  final _LootGetDart lootTableGet;
  final _CopyFaunaDart copyFauna;
  final _CopyFaunaNestsDart copyFaunaNests;
  final _WorldSurfaceDart worldSurfaceSample;
  final _GeologySampleDart worldGeologyMaterial;
  final _GeologySampleDart worldGeologyOreResource;
  final _GeologySampleDart worldCaveOpennessPermille;
  final _WorldCellPairDart worldCaveEntrance;
  final _U32Dart worldgenVersion;
  final _U32Dart dayIndex;
  final _U32Dart dayPhasePermille;
  final _U32Dart daylightPermille;
  final _U32Dart isNight;
  final _U32Dart playerSatietyPermille;
  final _U32Dart playerHydrationPermille;
  final _U32Dart playerOxygenPermille;
  final _U32Dart playerTrailBroken;
  final _U32Dart weatherRainPermille;
  final _CopyArtifactsDart copyArtifacts;
  final _CopyArtifactsPageDart copyArtifactsPage;
  final _U32Dart constructionCount;
  final _CopyConstructionPageDart copyConstructionPage;
  final _U32Dart openedArtifactId;
  final _CopyStorageDart copyArtifactStorage;
  final _RepairQuoteDart repairQuoteSelected;
  final _U32Dart saveSchemaVersion;
  final _U32ArgDart saveSchemaSupported;
  final _U64Dart saveBlobSize;
  final _SaveWriteDart saveWrite;
  final _SaveLoadDart saveLoad;
  final _MapQueryDart mapQuery;
  final _RemoteViewDart renderArtifactView;
  final _RemoteViewDart renderAvatarPreview;
  final _CameraPreviewDart renderCameraPreview;
  final _TextureUploadDart avatarTextureUpload;
  final _TextureFaceDart avatarTextureClear;
  final _MusicResetDart musicReset;
  final _MusicSubmitDart musicSubmit;
  final _U32Dart framebufferBytes;
  final _U32Dart framebufferStrideBytes;
  final _U32Dart renderWidth;
  final _U32Dart renderHeight;
  final _SetU32Dart setVisualTheme;
  final _SetU32Dart setCameraMode;
  final _U32Dart cameraMode;
  final _SetU32Dart setMusicReactivity;
  final _U32Dart musicReactivity;
  final _U32Dart visualTheme;
  final _SetU32Dart setPresentationMode;
  final _U64Dart stateHash;
  final _U32Dart leaderCount;
  final _RankDart leaderScore;
  final _RankDart leaderNameCode;
  final _RankDart leaderIsPlayer;

  OdgAbiDescriptor queryAndValidateAbi() {
    final Pointer<OdgFfiAbiInfo> info = calloc<OdgFfiAbiInfo>();
    final Pointer<Uint64> required = calloc<Uint64>();
    try {
      final int status = ffiAbiQuery(
        odgFfiAbiVersion,
        info,
        sizeOf<OdgFfiAbiInfo>(),
        required,
      );
      if (status != odgStatusOk) {
        throw OdgNativeException('FFI ABI query failed', status);
      }
      final OdgFfiAbiInfo value = info.ref;
      const int requiredFeatures = odgFfiFeatureFramebufferCopy |
          odgFfiFeatureStatsCopy |
          odgFfiFeaturePortraitRender |
          odgFfiFeatureFixed120Hz |
          odgFfiFeatureCameraInput |
          odgFfiFeatureGenericItems |
          odgFfiFeatureInventoryQuery |
          odgFfiFeatureCommandQueue |
          odgFfiFeatureInteractionHint |
          odgFfiFeatureMusicAnalyzer |
          odgFfiFeatureResources |
          odgFfiFeatureCrafting |
          odgFfiFeatureArtifacts |
          odgFfiFeatureSaveLoad |
          odgFfiFeatureMapQuery |
          odgFfiFeatureTextureUpload |
          odgFfiFeatureAvatarSkins |
          odgFfiFeatureChunkWorldgen |
          odgFfiFeatureCameraProfile |
          odgFfiFeatureRemoteView |
          odgFfiFeatureChunkRuntime |
          odgFfiFeatureTurretModes |
          odgFfiFeatureArtifactPaging |
          odgFfiFeatureEcology |
          odgFfiFeatureNutrition |
          odgFfiFeatureFauna |
          odgFfiFeatureTerrainSurface |
          odgFfiFeatureWeather |
          odgFfiFeatureDeathRecovery |
          odgFfiFeatureTerritoryPolicy |
          odgFfiFeatureTrailBreak |
          odgFfiFeatureHydration |
          odgFfiFeatureFluidRegistry |
          odgFfiFeatureSaveSchemaQuery |
          odgFfiFeatureFaunaNestingRegistry |
          odgFfiFeatureGeology |
          odgFfiFeatureDayNight |
          odgFfiFeatureRespiration |
          odgFfiFeatureConstructionBlocks |
          odgFfiFeatureConstructionShapes |
          odgFfiFeatureConstructionDurability;
      if (required.value != sizeOf<OdgFfiAbiInfo>() ||
          value.structSize != sizeOf<OdgFfiAbiInfo>() ||
          value.ffiAbiVersion != odgFfiAbiVersion ||
          value.engineApiVersion != odgApiVersion ||
          apiVersion() != odgApiVersion ||
          value.endianMarker != odgFfiEndianMarker ||
          value.gameStatsSize != sizeOf<OdgGameStats>() ||
          value.leaderEntrySize != sizeOf<OdgLeaderEntry>() ||
          value.inventorySnapshotSize != sizeOf<OdgInventorySnapshot>() ||
          value.interactionHintSize != sizeOf<OdgInteractionHint>() ||
          value.resourceSnapshotSize != sizeOf<OdgResourceSnapshot>() ||
          value.artifactSnapshotSize != sizeOf<OdgArtifactSnapshot>() ||
          value.foodDefinitionSize != sizeOf<OdgFoodDefinition>() ||
          value.floraSpeciesDefinitionSize != sizeOf<OdgFloraSpeciesDefinition>() ||
          value.faunaSpeciesDefinitionSize != sizeOf<OdgFaunaSpeciesDefinition>() ||
          value.faunaSnapshotSize != sizeOf<OdgFaunaSnapshot>() ||
          value.faunaNestSnapshotSize != sizeOf<OdgFaunaNestSnapshot>() ||
          value.surfaceSampleSize != sizeOf<OdgSurfaceSample>() ||
          value.fluidDefinitionSize != sizeOf<OdgFluidDefinition>() ||
          value.fluidContainerDefinitionSize != sizeOf<OdgFluidContainerDefinition>() ||
          value.faunaNestingDefinitionSize != sizeOf<OdgFaunaNestingDefinition>() ||
          value.constructionSnapshotSize != sizeOf<OdgConstructionSnapshot>() ||
          value.tickRate != odgTickRate ||
          value.framebufferPixelFormat != odgPixelFormatRgba8 ||
          value.framebufferBytesPerPixel != 4 ||
          (value.featureBits & requiredFeatures) != requiredFeatures ||
          value.maxRenderWidth <= 0 ||
          value.maxRenderHeight <= 0 ||
          value.maxRenderPixels <= 0) {
        throw const OdgNativeException(
          'Native library does not satisfy ODG API 37 / FFI ABI v9.',
        );
      }
      return OdgAbiDescriptor(
        maxRenderWidth: value.maxRenderWidth,
        maxRenderHeight: value.maxRenderHeight,
        maxRenderPixels: value.maxRenderPixels,
        featureBits: value.featureBits,
      );
    } finally {
      calloc.free(required);
      calloc.free(info);
    }
  }
}
