import 'package:flutter/gestures.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain_greenfield/src/input/game_input_controller.dart';

void main() {
  const Size viewport = Size(400, 800);

  test('natural swipe up produces positive LOOK UP pitch exactly once', () {
    final GameInputController input = GameInputController();
    input.pointerDown(
      const PointerDownEvent(pointer: 1, position: Offset(300, 500)),
      viewport,
    );
    input.pointerMove(
      const PointerMoveEvent(
        pointer: 1,
        position: Offset(300, 450),
        delta: Offset(0, -50),
      ),
      viewport,
    );
    final InputPacket first = input.sampleForEngine();
    final InputPacket second = input.sampleForEngine();
    expect(first.lookPitchQ15, greaterThan(0));
    expect(second.lookPitchQ15, 0);
  });

  test('invert Y flips natural pitch once and only once', () {
    final GameInputController input = GameInputController()..setInvertY(true);
    input.pointerDown(
      const PointerDownEvent(pointer: 2, position: Offset(300, 500)),
      viewport,
    );
    input.pointerMove(
      const PointerMoveEvent(
        pointer: 2,
        position: Offset(300, 450),
        delta: Offset(0, -50),
      ),
      viewport,
    );
    expect(input.sampleForEngine().lookPitchQ15, lessThan(0));
  });

  test('micro jitter stays below look touch slop', () {
    final GameInputController input = GameInputController();
    input.pointerDown(
      const PointerDownEvent(pointer: 7, position: Offset(300, 500)),
      viewport,
    );
    input.pointerMove(
      const PointerMoveEvent(pointer: 7, position: Offset(301, 501)),
      viewport,
    );
    final InputPacket packet = input.sampleForEngine();
    expect(packet.lookYawQ15, 0);
    expect(packet.lookPitchQ15, 0);
  });

  test('large look delta is drained without clipping away the remainder', () {
    final GameInputController input = GameInputController();
    input.pointerDown(
      const PointerDownEvent(pointer: 9, position: Offset(300, 600)),
      viewport,
    );
    input.pointerMove(
      const PointerMoveEvent(pointer: 9, position: Offset(300, 100)),
      viewport,
    );
    final InputPacket first = input.sampleForEngine();
    final InputPacket second = input.sampleForEngine();
    final InputPacket third = input.sampleForEngine();
    expect(first.lookPitchQ15, greaterThan(0));
    expect(second.lookPitchQ15, greaterThan(0));
    expect(third.lookPitchQ15, 0);
  });

  test('second look pointer cannot steal look gesture ownership', () {
    final GameInputController input = GameInputController();
    input.pointerDown(
      const PointerDownEvent(pointer: 10, position: Offset(300, 500)),
      viewport,
    );
    input.pointerDown(
      const PointerDownEvent(pointer: 11, position: Offset(330, 500)),
      viewport,
    );
    input.pointerMove(
      const PointerMoveEvent(pointer: 11, position: Offset(380, 500)),
      viewport,
    );
    expect(input.sampleForEngine().lookYawQ15, 0);
  });

  test('jump zone is not stolen by camera-look ownership', () {
    final GameInputController input = GameInputController();
    input.pointerDown(
      const PointerDownEvent(pointer: 8, position: Offset(360, 750)),
      viewport,
    );
    input.pointerMove(
      const PointerMoveEvent(pointer: 8, position: Offset(340, 720)),
      viewport,
    );
    final InputPacket packet = input.sampleForEngine();
    expect(packet.lookYawQ15, 0);
    expect(packet.lookPitchQ15, 0);
  });

  test('forward stick is independent from camera look ownership', () {
    final GameInputController input = GameInputController();
    input.pointerDown(
      const PointerDownEvent(pointer: 1, position: Offset(80, 650)),
      viewport,
    );
    input.pointerMove(
      const PointerMoveEvent(pointer: 1, position: Offset(80, 600)),
      viewport,
    );
    final InputPacket packet = input.sampleForEngine();
    expect(packet.moveForwardQ15, greaterThan(0));
    expect(packet.lookYawQ15, 0);
    expect(packet.lookPitchQ15, 0);
  });
  test('lifecycle cancellation clears continuous and accumulated input', () {
    final GameInputController input = GameInputController();
    input.pointerDown(
      const PointerDownEvent(pointer: 21, position: Offset(80, 650)),
      viewport,
    );
    input.pointerMove(
      const PointerMoveEvent(pointer: 21, position: Offset(100, 590)),
      viewport,
    );
    input.pressJump();
    input.cancelActiveGestures();
    final InputPacket packet = input.sampleForEngine();
    expect(packet.moveXQ15, 0);
    expect(packet.moveForwardQ15, 0);
    expect(packet.lookYawQ15, 0);
    expect(packet.lookPitchQ15, 0);
    expect(packet.buttonsPressed, 0);
    expect(packet.buttonsHeld, 0);
  });

}
