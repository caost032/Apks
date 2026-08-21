import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

import '../native/odg_bindings.dart';
import '../profile/game_profile.dart';

final class ControlLayout {
  const ControlLayout({
    required this.viewport,
    required this.safePadding,
    required this.joystickCenter,
    required this.joystickRadius,
    required this.actionRect,
    required this.jumpRect,
    required this.dashRect,
    required this.dropRect,
    required this.joystickOpacity,
    required this.actionOpacity,
    required this.jumpOpacity,
    required this.dashOpacity,
    required this.dropOpacity,
  });

  factory ControlLayout.forViewport(
    Size viewport,
    EdgeInsets safePadding, {
    bool moveOnLeft = true,
    ControlProfile? profile,
  }) {
    final double shortest = math.min(viewport.width, viewport.height);
    final double baseRadius = (shortest * 0.105).clamp(48, 70).toDouble();
    if (profile != null) {
      final Rect safe = Rect.fromLTRB(
        safePadding.left,
        safePadding.top,
        viewport.width - safePadding.right,
        viewport.height - safePadding.bottom,
      );
      Offset point(ControlAnchor anchor) => Offset(
            safe.left + safe.width * anchor.x,
            safe.top + safe.height * anchor.y,
          );
      final double radius = baseRadius * profile.joystick.scale;
      final double baseActionDiameter = (baseRadius * 0.96).clamp(54, 68).toDouble();
      final double actionDiameter = baseActionDiameter * profile.interact.scale;
      final double jumpDiameter = baseActionDiameter * profile.jump.scale;
      final double dashDiameter = baseActionDiameter * profile.dash.scale;
      final double dropWidth = (baseRadius * 1.02).clamp(56, 74).toDouble() * profile.drop.scale;
      final double dropHeight = 36 * profile.drop.scale;
      return ControlLayout(
        viewport: viewport,
        safePadding: safePadding,
        joystickCenter: point(profile.joystick),
        joystickRadius: radius,
        actionRect: Rect.fromCenter(center: point(profile.interact), width: actionDiameter, height: actionDiameter),
        jumpRect: Rect.fromCenter(center: point(profile.jump), width: jumpDiameter, height: jumpDiameter),
        dashRect: Rect.fromCenter(center: point(profile.dash), width: dashDiameter, height: dashDiameter),
        dropRect: Rect.fromCenter(center: point(profile.drop), width: dropWidth, height: dropHeight),
        joystickOpacity: profile.joystick.opacity,
        actionOpacity: profile.interact.opacity,
        jumpOpacity: profile.jump.opacity,
        dashOpacity: profile.dash.opacity,
        dropOpacity: profile.drop.opacity,
      );
    }
    final double radius = baseRadius;
    final double sideInset = radius + 20;
    final double bottom = safePadding.bottom + radius + 22;
    final double joystickX = moveOnLeft
        ? safePadding.left + sideInset
        : viewport.width - safePadding.right - sideInset;
    final double actionDiameter = (radius * 0.96).clamp(54, 68).toDouble();
    final double actionX = moveOnLeft
        ? viewport.width - safePadding.right - 18 - actionDiameter
        : safePadding.left + 18;
    final double actionY = viewport.height - safePadding.bottom - 18 - actionDiameter;
    final Rect action = Rect.fromLTWH(actionX, actionY, actionDiameter, actionDiameter);
    final Rect jump = Rect.fromCenter(
      center: Offset(action.left - actionDiameter * 0.68, action.center.dy + actionDiameter * 0.08),
      width: actionDiameter * 0.78,
      height: actionDiameter * 0.78,
    );
    final Rect dash = Rect.fromCenter(
      center: Offset(action.left - actionDiameter * 0.62, action.top - actionDiameter * 0.28),
      width: actionDiameter * 0.74,
      height: actionDiameter * 0.74,
    );
    final Rect drop = Rect.fromLTWH(actionX - 2, action.top - 52, actionDiameter + 4, 36);
    return ControlLayout(
      viewport: viewport,
      safePadding: safePadding,
      joystickCenter: Offset(joystickX, viewport.height - bottom),
      joystickRadius: radius,
      actionRect: action,
      jumpRect: jump,
      dashRect: dash,
      dropRect: drop,
      joystickOpacity: 0.66,
      actionOpacity: 0.72,
      jumpOpacity: 0.68,
      dashOpacity: 0.64,
      dropOpacity: 0.62,
    );
  }

  final Size viewport;
  final EdgeInsets safePadding;
  final Offset joystickCenter;
  final double joystickRadius;
  final Rect actionRect;
  final Rect jumpRect;
  final Rect dashRect;
  final Rect dropRect;
  final double joystickOpacity;
  final double actionOpacity;
  final double jumpOpacity;
  final double dashOpacity;
  final double dropOpacity;
}

enum _PointerRole { move, look, action, jump, dash, drop }

final class GameInputSample {
  const GameInputSample({
    required this.moveX,
    required this.moveForward,
    required this.lookX,
    required this.lookY,
    required this.buttons,
  });

  final double moveX;
  final double moveForward;
  final double lookX;
  final double lookY;
  final int buttons;

  int get moveXQ15 => (moveX * 32767).round().clamp(-32767, 32767).toInt();
  int get moveForwardQ15 => (moveForward * 32767).round().clamp(-32767, 32767).toInt();
  int get lookXQ15 => (lookX * 32767).round().clamp(-32767, 32767).toInt();
  int get lookYQ15 => (lookY * 32767).round().clamp(-32767, 32767).toInt();
}

