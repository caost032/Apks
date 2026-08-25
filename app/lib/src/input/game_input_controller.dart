import 'dart:math' as math;
import 'dart:ui' show Offset, Size;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';

const int _q15Max = 32767;
const int odgJumpButton = 1;

enum PointerRole { movement, look }

@immutable
final class InputPacket {
  const InputPacket({
    required this.sequence,
    required this.moveXQ15,
    required this.moveForwardQ15,
    required this.lookYawQ15,
    required this.lookPitchQ15,
    required this.buttonsPressed,
    required this.buttonsHeld,
  });

  final int sequence;
  final int moveXQ15;
  final int moveForwardQ15;
  final int lookYawQ15;
  final int lookPitchQ15;
  final int buttonsPressed;
  final int buttonsHeld;

  Map<String, Object> toMessage() => <String, Object>{
        'type': 'input',
        'sequence': sequence,
        'moveXQ15': moveXQ15,
        'moveForwardQ15': moveForwardQ15,
        'lookYawQ15': lookYawQ15,
        'lookPitchQ15': lookPitchQ15,
        'buttonsPressed': buttonsPressed,
        'buttonsHeld': buttonsHeld,
      };
}

final class GameInputController extends ChangeNotifier {
  GameInputController({this.lookSensitivity = 0.55});

  final double lookSensitivity;
  final Map<int, PointerRole> _roles = <int, PointerRole>{};
  final Map<int, Offset> _lastPosition = <int, Offset>{};
  final Map<int, Offset> _lookSlopAccum = <int, Offset>{};

  Offset _moveOrigin = Offset.zero;
  Offset _movement = Offset.zero;
  double _lookYawAccum = 0.0;
  double _lookPitchAccum = 0.0;
  int _buttonsPressed = 0;
  int _buttonsHeld = 0;
  int _sequence = 0;
  bool _invertY = false;

  bool get invertY => _invertY;
  Offset get movement => _movement;

  void setInvertY(bool value) {
    if (_invertY == value) return;
    _invertY = value;
    notifyListeners();
  }

  void pointerDown(PointerDownEvent event, Size viewport) {
    final bool jumpZone = event.position.dx > viewport.width - 118.0 &&
        event.position.dy > viewport.height - 148.0;
    final bool settingsZone = event.position.dx > viewport.width - 116.0 &&
        event.position.dy < 104.0;
    if (jumpZone || settingsZone) return;
    final PointerRole role = event.position.dx < viewport.width * 0.46 &&
            event.position.dy > viewport.height * 0.40
        ? PointerRole.movement
        : PointerRole.look;
    // One owner per gesture role: one movement finger + one look finger.
    // Additional pointers may still belong to dedicated action widgets.
    if (_roles.containsValue(role)) return;
    _roles[event.pointer] = role;
    _lastPosition[event.pointer] = event.position;
    if (role == PointerRole.look) {
      _lookSlopAccum[event.pointer] = Offset.zero;
    }
    if (role == PointerRole.movement) {
      _moveOrigin = event.position;
      _movement = Offset.zero;
      notifyListeners();
    }
  }

  void pointerMove(PointerMoveEvent event, Size viewport) {
    final PointerRole? role = _roles[event.pointer];
    final Offset? last = _lastPosition[event.pointer];
    if (role == null || last == null) return;
    _lastPosition[event.pointer] = event.position;
    if (role == PointerRole.movement) {
      const double radius = 58.0;
      final Offset raw = event.position - _moveOrigin;
      final double length = raw.distance;
      final Offset clamped = length > radius ? raw / length * radius : raw;
      _movement = Offset(
        clamped.dx / radius,
        -clamped.dy / radius,
      );
      notifyListeners();
      return;
    }

    Offset delta = event.position - last;
    final Offset slopAccum = (_lookSlopAccum[event.pointer] ?? Offset.zero) + delta;
    if (slopAccum.distance < 2.5) {
      _lookSlopAccum[event.pointer] = slopAccum;
      return;
    }
    if (_lookSlopAccum[event.pointer] != null) {
      delta = slopAccum;
      _lookSlopAccum.remove(event.pointer);
    }
    final double basis = math.max(240.0, math.min(viewport.width, viewport.height));
    final double scale = lookSensitivity * 2.25 / basis;
    _lookYawAccum += delta.dx * scale;
    // Natural convention: a finger moving up (negative dy) produces
    // positive pitch, and the C contract defines positive pitch as LOOK UP.
    final double naturalPitch = -delta.dy * scale;
    _lookPitchAccum += _invertY ? -naturalPitch : naturalPitch;
  }

  void pointerUpOrCancel(int pointer) {
    final PointerRole? role = _roles.remove(pointer);
    _lastPosition.remove(pointer);
    _lookSlopAccum.remove(pointer);
    if (role == PointerRole.movement) {
      _movement = Offset.zero;
      notifyListeners();
    }
  }

  void pressJump() {
    _buttonsPressed |= odgJumpButton;
    _buttonsHeld |= odgJumpButton;
  }

  void releaseJump() {
    _buttonsHeld &= ~odgJumpButton;
  }

  void cancelActiveGestures() {
    final bool changed = _roles.isNotEmpty ||
        _movement != Offset.zero ||
        _lookYawAccum != 0.0 ||
        _lookPitchAccum != 0.0 ||
        _buttonsPressed != 0 ||
        _buttonsHeld != 0;
    _roles.clear();
    _lastPosition.clear();
    _lookSlopAccum.clear();
    _movement = Offset.zero;
    _lookYawAccum = 0.0;
    _lookPitchAccum = 0.0;
    _buttonsPressed = 0;
    _buttonsHeld = 0;
    if (changed) notifyListeners();
  }

  InputPacket sampleForEngine() {
    _sequence += 1;
    // The ABI carries one normalized look chunk per packet. If the UI was
    // briefly unable to submit, drain large accumulated swipes over subsequent
    // packets instead of clipping and losing player intent.
    final double yawChunk = _lookYawAccum.clamp(-1.0, 1.0).toDouble();
    final double pitchChunk = _lookPitchAccum.clamp(-1.0, 1.0).toDouble();
    final InputPacket packet = InputPacket(
      sequence: _sequence,
      moveXQ15: _toQ15(_movement.dx),
      moveForwardQ15: _toQ15(_movement.dy),
      lookYawQ15: _toDeltaQ15(yawChunk),
      lookPitchQ15: _toDeltaQ15(pitchChunk),
      buttonsPressed: _buttonsPressed,
      buttonsHeld: _buttonsHeld,
    );
    _lookYawAccum -= yawChunk;
    _lookPitchAccum -= pitchChunk;
    _buttonsPressed = 0;
    return packet;
  }

  void restoreUnsentLook(InputPacket packet) {
    _lookYawAccum += packet.lookYawQ15 / _q15Max;
    _lookPitchAccum += packet.lookPitchQ15 / _q15Max;
    _buttonsPressed |= packet.buttonsPressed;
  }

  static int _toQ15(double value) =>
      (value.clamp(-1.0, 1.0) * _q15Max).round();

  static int _toDeltaQ15(double value) =>
      (value.clamp(-1.0, 1.0) * _q15Max).round();
}
