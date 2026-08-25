final class EngineSnapshot {
  const EngineSnapshot({
    required this.sequence,
    required this.simulationStep,
    required this.publishedNs,
    required this.playerX,
    required this.playerY,
    required this.playerZ,
    required this.playerSpeed,
    required this.playerFacingYaw,
    required this.cameraYaw,
    required this.cameraPitch,
    required this.cameraDistance,
    required this.grounded,
    required this.overloadCount,
    required this.simP50Us,
    required this.simP95Us,
    required this.simP99Us,
    required this.simMaxUs,
    required this.simSpikesOver5Ms,
    required this.renderP50Us,
    required this.renderP95Us,
    required this.renderP99Us,
    required this.renderMaxUs,
    required this.renderSpikesOver16Ms,
    required this.inputAgeUs,
    required this.renderWidth,
    required this.renderHeight,
  });

  final int sequence;
  final int simulationStep;
  final int publishedNs;
  final double playerX;
  final double playerY;
  final double playerZ;
  final double playerSpeed;
  final double playerFacingYaw;
  final double cameraYaw;
  final double cameraPitch;
  final double cameraDistance;
  final bool grounded;
  final int overloadCount;
  final int simP50Us;
  final int simP95Us;
  final int simP99Us;
  final int simMaxUs;
  final int simSpikesOver5Ms;
  final int renderP50Us;
  final int renderP95Us;
  final int renderP99Us;
  final int renderMaxUs;
  final int renderSpikesOver16Ms;
  final int inputAgeUs;
  final int renderWidth;
  final int renderHeight;

  Map<String, Object> toMessage() => <String, Object>{
        'type': 'snapshot',
        'sequence': sequence,
        'simulationStep': simulationStep,
        'publishedNs': publishedNs,
        'playerX': playerX,
        'playerY': playerY,
        'playerZ': playerZ,
        'playerSpeed': playerSpeed,
        'playerFacingYaw': playerFacingYaw,
        'cameraYaw': cameraYaw,
        'cameraPitch': cameraPitch,
        'cameraDistance': cameraDistance,
        'grounded': grounded,
        'overloadCount': overloadCount,
        'simP50Us': simP50Us,
        'simP95Us': simP95Us,
        'simP99Us': simP99Us,
        'simMaxUs': simMaxUs,
        'simSpikesOver5Ms': simSpikesOver5Ms,
        'renderP50Us': renderP50Us,
        'renderP95Us': renderP95Us,
        'renderP99Us': renderP99Us,
        'renderMaxUs': renderMaxUs,
        'renderSpikesOver16Ms': renderSpikesOver16Ms,
        'inputAgeUs': inputAgeUs,
        'renderWidth': renderWidth,
        'renderHeight': renderHeight,
      };

  factory EngineSnapshot.fromMessage(Map<Object?, Object?> value) {
    return EngineSnapshot(
      sequence: value['sequence']! as int,
      simulationStep: value['simulationStep']! as int,
      publishedNs: value['publishedNs']! as int,
      playerX: value['playerX']! as double,
      playerY: value['playerY']! as double,
      playerZ: value['playerZ']! as double,
      playerSpeed: value['playerSpeed']! as double,
      playerFacingYaw: value['playerFacingYaw']! as double,
      cameraYaw: value['cameraYaw']! as double,
      cameraPitch: value['cameraPitch']! as double,
      cameraDistance: value['cameraDistance']! as double,
      grounded: value['grounded']! as bool,
      overloadCount: value['overloadCount']! as int,
      simP50Us: value['simP50Us']! as int,
      simP95Us: value['simP95Us']! as int,
      simP99Us: value['simP99Us']! as int,
      simMaxUs: value['simMaxUs']! as int,
      simSpikesOver5Ms: value['simSpikesOver5Ms']! as int,
      renderP50Us: value['renderP50Us']! as int,
      renderP95Us: value['renderP95Us']! as int,
      renderP99Us: value['renderP99Us']! as int,
      renderMaxUs: value['renderMaxUs']! as int,
      renderSpikesOver16Ms: value['renderSpikesOver16Ms']! as int,
      inputAgeUs: value['inputAgeUs']! as int,
      renderWidth: value['renderWidth']! as int,
      renderHeight: value['renderHeight']! as int,
    );
  }
}