final class MultiTouchInputRouter extends ChangeNotifier {
  ControlLayout? _layout;
  final Map<int, _PointerRole> _roles = <int, _PointerRole>{};
  final Map<int, Offset> _lastPositions = <int, Offset>{};
  int? _movePointer;
  int? _lookPointer;
  Offset _moveVector = Offset.zero;
  double _lookX = 0;
  double _lookY = 0;
  int? _actionPointer;
  int _jumpPulseFrames = 0;
  int _dashPulseFrames = 0;
  int _dropPulseFrames = 0;
  bool _actionEnabled = false;
  bool _movementActionsEnabled = false;
  bool _dropEnabled = false;
  double _lookSensitivity = 1.0;

  Offset get moveVector => _moveVector;
  bool get actionPressed => _actionPointer != null;
  bool get jumpPressed => _jumpPulseFrames > 0;
  bool get dashPressed => _dashPulseFrames > 0;
  bool get dropPressed => _dropPulseFrames > 0;

  void setLookSensitivity(double value) {
    _lookSensitivity = value.clamp(0.40, 2.00).toDouble();
  }

  void updateLayout(ControlLayout layout) {
    _layout = layout;
    if (_movePointer != null) _updateMove(_lastPositions[_movePointer!]);
  }

  void setActionsEnabled({required bool action, required bool movement, required bool drop}) {
    _actionEnabled = action;
    _movementActionsEnabled = movement;
    _dropEnabled = drop;
  }

  void pointerDown(int pointer, Offset position) {
    final ControlLayout? layout = _layout;
    if (layout == null || _roles.containsKey(pointer)) return;
    if (_actionEnabled && layout.actionRect.contains(position)) {
      _roles[pointer] = _PointerRole.action;
      _actionPointer = pointer;
    } else if (_movementActionsEnabled && layout.jumpRect.contains(position)) {
      _roles[pointer] = _PointerRole.jump;
      _jumpPulseFrames = 2;
    } else if (_movementActionsEnabled && layout.dashRect.contains(position)) {
      _roles[pointer] = _PointerRole.dash;
      _dashPulseFrames = 2;
    } else if (_dropEnabled && layout.dropRect.contains(position)) {
      _roles[pointer] = _PointerRole.drop;
      _dropPulseFrames = 2;
    } else if (_movePointer == null &&
        (position - layout.joystickCenter).distance <= layout.joystickRadius * 1.42) {
      _roles[pointer] = _PointerRole.move;
      _movePointer = pointer;
      _lastPositions[pointer] = position;
      _updateMove(position);
    } else if (_lookPointer == null) {
      _roles[pointer] = _PointerRole.look;
      _lookPointer = pointer;
      _lastPositions[pointer] = position;
    }
    notifyListeners();
  }

  void pointerMove(int pointer, Offset position) {
    final _PointerRole? role = _roles[pointer];
    if (role == _PointerRole.move) {
      _lastPositions[pointer] = position;
      _updateMove(position);
      notifyListeners();
    } else if (role == _PointerRole.look) {
      final Offset previous = _lastPositions[pointer] ?? position;
      final Offset delta = position - previous;
      _lastPositions[pointer] = position;
      _lookX = (_lookX + delta.dx * 0.026 * _lookSensitivity).clamp(-1, 1).toDouble();
      _lookY = (_lookY + delta.dy * 0.026 * _lookSensitivity).clamp(-1, 1).toDouble();
    }
  }

  void pointerUp(int pointer) {
    final _PointerRole? role = _roles.remove(pointer);
    _lastPositions.remove(pointer);
    if (role == _PointerRole.move) {
      _movePointer = null;
      _moveVector = Offset.zero;
    } else if (role == _PointerRole.look) {
      _lookPointer = null;
    } else if (role == _PointerRole.action) {
      if (_actionPointer == pointer) _actionPointer = null;
    }
    notifyListeners();
  }

  GameInputSample sample() {
    int buttons = 0;
    if (_actionPointer != null) buttons |= odgButtonAction;
    if (_jumpPulseFrames > 0) {
      buttons |= odgButtonJump;
      _jumpPulseFrames -= 1;
    }
    if (_dashPulseFrames > 0) {
      buttons |= odgButtonDash;
      _dashPulseFrames -= 1;
    }
    if (_dropPulseFrames > 0) {
      buttons |= odgButtonDrop;
      _dropPulseFrames -= 1;
    }
    final GameInputSample result = GameInputSample(
      moveX: _moveVector.dx,
      moveForward: -_moveVector.dy,
      lookX: _lookX,
      lookY: _lookY,
      buttons: buttons,
    );
    _lookX *= 0.44;
    _lookY *= 0.44;
    if (_lookX.abs() < 0.003) _lookX = 0;
    if (_lookY.abs() < 0.003) _lookY = 0;
    return result;
  }

  void clear() {
    _roles.clear();
    _lastPositions.clear();
    _movePointer = null;
    _lookPointer = null;
    _moveVector = Offset.zero;
    _lookX = 0;
    _lookY = 0;
    _actionPointer = null;
    _jumpPulseFrames = 0;
    _dashPulseFrames = 0;
    _dropPulseFrames = 0;
    notifyListeners();
  }

  void _updateMove(Offset? position) {
    final ControlLayout? layout = _layout;
    if (layout == null || position == null) {
      _moveVector = Offset.zero;
      return;
    }
    final Offset raw = (position - layout.joystickCenter) / layout.joystickRadius;
    final double magnitude = raw.distance;
    if (magnitude <= 0.075) {
      _moveVector = Offset.zero;
      return;
    }
    final double normalizedMagnitude = ((math.min(1, magnitude) - 0.075) / 0.925).clamp(0, 1).toDouble();
    _moveVector = raw / magnitude * normalizedMagnitude;
  }
}
