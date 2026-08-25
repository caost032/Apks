import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain_greenfield/main.dart';

void main() {
  testWidgets('Slice 1 HUD keeps reticle, movement, jump and telemetry visible',
      (WidgetTester tester) async {
    await tester.pumpWidget(const OdparGreenfieldApp());
    await tester.pump();
    expect(find.byKey(const ValueKey<String>('reticle')), findsOneWidget);
    expect(find.byKey(const ValueKey<String>('movement-stick')), findsOneWidget);
    expect(find.byKey(const ValueKey<String>('jump-button')), findsOneWidget);
    expect(find.byKey(const ValueKey<String>('telemetry')), findsOneWidget);
    expect(find.byKey(const ValueKey<String>('invert-y-toggle')), findsOneWidget);
  });
}
